// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <functional>
#include "Track.h"
#include "MixerTrack.h"
#include "Transport.h"

/** Receives OSC and turns it into real-time actions: live notes/CC into an
    instrument track's MidiMessageCollector, and lock-free param/transport
    changes. Structural changes are NOT done here (those go over gRPC in Phase 2).

    Handlers run on the OSC network thread (RealtimeCallback), so they only touch
    thread-safe things: the collector (internally locked), atomics, and a brief
    try-lock for effect params. */
class OscControl : private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
public:
    struct Hooks
    {
        std::function<Track* (int)>               resolveTrack;   // id -> Track* (stable in Phase 1)
        std::function<bool (int, const juce::String&, float)> setSynthParam;   // built-in synth tweak
        std::vector<std::unique_ptr<MixerTrack>>* mixerTracks = nullptr;
        juce::CriticalSection*                    engineLock = nullptr;
        Transport*                                transport = nullptr;
        std::function<void ()>                    panic;          // all-notes-off (clear stuck notes)
        std::function<void (const juce::String&)> log;            // optional
        // session view (clip-launch grid) — live triggers
        std::function<void (int, int)>            sessionLaunch;  // (track_id, scene)
        std::function<void (int)>                 sessionStop;    // (track_id) -> back to arrangement
        std::function<void (int)>                 sessionScene;   // (scene) -> launch the whole row
        std::function<void ()>                    sessionStopAll;
        std::function<void (double)>              sessionQuantum; // launch-quantize in beats (0 = immediate)
    };

    explicit OscControl (Hooks h) : hooks (std::move (h)) {}
    ~OscControl() override { stop(); }

    bool start (int port)
    {
        if (! receiver.connect (port))
            return false;
        receiver.addListener (this);
        running = true;
        return true;
    }

    void stop()
    {
        if (running) { receiver.removeListener (this); receiver.disconnect(); running = false; }
    }

private:
    void oscMessageReceived (const juce::OSCMessage& m) override { handle (m); }
    void oscBundleReceived  (const juce::OSCBundle& b) override
    {
        for (auto& e : b)
            if (e.isMessage()) handle (e.getMessage());
    }

    static float argF (const juce::OSCArgument& a)
    {
        if (a.isFloat32()) return a.getFloat32();
        if (a.isInt32())   return (float) a.getInt32();
        return 0.0f;
    }
    static int argI (const juce::OSCArgument& a)
    {
        if (a.isInt32())   return a.getInt32();
        if (a.isFloat32()) return (int) a.getFloat32();
        return 0;
    }

    void handle (const juce::OSCMessage& m)
    {
        const juce::String addr = m.getAddressPattern().toString();
        auto parts = juce::StringArray::fromTokens (addr, "/", "");
        parts.removeEmptyStrings();
        // parts: [gloopy, track, <id>, note] etc
        if (parts.size() < 2 || parts[0] != "gloopy")
            return;

        if (parts[1] == "transport")
            return handleTransport (parts, m);
        if (parts[1] == "track" && parts.size() >= 4)
            return handleTrack (parts[2].getIntValue(), parts[3], m);
        if (parts[1] == "insert" && parts.size() >= 7 && parts[3] == "fx" && parts[5] == "param")
            return handleEffectParam (parts[2].getIntValue(), parts[4].getIntValue(), parts[6], m);
        if (parts[1] == "insert" && parts.size() >= 4
              && (parts[3] == "vol" || parts[3] == "pan" || parts[3] == "mute"))
            return handleInsertParam (parts[2].getIntValue(), parts[3], m);
        if (parts[1] == "session")
            return handleSession (parts, m);
    }

    // /gloopy/session/launch <track_id> <scene> | /session/stop <track_id> | /session/scene <scene> | /session/stopall
    void handleSession (const juce::StringArray& p, const juce::OSCMessage& m)
    {
        if (p.size() < 3) return;
        const auto& cmd = p[2];
        if      (cmd == "launch"  && m.size() >= 2 && hooks.sessionLaunch)  hooks.sessionLaunch (argI (m[0]), argI (m[1]));
        else if (cmd == "stop"    && m.size() >= 1 && hooks.sessionStop)    hooks.sessionStop (argI (m[0]));
        else if (cmd == "scene"   && m.size() >= 1 && hooks.sessionScene)   hooks.sessionScene (argI (m[0]));
        else if (cmd == "stopall"                  && hooks.sessionStopAll) hooks.sessionStopAll();
        else if (cmd == "quantum" && m.size() >= 1 && hooks.sessionQuantum) hooks.sessionQuantum (argF (m[0]));
        if (hooks.log) hooks.log ("session " + cmd);
    }

    void handleTransport (const juce::StringArray& p, const juce::OSCMessage& m)
    {
        auto* t = hooks.transport;
        if (t == nullptr || p.size() < 3) return;
        const auto& cmd = p[2];
        if      (cmd == "play")  t->setPlaying (true);
        else if (cmd == "stop")  { t->setPlaying (false); t->requestReset(); }
        else if (cmd == "tempo" && m.size() >= 1) t->setBpm (argF (m[0]));
        else if (cmd == "seek"  && m.size() >= 1) t->requestSeek (argF (m[0]));
        else if (cmd == "panic" && hooks.panic)   hooks.panic();
        if (hooks.log) hooks.log ("transport " + cmd);
    }

    void handleTrack (int id, const juce::String& cmd, const juce::OSCMessage& m)
    {
        Track* t = hooks.resolveTrack ? hooks.resolveTrack (id) : nullptr;
        if (t == nullptr) return;

        if (cmd == "note" && m.size() >= 2)
        {
            const int pitch = juce::jlimit (0, 127, argI (m[0]));   // clamp untrusted OSC to a valid note
            const int vel   = argI (m[1]);
            const auto msg = vel > 0 ? juce::MidiMessage::noteOn  (1, pitch, (juce::uint8) juce::jlimit (1, 127, vel))
                                     : juce::MidiMessage::noteOff (1, pitch);
            t->liveMidi.addMessageToQueue (msg);
            if (hooks.log) hooks.log ("note track=" + juce::String (id) + " pitch=" + juce::String (pitch)
                                        + " vel=" + juce::String (vel));
        }
        else if (cmd == "cc" && m.size() >= 2)
        {
            t->liveMidi.addMessageToQueue (juce::MidiMessage::controllerEvent (
                1, juce::jlimit (0, 127, argI (m[0])), juce::jlimit (0, 127, (int) (argF (m[1]) * 127.0f))));
        }
        else if (cmd == "vol"  && m.size() >= 1) t->volume.store (juce::jlimit (0.0f, 1.0f, argF (m[0])));
        else if (cmd == "pan"  && m.size() >= 1) t->pan.store (juce::jlimit (-1.0f, 1.0f, argF (m[0])));
        else if (cmd == "mute" && m.size() >= 1) t->mute.store (argI (m[0]) != 0);
        // /gloopy/track/<id>/synth <paramName> <value> — live built-in synth tweak
        else if (cmd == "synth" && m.size() >= 2 && m[0].isString() && hooks.setSynthParam)
            hooks.setSynthParam (id, m[0].getString(), argF (m[1]));
    }

    void handleInsertParam (int insert, const juce::String& cmd, const juce::OSCMessage& m)
    {
        if (hooks.mixerTracks == nullptr || hooks.engineLock == nullptr || m.size() < 1)
            return;
        const juce::ScopedTryLock stl (*hooks.engineLock);
        if (! stl.isLocked())
            return;
        auto& mt = *hooks.mixerTracks;
        if (! juce::isPositiveAndBelow (insert, (int) mt.size())) return;
        auto& x = *mt[(size_t) insert];
        if      (cmd == "vol")  x.volume.store (juce::jlimit (0.0f, 1.0f, argF (m[0])));
        else if (cmd == "pan")  x.pan.store (juce::jlimit (-1.0f, 1.0f, argF (m[0])));
        else if (cmd == "mute") x.mute.store (argI (m[0]) != 0);
    }

    void handleEffectParam (int insert, int fxSlot, const juce::String& name, const juce::OSCMessage& m)
    {
        if (hooks.mixerTracks == nullptr || hooks.engineLock == nullptr || m.size() < 1)
            return;
        const float value = argF (m[0]);

        const juce::ScopedTryLock stl (*hooks.engineLock);
        if (! stl.isLocked())
            return;   // audio is mid-block; drop this knob update

        auto& mt = *hooks.mixerTracks;
        if (! juce::isPositiveAndBelow (insert, (int) mt.size())) return;
        auto& fx = mt[(size_t) insert]->effects;
        if (! juce::isPositiveAndBelow (fxSlot, (int) fx.size())) return;

        for (auto& p : fx[(size_t) fxSlot]->parameters())
            if (p.name.equalsIgnoreCase (name)) { p.set (value); break; }
    }

    Hooks hooks;
    juce::OSCReceiver receiver;
    bool running { false };
};
