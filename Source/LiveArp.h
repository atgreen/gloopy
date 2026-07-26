// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include "NoteEdits.h"   // arpSequence

/** Real-time arpeggiator for live MIDI input — hold a chord and it plays the notes as a
    stepped pattern, tracking the held set as it changes, exactly like Ableton's Arpeggiator /
    a hardware arp. Runs on the audio thread: incoming note-on/offs update the held set, and a
    beat-clock (advanced by the rendered samples, so it ticks whether or not the transport is
    playing) fires one note per `rate` beats. Non-note messages pass straight through.

    - New chord → the pattern restarts from its first note (root), matching arpSequence / the
      clip arp.
    - `hold` (latch): releasing the keys keeps the last chord arpeggiating; the next fresh press
      (after everything is up) replaces it.
    - `gate` (0..1) is the note length as a fraction of the step.

    Message-rate allocations only (the per-step sequence is cached and rebuilt when the set /
    octaves / mode change); the per-step firing is allocation-free. Deterministic except for
    `mode == 3` (random), which is fine — this only runs on live input, never on offline render. */
class LiveArp
{
public:
    /** Process one render block. Reads note events from @p in, appends arp + pass-through
        events to @p out, keeping internal state across blocks. @p spb = samples per beat. */
    void process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples, double spb,
                  double rate, int octaves, float gate, int mode, bool hold)
    {
        if (spb <= 0.0 || rate <= 0.0) { out.addEvents (in, 0, numSamples, 0); return; }

        // 1) Apply this block's note events to the held set (pass non-note messages through).
        for (const auto meta : in)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
            {
                if (down.empty()) active.clear();                  // fresh chord after silence -> replace
                addUnique (down, m.getNoteNumber());
                addUnique (active, m.getNoteNumber());
                vel = m.getFloatVelocity();
            }
            else if (m.isNoteOff())
            {
                erase (down, m.getNoteNumber());
                if (! hold) erase (active, m.getNoteNumber());     // hold latches the released notes
            }
            else
            {
                out.addEvent (m, meta.samplePosition);             // CC / pitchbend / etc. pass through
            }
        }

        // 2) A changed set restarts the pattern from its first note and fires promptly.
        if (active != heldForSeq)
        {
            heldForSeq = active;
            stepIndex  = 0;
            toNextStep = 0.0;                                      // fire on the next step check below
            rebuildSeq (octaves, mode);
        }
        else if (octaves != seqOct || mode != seqMode)
        {
            rebuildSeq (octaves, mode);
        }

        // 3) Nothing held (and not latched): release any sounding note and idle.
        if (active.empty())
        {
            if (sounding >= 0) { out.addEvent (juce::MidiMessage::noteOff (1, sounding), 0); sounding = -1; }
            toGateOff = 1e18;
            return;
        }

        // 4) Walk the block, firing gate-offs and step note-ons at their sample offsets. Events
        //    landing at exactly the block boundary are carried to the next block (the >= remaining
        //    guard), so timing doesn't double-fire and stays stable across block sizes.
        const double blockBeats = (double) numSamples / spb;
        const float  g          = juce::jlimit (0.05f, 1.0f, gate);
        constexpr double eps    = 1e-9;
        double posBeats  = 0.0;                                    // position within this block, beats
        double remaining = blockBeats;

        for (int guard = 0; guard < 4096; ++guard)
        {
            const double dtStep = juce::jmax (0.0, toNextStep);
            const double dtGate = (sounding >= 0) ? juce::jmax (0.0, toGateOff) : 1e18;
            const double dt     = juce::jmin (dtStep, dtGate);
            if (dt >= remaining - eps)                             // next event is past this block
            {
                toNextStep -= remaining; if (sounding >= 0) toGateOff -= remaining;
                break;
            }
            posBeats += dt; remaining -= dt;
            toNextStep -= dt; if (sounding >= 0) toGateOff -= dt;
            const int off = juce::jlimit (0, juce::jmax (0, numSamples - 1), (int) std::llround (posBeats * spb));

            if (sounding >= 0 && toGateOff <= eps)                 // gate: end the sounding note
            {
                out.addEvent (juce::MidiMessage::noteOff (1, sounding), off);
                sounding = -1; toGateOff = 1e18;
            }
            if (toNextStep <= eps)                                 // step: play the next note
            {
                if (sounding >= 0) { out.addEvent (juce::MidiMessage::noteOff (1, sounding), off); sounding = -1; }
                if (! seq.empty())
                {
                    const int pitch = (mode == 3) ? seq[(size_t) rng.nextInt ((int) seq.size())]
                                                  : seq[(size_t) (stepIndex % (int) seq.size())];
                    out.addEvent (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) juce::jlimit (1, 127, (int) (vel * 127.0f))), off);
                    sounding  = pitch;
                    toGateOff = (double) g * rate;
                    ++stepIndex;
                }
                toNextStep += rate;
            }
        }
    }

    /** Silence + forget everything (arp toggled off, panic, transport reset). The caller is
        expected to also all-notes-off the generator, so we don't emit here. */
    void reset()
    {
        down.clear(); active.clear(); heldForSeq.clear(); seq.clear();
        sounding = -1; stepIndex = 0; toNextStep = 0.0; toGateOff = 1e18;
        seqOct = -1; seqMode = -1;
    }

private:
    static void addUnique (std::vector<int>& v, int p)
    {
        auto it = std::lower_bound (v.begin(), v.end(), p);
        if (it == v.end() || *it != p) v.insert (it, p);          // keep sorted + unique
    }
    static void erase (std::vector<int>& v, int p)
    {
        auto it = std::lower_bound (v.begin(), v.end(), p);
        if (it != v.end() && *it == p) v.erase (it);
    }
    void rebuildSeq (int octaves, int mode)
    {
        seq = arpSequence (heldForSeq, octaves, mode);
        seqOct = octaves; seqMode = mode;
    }

    std::vector<int> down;        // physically-held keys (sorted, unique)
    std::vector<int> active;      // set the arp plays (latches past release when hold is on)
    std::vector<int> heldForSeq;  // set the cached seq was built from
    std::vector<int> seq;         // cached ordered pattern (rebuilt on set/octaves/mode change)
    int    seqOct   { -1 };
    int    seqMode  { -1 };
    float  vel      { 0.8f };
    int    sounding { -1 };       // pitch of the currently-playing arp note (ch 1), -1 = none
    int    stepIndex { 0 };
    double toNextStep { 0.0 };    // beats until the next step fires
    double toGateOff  { 1e18 };   // beats until the sounding note's gate-off
    juce::Random rng { 20260726 };// live-only; not on the deterministic render path
};
