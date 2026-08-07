// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <ableton/LinkAudio.hpp>
#include "Generator.h"
#include <atomic>
#include <array>

/** Stage C of the Ableton Link epic (ideas.md): RECEIVE a peer's LinkAudio channel into a Gloopy
    track so it flows through the track's inserts + fader like any generator. A LinkAudioSource
    subscribes to the chosen channel; its callback (on a Link-managed thread) deinterleaves the
    received 16-bit PCM into a lock-free SPSC ring (juce::AbstractFifo); render() drains the ring on
    the audio thread, resampling the sender's rate to the device rate (linear, same pattern as
    JackCapture) which also keeps the ring balanced. Mirrors the send side (LinkAudioSink) in
    LinkController. */
class LinkAudioReceiver : public Generator,
                          private juce::Timer
{
public:
    static constexpr int kRing    = 1 << 16;   // ~1.3s @48k — absorbs bursty network delivery
    static constexpr int kScratch = 8192;      // max input frames peeked per render()
    static constexpr int kLowWater = 3072;     // fade out when the ring drains below this (~64ms of
                                               // audio still buffered to ramp over) → clean stop
    static constexpr int kPrime   = 12288;     // ~256ms @48k cushion before draining; network audio
                                               // arrives in scheduling/packet bursts, not on a steady
                                               // audio clock, so a small cushion starves -> choppy.

    LinkAudioReceiver (ableton::LinkAudio& link, ableton::LinkAudio::Channel channel, juce::String name,
                       std::shared_ptr<std::atomic<bool>> active)
        : displayName (std::move (name)),
          channelName (juce::String (channel.name)),
          peerName    (juce::String (channel.peerName)),
          linkPtr     (&link),
          activeFlag  (std::move (active))
    {
        subscribe (channel.id);
        // Watchdog (message thread): if buffers stop arriving (peer left / Link toggled off) the
        // source goes dead and never revives on its own. Re-subscribe whenever our channel is
        // (re)available and we're idle — so re-linking, or the peer coming back, just resumes.
        startTimer (400);
    }

    ~LinkAudioReceiver() override { stopTimer(); source.reset(); }   // stops the watchdog + receive callback

    // --- Generator ------------------------------------------------------------
    void prepare (double sr, int block) override
    {
        deviceRate = sr; maxBlock = block;
        scratchL.assign ((size_t) block, 0.0f);
        scratchR.assign ((size_t) block, 0.0f);
    }

    void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&, int startSample, int numSamples) override
    {
        if (numSamples > (int) scratchL.size()) return;
        const double srcRate = recvRate.load (std::memory_order_relaxed);
        const double ratio   = (srcRate > 0.0 && deviceRate > 0.0) ? srcRate / deviceRate : 1.0;
        drain (scratchL.data(), scratchR.data(), numSamples, ratio);
        buffer.addFrom (0, startSample, scratchL.data(), numSamples);
        if (buffer.getNumChannels() > 1)
            buffer.addFrom (1, startSample, scratchR.data(), numSamples);
    }

    void allNotesOff() override {}
    juce::String typeName() const override { return "LinkAudio"; }

    const juce::String& getChannelName() const { return channelName; }
    const juce::String& getPeerName()    const { return peerName; }
    const juce::String& getName()        const { return displayName; }

private:
    void subscribe (ableton::ChannelId id)
    {
        source = std::make_unique<ableton::LinkAudioSource> (*linkPtr, id,
            [this] (ableton::LinkAudioSource::BufferHandle handle) { onBuffer (handle); });
    }

    // Message thread: if no buffers arrived since the last tick (source dead — peer gone / Link off)
    // and our channel is currently available again, re-subscribe so audio resumes.
    void timerCallback() override
    {
        const long now = arrivals.load (std::memory_order_relaxed);
        const bool idle = (now == lastArrivals);
        lastArrivals = now;
        if (! idle || linkPtr == nullptr) return;                 // still receiving, or no link
        for (const auto& c : linkPtr->channels())
            if (juce::String (c.name) == channelName && juce::String (c.peerName) == peerName)
            { subscribe (c.id); return; }                          // channel back -> resubscribe
    }

    // Link-managed thread: copy the received interleaved int16 PCM into the ring as float.
    void onBuffer (ableton::LinkAudioSource::BufferHandle handle)
    {
        const int nch = (int) handle.info.numChannels;
        const int nf  = (int) handle.info.numFrames;
        if (nf <= 0 || nch <= 0 || handle.samples == nullptr) return;
        recvRate.store ((double) handle.info.sampleRate, std::memory_order_relaxed);
        arrivals.fetch_add (1, std::memory_order_relaxed);        // watchdog liveness signal

        int s1, z1, s2, z2;
        const int room = juce::jmin (nf, fifo.getFreeSpace());
        fifo.prepareToWrite (room, s1, z1, s2, z2);
        auto put = [&] (int dst, int srcFrame)
        {
            const int16_t* s = handle.samples + (size_t) srcFrame * (size_t) nch;
            bufL[(size_t) dst] = (float) s[0] / 32768.0f;
            bufR[(size_t) dst] = (float) (nch > 1 ? s[1] : s[0]) / 32768.0f;
        };
        int f = 0;
        for (int k = 0; k < z1; ++k) put (s1 + k, f++);
        for (int k = 0; k < z2; ++k) put (s2 + k, f++);
        fifo.finishedWrite (z1 + z2);
    }

    // Audio thread: drain the ring into out, resampling by ratio = senderRate/deviceRate.
    void drain (float* outL, float* outR, int n, double ratio)
    {
        if (ratio <= 0.0) ratio = 1.0;

        // Priming: hold silence until a latency cushion has built up, so network jitter doesn't
        // starve the ring into choppy output.
        const int ready = fifo.getNumReady();
        if (! primed)
        {
            if (ready < kPrime) { for (int i = 0; i < n; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; } return; }
            primed = true;
        }

        const int scratch = (int) inL.size();
        int want = (int) (frac + (double) n * ratio) + 2;
        if (want > scratch) want = scratch;
        const int take = juce::jmin (want, fifo.getNumReady());
        int s1, z1, s2, z2;
        fifo.prepareToRead (take, s1, z1, s2, z2);
        for (int k = 0; k < z1; ++k) { inL[(size_t) k]        = bufL[(size_t) (s1 + k)]; inR[(size_t) k]        = bufR[(size_t) (s1 + k)]; }
        for (int k = 0; k < z2; ++k) { inL[(size_t) (z1 + k)] = bufL[(size_t) (s2 + k)]; inR[(size_t) (z1 + k)] = bufR[(size_t) (s2 + k)]; }

        double pos = frac;
        int produced = 0;
        for (; produced < n; ++produced)
        {
            const int i = (int) pos;
            if (i + 1 >= take) break;                  // underrun -> pad below
            const float fr = (float) (pos - i);
            outL[produced] = inL[(size_t) i] + (inL[(size_t) (i + 1)] - inL[(size_t) i]) * fr;
            outR[produced] = inR[(size_t) i] + (inR[(size_t) (i + 1)] - inR[(size_t) i]) * fr;
            pos += ratio;
        }
        int consumed = (int) pos;
        if (consumed > take) consumed = take;
        fifo.finishedRead (consumed);
        frac = pos - consumed;
        for (; produced < n; ++produced) { outL[produced] = 0.0f; outR[produced] = 0.0f; }

        // Envelope driven by BUFFER HEALTH, not by "did this block underrun": once the ring drops
        // below kLowWater there is still ~kLowWater frames of real audio left to fade over, so the
        // last audio ramps to silence smoothly (stream stop / Link off = clean fade, no pop). A
        // fade that only reacts to an empty block would be fading silence, leaving the pop. Fades
        // back up when the buffer recovers (join / momentary gap). ~21ms.
        const bool  active = ! activeFlag || activeFlag->load (std::memory_order_relaxed);
        const float target = (active && ready >= kLowWater) ? 1.0f : 0.0f;   // mute+fade when Link is off
        const float step   = 1.0f / (float) kFadeLen;
        for (int i = 0; i < n; ++i)
        {
            if      (env < target) { env += step; if (env > target) env = target; }
            else if (env > target) { env -= step; if (env < target) env = target; }
            outL[i] *= env; outR[i] *= env;
        }

        // Re-prime once the ring is fully drained (a real stop — Link off / stream ended). Without
        // this, when the stream resumes (re-link) arrival and consumption are balanced so the ring
        // stays near-empty, below kLowWater, and the envelope never re-opens → silence forever.
        // Safe from the old thrashing because the 256ms cushion keeps the ring far from 0 during
        // normal streaming; it only hits 0 when the source genuinely stops.
        if (fifo.getNumReady() == 0) { primed = false; frac = 0.0; }
    }

    juce::String displayName, channelName, peerName;
    std::unique_ptr<ableton::LinkAudioSource> source;

    juce::AbstractFifo fifo { kRing };
    std::array<float, (size_t) kRing>    bufL {}, bufR {};   // ring storage (producer: Link thread)
    std::array<float, (size_t) kScratch> inL  {}, inR  {};   // peek scratch (consumer: audio thread)
    static constexpr int kFadeLen = 1024;                    // ~21ms @48k silence<->audio ramp
    double frac { 0.0 };                                     // resampler phase (audio thread)
    bool   primed { false };                                 // has the jitter cushion filled? (audio thread)
    float  env { 0.0f };                                     // silence<->audio envelope (audio thread)
    std::atomic<double> recvRate { 48000.0 };                // sender rate, from the last buffer's Info

    std::vector<float> scratchL, scratchR;
    double deviceRate { 48000.0 };
    int    maxBlock   { 512 };

    ableton::LinkAudio* linkPtr { nullptr };   // for the watchdog re-subscribe (message thread)
    std::shared_ptr<std::atomic<bool>> activeFlag;   // Link on/off (mute+fade when off; SDK stays on)
    std::atomic<long>   arrivals { 0 };        // incremented per received buffer (liveness)
    long                lastArrivals { 0 };    // watchdog's previous-tick snapshot (message thread)
};
