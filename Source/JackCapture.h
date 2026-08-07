// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#if defined(__linux__)
 #include <dlfcn.h>   // PipeWire-JACK is dlopen'd at runtime; Linux-only (stub elsewhere)
#endif

// Slice 3 of the external-instrument / session-host epic (ideas.md): pull a standalone app's
// audio (e.g. ZynAddSubFX -O jack) INTO Gloopy's mix so it flows through the track's inserts/fader.
//
// Gloopy isn't built against JACK (no jack/jack.h here), and on this box libjack is only the
// PipeWire shim. So we dlopen it at runtime (full path first) and declare the handful of JACK ABI
// functions we need ourselves. If it can't be loaded/opened, capture is simply unavailable and
// external-instrument tracks stay silent (slices 1-2 still work) — no hard dependency.
//
// One JACK client ("Gloopy") with a FIXED POOL of stereo input port-pairs (slots). An external
// track claims a slot and connects the app's JACK output ports to it; the JACK RT process callback
// copies each active slot's port audio into a lock-free SPSC ring (juce::AbstractFifo); the audio
// thread drains the ring in the generator's render(). Fixed pool = the RT callback never races a
// registry mutation (slots are registered once; claiming just flips an atomic + connects ports).
class JackCapture
{
public:
    static constexpr int kSlots      = 8;
    static constexpr int kRingFrames = 1 << 14;   // ~0.37s @44.1k of headroom for callback jitter

    JackCapture()
    {
        if (! load()) { std::cout << "[jack] libjack not found — external audio disabled" << std::endl; return; }
        int status = 0;
        client = jack_client_open ("Gloopy", 1 /*JackNoStartServer*/, &status);
        if (client == nullptr) { std::cout << "[jack] could not open a JACK/PipeWire client — external audio disabled" << std::endl; return; }
        for (int i = 0; i < kSlots; ++i)
        {
            slots[(size_t) i].pL = jack_port_register (client, ("ext" + juce::String (i + 1) + "_L").toRawUTF8(),
                                                       "32 bit float mono audio", 0x1 /*JackPortIsInput*/, 0);
            slots[(size_t) i].pR = jack_port_register (client, ("ext" + juce::String (i + 1) + "_R").toRawUTF8(),
                                                       "32 bit float mono audio", 0x1, 0);
        }
        jack_set_process_callback (client, &JackCapture::processStatic, this);
        jack_activate (client);
        jackSR = jack_get_sample_rate (client);
        available = true;
        std::cout << "[jack] capture client ready (" << kSlots << " stereo slots, sr="
                  << jackSR << ")" << std::endl;
    }

    ~JackCapture()
    {
        if (client != nullptr) { jack_deactivate (client); jack_client_close (client); }
#if defined(__linux__)
        if (lib != nullptr) dlclose (lib);
#endif
    }

    bool isAvailable() const { return available; }

    /** The JACK/PipeWire graph sample rate — the rate the capture ring is FILLED at. Callers
        resample by (jackSampleRate / theirDeviceRate) when draining (see read()). */
    double jackSampleRate() const { return (double) jackSR; }

    /** Claim a free slot (message thread). Returns -1 if none/unavailable. */
    int claim()
    {
        if (! available) return -1;
        for (int i = 0; i < kSlots; ++i)
        {
            bool expected = false;
            if (slots[(size_t) i].active.compare_exchange_strong (expected, true))
            {
                slots[(size_t) i].fifo.reset();
                slots[(size_t) i].frac = 0.0;
                return i;
            }
        }
        return -1;
    }

    /** Release a slot and disconnect its ports (message thread). */
    void release (int slot)
    {
        if (! available || slot < 0 || slot >= kSlots) return;
        auto& s = slots[(size_t) slot];
        s.active.store (false);
        if (s.pL) jack_port_disconnect (client, s.pL);
        if (s.pR) jack_port_disconnect (client, s.pR);
    }

    /** Connect the given source JACK output ports (an app's outputs) to this slot's inputs.
        Pass the port full-names ("ClientName:out_1"); connects up to two (L/R). Message thread. */
    void connect (int slot, const juce::StringArray& srcPorts)
    {
        if (! available || slot < 0 || slot >= kSlots) return;
        auto& s = slots[(size_t) slot];
        if (srcPorts.size() >= 1 && s.pL) jack_connect (client, srcPorts[0].toRawUTF8(), jack_port_name (s.pL));
        if (srcPorts.size() >= 2 && s.pR) jack_connect (client, srcPorts[1].toRawUTF8(), jack_port_name (s.pR));
        else if (srcPorts.size() == 1 && s.pR) jack_connect (client, srcPorts[0].toRawUTF8(), jack_port_name (s.pR)); // mono -> both
    }

    /** All JACK output ports (destinations we can capture) — used to discover an app's outputs. */
    juce::StringArray outputPorts() const
    {
        juce::StringArray out;
        if (! available) return out;
        if (auto** p = jack_get_ports (client, nullptr, "32 bit float mono audio", 0x2 /*JackPortIsOutput*/))
        {
            for (int i = 0; p[i] != nullptr; ++i) out.add (p[i]);
            jack_free (p);
        }
        return out;
    }

    /** Audio thread: drain n output frames of this slot into outL/outR (overwrites), resampling
        the ring from the JACK graph rate to the caller's device rate by `ratio` = jackSR/deviceSR
        (linear interpolation). Consuming ratio input frames per output frame keeps the ring
        balanced despite the two clocks running at different nominal rates (48k vs 44.1k) — without
        this the ring overflows and drops samples continuously, which is heard as clicking. */
    void read (int slot, float* outL, float* outR, int n, double ratio)
    {
        if (! available || slot < 0 || slot >= kSlots)
        { for (int i = 0; i < n; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; } return; }
        auto& s = slots[(size_t) slot];
        if (ratio <= 0.0) ratio = 1.0;

        // Peek a contiguous run of input frames (do NOT finishedRead yet — we advance by exactly
        // the number of whole input frames the resampler consumes, carrying the fractional phase).
        const int scratch = (int) s.inL.size();
        int want = (int) (s.frac + (double) n * ratio) + 2;
        if (want > scratch) want = scratch;
        const int take = juce::jmin (want, s.fifo.getNumReady());
        int start1, size1, start2, size2;
        s.fifo.prepareToRead (take, start1, size1, start2, size2);
        for (int k = 0; k < size1; ++k) { s.inL[(size_t) k] = s.bufL[(size_t) (start1 + k)]; s.inR[(size_t) k] = s.bufR[(size_t) (start1 + k)]; }
        for (int k = 0; k < size2; ++k) { s.inL[(size_t) (size1 + k)] = s.bufL[(size_t) (start2 + k)]; s.inR[(size_t) (size1 + k)] = s.bufR[(size_t) (start2 + k)]; }

        double pos = s.frac;
        int produced = 0;
        for (; produced < n; ++produced)
        {
            const int i = (int) pos;
            if (i + 1 >= take) break;                       // not enough input -> underrun, pad below
            const float f = (float) (pos - i);
            outL[produced] = s.inL[(size_t) i] + (s.inL[(size_t) (i + 1)] - s.inL[(size_t) i]) * f;
            outR[produced] = s.inR[(size_t) i] + (s.inR[(size_t) (i + 1)] - s.inR[(size_t) i]) * f;
            pos += ratio;
        }
        int consumed = (int) pos;                            // whole input frames passed
        if (consumed > take) consumed = take;
        s.fifo.finishedRead (consumed);
        s.frac = pos - consumed;                             // carry fractional phase (< 1)
        for (; produced < n; ++produced) { outL[produced] = 0.0f; outR[produced] = 0.0f; }
    }

private:
    // --- minimal JACK ABI (declared here — no jack/jack.h on this system) --------------------
    using jack_client_t = void; using jack_port_t = void;
    jack_client_t* (*jack_client_open)(const char*, int, int*, ...) = nullptr;
    int   (*jack_client_close)(jack_client_t*) = nullptr;
    int   (*jack_activate)(jack_client_t*) = nullptr;
    int   (*jack_deactivate)(jack_client_t*) = nullptr;
    jack_port_t* (*jack_port_register)(jack_client_t*, const char*, const char*, unsigned long, unsigned long) = nullptr;
    void* (*jack_port_get_buffer)(jack_port_t*, unsigned int) = nullptr;
    const char* (*jack_port_name)(const jack_port_t*) = nullptr;
    int   (*jack_set_process_callback)(jack_client_t*, int(*)(unsigned int, void*), void*) = nullptr;
    int   (*jack_connect)(jack_client_t*, const char*, const char*) = nullptr;
    int   (*jack_port_disconnect)(jack_client_t*, jack_port_t*) = nullptr;
    const char** (*jack_get_ports)(jack_client_t*, const char*, const char*, unsigned long) = nullptr;
    void  (*jack_free)(void*) = nullptr;
    unsigned int (*jack_get_sample_rate)(jack_client_t*) = nullptr;

    void* lib { nullptr };
    jack_client_t* client { nullptr };
    bool available { false };

    static constexpr int kScratch = 8192;   // max input frames peeked per read() (>= maxBlock*ratio)
    struct Slot
    {
        jack_port_t* pL { nullptr }; jack_port_t* pR { nullptr };
        std::atomic<bool> active { false };
        juce::AbstractFifo fifo { kRingFrames };
        std::array<float, (size_t) kRingFrames> bufL {}; std::array<float, (size_t) kRingFrames> bufR {};
        double frac { 0.0 };                                    // resampler fractional phase (audio thread)
        std::array<float, (size_t) kScratch> inL {}, inR {};    // contiguous peek scratch (audio thread)
    };
    std::array<Slot, (size_t) kSlots> slots;
    unsigned int jackSR { 48000 };

    template <typename T> bool sym (T& fp, const char* name) { fp = reinterpret_cast<T> (dlsym (lib, name)); return fp != nullptr; }

    bool load()
    {
#if defined(__linux__)
        const char* paths[] = { "/usr/lib64/pipewire-0.3/jack/libjack.so.0", "libjack.so.0", "libjack.so" };
        for (auto* p : paths) if ((lib = dlopen (p, RTLD_NOW | RTLD_LOCAL)) != nullptr) break;
        if (lib == nullptr) return false;
        return sym (jack_client_open, "jack_client_open") && sym (jack_client_close, "jack_client_close")
            && sym (jack_activate, "jack_activate") && sym (jack_deactivate, "jack_deactivate")
            && sym (jack_port_register, "jack_port_register") && sym (jack_port_get_buffer, "jack_port_get_buffer")
            && sym (jack_port_name, "jack_port_name") && sym (jack_set_process_callback, "jack_set_process_callback")
            && sym (jack_connect, "jack_connect") && sym (jack_port_disconnect, "jack_port_disconnect")
            && sym (jack_get_ports, "jack_get_ports") && sym (jack_free, "jack_free")
            && sym (jack_get_sample_rate, "jack_get_sample_rate");
#else
        return false;   // non-Linux: no PipeWire-JACK, capture unavailable (external tracks silent)
#endif
    }

    static int processStatic (unsigned int nframes, void* arg) { return static_cast<JackCapture*> (arg)->process ((int) nframes); }

    int process (int nframes)   // JACK RT thread — lock-free, no alloc
    {
        for (auto& s : slots)
        {
            if (! s.active.load (std::memory_order_relaxed) || s.pL == nullptr) continue;
            auto* inL = static_cast<const float*> (jack_port_get_buffer (s.pL, (unsigned) nframes));
            auto* inR = s.pR ? static_cast<const float*> (jack_port_get_buffer (s.pR, (unsigned) nframes)) : inL;
            int start1, size1, start2, size2;
            const int room = juce::jmin (nframes, s.fifo.getFreeSpace());
            s.fifo.prepareToWrite (room, start1, size1, start2, size2);
            int done = 0;
            for (int k = 0; k < size1; ++k) { s.bufL[(size_t) (start1 + k)] = inL[done]; s.bufR[(size_t) (start1 + k)] = inR[done]; ++done; }
            for (int k = 0; k < size2; ++k) { s.bufL[(size_t) (start2 + k)] = inL[done]; s.bufR[(size_t) (start2 + k)] = inR[done]; ++done; }
            s.fifo.finishedWrite (done);
        }
        return 0;
    }
};
