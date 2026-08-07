// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include "Generator.h"
#include "JackCapture.h"
#include "X11Raise.h"

/** Slices 1-3 (prototype) of the external-instrument / session-host epic (ideas.md).

    Launches a STANDALONE application (e.g. ZynAddSubFX) as its own OS process so its real
    native GUI appears, and owns its lifecycle: the process starts when the track is created
    and is killed when the generator is destroyed (track removed / project closed).

    Slice 1: launch + native GUI + lifecycle.
    Slice 2: route the track's MIDI (live + sequenced) to the app so you can PLAY it — when the
             app registers its ALSA MIDI input it appears as a juce::MidiOutput destination.
    Slice 3 (here): route the app's AUDIO back INTO the mix. When launched with a JACK/PipeWire
             output (e.g. `zynaddsubfx -O jack`) the app registers JACK output ports; we claim a
             stereo capture slot on the shared JackCapture client, auto-connect the app's new
             output ports to it, and render() drains the lock-free ring into the track buffer so
             the sound flows through this track's inserts + fader like any other generator.
             (If JACK isn't available, or the app has no JACK output, the track is simply silent
             and you still hear the app through its own output — slices 1-2 unaffected.)
    NSM state save/restore is slice 4; persistence is slice 5.

    Thread note: the process, MIDI port and JACK connections are started/opened on the message
    thread (ctor + a message-thread Timer that waits for the app's ports to appear); they are torn
    down on the dtor, which the engine performs under engineLock so the audio thread isn't in
    render() then. render() runs on the audio thread and only touches atomics + the lock-free ring. */
class ExternalInstrument : public Generator,
                           private juce::Timer
{
public:
    ExternalInstrument (juce::StringArray argv, juce::String display, std::shared_ptr<JackCapture> jackCapture)
        : command (std::move (argv)), displayName (std::move (display)), jack (std::move (jackCapture))
    {
        for (auto& d : juce::MidiOutput::getAvailableDevices())   // snapshot ports that exist BEFORE launch
            preLaunchOuts.add (d.identifier);
        if (jack && jack->isAvailable())
        {
            preLaunchJackOuts = jack->outputPorts();               // snapshot JACK outputs before launch
            slot = jack->claim();
        }
        preLaunchClients = gloopyx::listClients();                 // snapshot WM windows before launch (for auto-raise)
        launch();
        startTimer (250);   // poll for the app's MIDI-in + JACK-out ports to appear, then connect
    }

    ~ExternalInstrument() override
    {
        stopTimer();
        audioConnected.store (false);
        midiOut.store (nullptr);
        out.reset();                       // close the MIDI connection
        if (jack && slot >= 0) jack->release (slot);
        stop();                            // kill the process
    }

    void launch()
    {
        if (command.isEmpty()) return;
        proc = std::make_unique<juce::ChildProcess>();
        if (proc->start (command))
            std::cout << "[external] launched: " << command.joinIntoString (" ") << std::endl;
        else
        {
            std::cout << "[external] FAILED to launch: " << command[0] << std::endl;
            proc.reset();
        }
    }

    void stop()
    {
        if (proc != nullptr) { proc->kill(); proc.reset(); }   // message thread only
    }

    bool isRunning() const { return proc != nullptr && proc->isRunning(); }

    // --- Generator ------------------------------------------------------------
    void prepare (double sr, int block) override
    {
        sampleRate = sr; maxBlock = block;
        scratchL.assign ((size_t) block, 0.0f);
        scratchR.assign ((size_t) block, 0.0f);
        // Ratio to convert the JACK/PipeWire graph rate (what the ring is filled at) to our device
        // rate. 1.0 when they match; ~1.088 for a 48k graph feeding a 44.1k device.
        resampleRatio = (jack && jack->isAvailable() && sr > 0.0) ? jack->jackSampleRate() / sr : 1.0;
    }

    // Forward the track's MIDI to the app (slice 2) and pull the app's audio into the track
    // buffer (slice 3). render() is on the audio thread; sendMessageNow does an ALSA write here —
    // acceptable for the prototype (a production impl would use a dedicated sender thread). The
    // JACK read is lock-free.
    void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int startSample, int numSamples) override
    {
        if (auto* o = midiOut.load (std::memory_order_relaxed))
            for (const auto meta : midi)
                o->sendMessageNow (meta.getMessage());

        if (jack && slot >= 0 && audioConnected.load (std::memory_order_relaxed)
            && numSamples <= (int) scratchL.size())
        {
            jack->read (slot, scratchL.data(), scratchR.data(), numSamples, resampleRatio);
            buffer.addFrom (0, startSample, scratchL.data(), numSamples);
            if (buffer.getNumChannels() > 1)
                buffer.addFrom (1, startSample, scratchR.data(), numSamples);
        }
    }

    void allNotesOff() override
    {
        if (auto* o = midiOut.load (std::memory_order_relaxed))
            for (int ch = 1; ch <= 16; ++ch)
                o->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
    }

    juce::String typeName() const override { return "External"; }

    const juce::StringArray& getCommand() const { return command; }
    const juce::String&      getName()    const { return displayName; }

private:
    // Message thread: once the launched app registers its ports they appear as NEW entries that
    // weren't there before launch — a NEW MidiOutput destination (its MIDI in) and NEW JACK output
    // ports (its audio out). Connect each the first time we see it; stop once both are done (or we
    // give up after ~10s).
    void timerCallback() override
    {
        const bool midiDone  = midiOut.load() != nullptr;
        const bool audioDone = audioConnected.load() || slot < 0 || ! (jack && jack->isAvailable());

        if (! midiDone)
        {
            for (auto& d : juce::MidiOutput::getAvailableDevices())
            {
                if (preLaunchOuts.contains (d.identifier)) continue;   // pre-existing, not the app's
                if (auto o = juce::MidiOutput::openDevice (d.identifier))
                {
                    std::cout << "[external] MIDI -> " << d.name << std::endl;
                    out = std::move (o);
                    midiOut.store (out.get());
                    break;
                }
            }
        }

        if (! audioDone)
        {
            juce::StringArray fresh;                                   // the app's new JACK output ports
            for (auto& p : jack->outputPorts())
                if (! preLaunchJackOuts.contains (p) && ! p.startsWith ("Gloopy:"))
                    fresh.add (p);
            if (fresh.size() >= 1)
            {
                jack->connect (slot, fresh);
                std::cout << "[external] audio <- " << fresh.joinIntoString (", ") << std::endl;
                audioConnected.store (true);
            }
        }

        // Once the app's window appears as a NEW managed client, raise it to the front — under
        // XWayland it otherwise opens hidden behind gloopy. Do it once (don't keep stealing focus).
        if (! guiRaised && proc != nullptr)
        {
            int raised = 0;
            for (auto w : gloopyx::listClients())
            {
                if (std::find (preLaunchClients.begin(), preLaunchClients.end(), w) != preLaunchClients.end()) continue;
                gloopyx::activateWindow (w);
                ++raised;
            }
            if (raised > 0) { std::cout << "[external] raised GUI window" << (raised > 1 ? "s" : "") << std::endl; guiRaised = true; }
        }

        const bool midiSettled  = midiOut.load() != nullptr || tries > 40;
        const bool audioSettled = audioConnected.load() || slot < 0 || ! (jack && jack->isAvailable()) || tries > 40;
        const bool guiSettled   = guiRaised || proc == nullptr || tries > 40;
        if (midiSettled && audioSettled && guiSettled)
            stopTimer();
        ++tries;
    }

    juce::StringArray command;
    juce::String      displayName;
    std::shared_ptr<JackCapture>        jack;
    int                                 slot { -1 };
    std::unique_ptr<juce::ChildProcess> proc;
    std::unique_ptr<juce::MidiOutput>   out;               // owns the MIDI connection
    std::atomic<juce::MidiOutput*>      midiOut { nullptr }; // audio-thread-visible handle
    std::atomic<bool>                   audioConnected { false };
    juce::StringArray                   preLaunchOuts, preLaunchJackOuts;
    std::vector<unsigned long>          preLaunchClients;   // WM windows present before launch (auto-raise)
    bool                                guiRaised { false };
    std::vector<float>                  scratchL, scratchR;
    double resampleRatio { 1.0 };
    int    tries { 0 };
    double sampleRate { 44100.0 };
    int    maxBlock   { 512 };
};
