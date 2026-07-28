// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Headless unit tests for Gloopy's pure logic. These deliberately avoid the
// GUI / audio-device machinery so they run fast in CI. They cover the pieces
// that have bitten us before: note scheduling / swing, and the ValueTree
// serialisation that save/load round-trips through.

#include <JuceHeader.h>
#include "Note.h"
#include "NoteScheduler.h"
#include "NoteEdits.h"
#include "LiveArp.h"
#include "SessionModel.h"
#include "SessionLauncher.h"
#include "NotesJson.h"
#include "AllpassPhaser.h"
#include "Biquad.h"
#include "EffectSync.h"
#include "Onsets.h"
#include "ParamScale.h"
#include "FileDrop.h"
#include "BrowserDrag.h"
#include "ParamId.h"
#include "StereoWiden.h"
#include "Lfo.h"
#include "FadeShape.h"
#include "Toml.h"
#include "Time.h"
#include <type_traits>

//==============================================================================
struct NoteSchedulerTests : juce::UnitTest
{
    NoteSchedulerTests() : juce::UnitTest ("NoteScheduler") {}

    void runTest() override
    {
        beginTest ("swingBeat straight = identity");
        {
            for (double b : { 0.0, 0.25, 0.5, 0.75, 1.0, 3.5 })
                expectWithinAbsoluteError (swingBeat (b, 0.5), b, 1e-12);
        }

        beginTest ("swingBeat keeps downbeats fixed and delays off-eighths");
        {
            // Integer beats (downbeats) never move, regardless of swing amount.
            for (double s : { 0.5, 0.6, 0.66, 0.75 })
            {
                expectWithinAbsoluteError (swingBeat (0.0, s), 0.0, 1e-12);
                expectWithinAbsoluteError (swingBeat (1.0, s), 1.0, 1e-12);
                expectWithinAbsoluteError (swingBeat (2.0, s), 2.0, 1e-12);
            }
            // The off-eighth (0.5) is pushed later by exactly the swing ratio.
            expectWithinAbsoluteError (swingBeat (0.5, 0.66), 0.66, 1e-9);
            expect (swingBeat (0.5, 0.66) > 0.5);
        }

        beginTest ("swingBeat is monotonic within a beat");
        {
            const double s = 0.66;
            double prev = -1.0;
            for (int i = 0; i <= 100; ++i)
            {
                const double b = i / 100.0;               // 0..1
                const double sw = swingBeat (b, s);
                expect (sw >= prev, "swing must not go backwards");
                prev = sw;
            }
        }

        // Constant-tempo converter: an empty tempo map -> the byte-identical spb path.
        auto constConv = [] (double spb) { TempoConv c; c.setMarkers (nullptr, nullptr, 0, 44100.0, spb); return c; };

        beginTest ("collectNotes places note-on/off in the right block");
        {
            const TempoConv tc = constConv (100.0);       // 100 samples per beat
            std::vector<Note> notes { { 60, 1.0, 2.0, 0.8f } };  // on@beat1 off@beat3

            juce::MidiBuffer midi;
            // Block covering samples [0, 400): should contain both on(100) and off(300).
            collectNotes (notes, midi, tc, 0.0, 0, 0, 0, 400);

            int ons = 0, offs = 0, onPos = -1, offPos = -1;
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn())  { ++ons;  onPos  = meta.samplePosition; }
                if (m.isNoteOff()) { ++offs; offPos = meta.samplePosition; }
            }
            expectEquals (ons, 1);
            expectEquals (offs, 1);
            expectEquals (onPos, 100);
            expectEquals (offPos, 300);
        }

        beginTest ("collectNotes windowing excludes out-of-range events");
        {
            const TempoConv tc = constConv (100.0);
            std::vector<Note> notes { { 60, 1.0, 2.0, 0.8f } };  // on@100 off@300

            juce::MidiBuffer midi;
            // Block [0,200): should have the note-on only.
            collectNotes (notes, midi, tc, 0.0, 0, 0, 0, 200);
            int ons = 0, offs = 0;
            for (const auto meta : midi)
            {
                if (meta.getMessage().isNoteOn())  ++ons;
                if (meta.getMessage().isNoteOff()) ++offs;
            }
            expectEquals (ons, 1);
            expectEquals (offs, 0);
        }

        beginTest ("collectNotes swing delays the off-beat note-on");
        {
            const TempoConv tc = constConv (100.0);
            std::vector<Note> notes { { 60, 0.5, 0.5, 0.8f } };  // off-eighth note

            juce::MidiBuffer straight, swung;
            collectNotes (notes, straight, tc, 0.0, 0, 0, 0, 400, 0.5);
            collectNotes (notes, swung,    tc, 0.0, 0, 0, 0, 400, 0.66);

            auto firstOn = [] (juce::MidiBuffer& b) {
                for (const auto meta : b)
                    if (meta.getMessage().isNoteOn()) return meta.samplePosition;
                return -1;
            };
            const int sPos = firstOn (straight);
            const int wPos = firstOn (swung);
            expectEquals (sPos, 50);                     // 0.5 beat * 100 spb
            expect (wPos > sPos, "swing should push the off-eighth later");
            expectEquals (wPos, 66);
        }

        beginTest ("TempoConv integrates a mid-song tempo change");
        {
            // 120 bpm from beat 0, doubling to 240 bpm at beat 2. At sampleRate 100:
            // spb=50 below beat 2, spb=25 above -> beat N maps to fewer samples after 2.
            const double beats[] = { 0.0, 2.0 };
            const double bpms[]  = { 120.0, 240.0 };
            TempoConv tc; tc.setMarkers (beats, bpms, 2, /*rate*/ 100.0, /*fallback*/ 0.0);

            expectEquals (tc.beatToSample (1.0), (juce::int64) 50);    // 1 beat @120 = 0.5s
            expectEquals (tc.beatToSample (2.0), (juce::int64) 100);   // 2 beats @120 = 1.0s
            expectEquals (tc.beatToSample (3.0), (juce::int64) 125);   // +1 beat @240 = +0.25s
            expectEquals (tc.beatToSample (4.0), (juce::int64) 150);   // +2 beats @240 = +0.5s

            // The speed-up shortens the render: 4 beats span 150 samples, not the 200
            // a constant 120 bpm (spb 50) would give.
            expect (tc.beatToSample (4.0) < (juce::int64) 200, "tempo speed-up shortens the render");

            // Inverse round-trips at the boundary and past it.
            expectWithinAbsoluteError (tc.sampleToBeat (100), 2.0, 1e-9);
            expectWithinAbsoluteError (tc.sampleToBeat (125), 3.0, 1e-9);

            // collectNotes honours it: a note on@beat2 lands at 100, off@beat3 at 125.
            std::vector<Note> vnotes { { 67, 2.0, 1.0, 0.7f } };
            juce::MidiBuffer vm;
            collectNotes (vnotes, vm, tc, 0.0, 0, 0, 0, 400);
            int vonPos = -1, voffPos = -1;
            for (const auto meta : vm)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn())  vonPos  = meta.samplePosition;
                if (m.isNoteOff()) voffPos = meta.samplePosition;
            }
            expectEquals (vonPos, 100);
            expectEquals (voffPos, 125);
        }
    }
};

//==============================================================================
struct SerializationTests : juce::UnitTest
{
    SerializationTests() : juce::UnitTest ("Serialization") {}

    void runTest() override
    {
        beginTest ("ValueTree survives XML round-trip (save/load path)");
        {
            juce::ValueTree root ("GLOOPY");
            root.setProperty ("bpm", 128.0, nullptr);
            root.setProperty ("swing", 0.66, nullptr);

            juce::ValueTree track ("TRACK");
            track.setProperty ("name", "Strings", nullptr);
            track.setProperty ("id", 3, nullptr);
            root.appendChild (track, nullptr);

            // This is exactly what SaveProject/LoadProject rely on.
            const auto xml = root.toXmlString();
            std::unique_ptr<juce::XmlElement> parsed (juce::XmlDocument::parse (xml));
            expect (parsed != nullptr);
            const auto back = juce::ValueTree::fromXml (*parsed);

            expect (back.isValid());
            expectWithinAbsoluteError ((double) back.getProperty ("bpm"),   128.0, 1e-12);
            expectWithinAbsoluteError ((double) back.getProperty ("swing"), 0.66,  1e-12);
            expectEquals (back.getNumChildren(), 1);
            expectEquals (back.getChild (0).getProperty ("name").toString(), juce::String ("Strings"));
            expectEquals ((int) back.getChild (0).getProperty ("id"), 3);
        }
    }
};

//==============================================================================
struct TomlTests : juce::UnitTest
{
    TomlTests() : juce::UnitTest ("Toml") {}

    void runTest() override
    {
        using namespace gloopy;

        beginTest ("writer/parser round-trip of scalars, tables, arrays-of-tables");
        {
            toml::Writer w;
            w.str ("title", "Nocturne \"Sketch\"").number ("bpm", 76.5).boolean ("looped", true)
             .integer ("count", 3).strArray ("roots", { "assets/samples", "~/sfz" }).blank();
            w.table ("generator").str ("type", "sfz").str ("path", "a/b c.sfz").blank();
            w.arrayItem ("clips").str ("id", "intro").number ("start", 0.0);
            w.arrayItem ("clips").str ("id", "verse").number ("start", 16.0);

            const auto doc = toml::parse (w.str());
            expectEquals (doc.root.getString ("title"), juce::String ("Nocturne \"Sketch\""));
            expectWithinAbsoluteError (doc.root.getDouble ("bpm"), 76.5, 1e-9);
            expect (doc.root.getBool ("looped"));
            expectEquals (doc.root.getInt ("count"), 3);
            expectEquals (doc.root.getStringArray ("roots").size(), 2);
            expectEquals (doc.root.getStringArray ("roots")[1], juce::String ("~/sfz"));

            auto* g = doc.table ("generator");
            expect (g != nullptr);
            expectEquals (g->getString ("path"), juce::String ("a/b c.sfz"));

            auto* clips = doc.array ("clips");
            expect (clips != nullptr);
            expectEquals ((int) clips->size(), 2);
            expectEquals ((*clips)[1].getString ("id"), juce::String ("verse"));
            expectWithinAbsoluteError ((*clips)[1].getDouble ("start"), 16.0, 1e-9);
        }

        beginTest ("tolerates comments, blank lines, quoted keys with spaces");
        {
            const juce::String src =
                "# a comment\n\n"
                "type = \"Limiter\"   # inline comment\n"
                "\"Thresh dB\" = -1.0\n"
                "bypassed = false\n";
            const auto doc = toml::parse (src);
            expectEquals (doc.root.getString ("type"), juce::String ("Limiter"));
            expectWithinAbsoluteError (doc.root.getDouble ("Thresh dB"), -1.0, 1e-9);
            expect (! doc.root.getBool ("bypassed"));
        }

        beginTest ("%.17g preserves a float-promoted double exactly");
        {
            const double v = (double) 0.8f;   // the classic 0.800000011920929
            const auto doc = toml::parse ("v = " + toml::Writer::num (v) + "\n");
            expect (doc.root.getDouble ("v") == v);
        }
    }
};

//==============================================================================
struct NoteEditTests : juce::UnitTest
{
    NoteEditTests() : juce::UnitTest ("NoteEdits") {}

    void runTest() override
    {
        beginTest ("strum staggers a chord's starts");
        {
            std::vector<Note> ns { {60,0,1,0.8f}, {64,0,1,0.8f}, {67,0,1,0.8f} };
            strumNotes (ns, 0.1, true);                       // down = high->low
            std::sort (ns.begin(), ns.end(), [] (auto& a, auto& b) { return a.pitch > b.pitch; });
            expectWithinAbsoluteError (ns[0].startBeat, 0.0, 1e-9);
            expectWithinAbsoluteError (ns[1].startBeat, 0.1, 1e-9);
            expectWithinAbsoluteError (ns[2].startBeat, 0.2, 1e-9);
        }

        beginTest ("onset detection finds transient starts");
        {
            // Four 4000-sample "hits" (a loud burst then silence) at 0/4000/8000/12000.
            const int seg = 4000, n = seg * 4;
            std::vector<float> x ((size_t) n, 0.0f);
            for (int h = 0; h < 4; ++h)
                for (int i = 0; i < 800; ++i)                       // 800-sample burst at each hit start
                    x[(size_t) (h * seg + i)] = (i % 2 ? 0.9f : -0.9f);   // full-scale buzz
            auto on = detectOnsets (x.data(), n, 44100.0, 1.0f);
            // Interior onsets only (the hit at 0 is never returned): expect ~3, near the cuts.
            expect ((int) on.size() == 3, juce::String ("onsets=") + juce::String ((int) on.size()));
            auto near = [] (int v, int want) { return std::abs (v - want) < 600; };
            if (on.size() == 3)
                expect (near (on[0], 4000) && near (on[1], 8000) && near (on[2], 12000));
            // Silence yields no onsets.
            std::vector<float> z ((size_t) n, 0.0f);
            expect (detectOnsets (z.data(), n, 44100.0, 1.0f).empty());
        }

        beginTest ("knife splits notes crossing a beat, leaves others");
        {
            std::vector<Note> ns { {60,0,2,0.8f},      // spans beat 1 -> splits into [0,1)+[1,2)
                                   {62,1,1,0.7f},      // starts AT the cut -> untouched
                                   {64,3,1,0.6f} };    // after the cut -> untouched
            splitNotesAtBeat (ns, 1.0);
            expect ((int) ns.size() == 4);
            std::sort (ns.begin(), ns.end(), [] (auto& a, auto& b)
                       { return a.pitch != b.pitch ? a.pitch < b.pitch : a.startBeat < b.startBeat; });
            // pitch 60 -> two halves
            expectWithinAbsoluteError (ns[0].startBeat, 0.0, 1e-9);
            expectWithinAbsoluteError (ns[0].lengthBeats, 1.0, 1e-9);
            expectWithinAbsoluteError (ns[1].startBeat, 1.0, 1e-9);
            expectWithinAbsoluteError (ns[1].lengthBeats, 1.0, 1e-9);
            expect (ns[0].velocity == 0.8f && ns[1].velocity == 0.8f);   // velocity preserved
            // the boundary note (62@1) and the after note (64@3) are untouched
            expect (ns[2].pitch == 62 && ns[3].pitch == 64);
            expectWithinAbsoluteError (ns[2].lengthBeats, 1.0, 1e-9);
        }

        beginTest ("legato stretches each note to the next onset; last unchanged");
        {
            std::vector<Note> ns { {60,0,1,0.8f}, {62,2,1,0.7f}, {64,4,1,0.6f} };
            legatoNotes (ns, 1.0f);
            expectWithinAbsoluteError (ns[0].lengthBeats, 2.0, 1e-9);   // 0->2
            expectWithinAbsoluteError (ns[1].lengthBeats, 2.0, 1e-9);   // 2->4
            expectWithinAbsoluteError (ns[2].lengthBeats, 1.0, 1e-9);   // last: unchanged
            expect (ns.size() == 3 && ns[0].pitch == 60 && ns[2].pitch == 64);   // size + order preserved
        }
        beginTest ("legato amount blends length; chords extend together");
        {
            std::vector<Note> ns { {60,0,1,0.8f}, {62,2,1,0.7f} };
            legatoNotes (ns, 0.5f);                                     // halfway from 1 -> 2
            expectWithinAbsoluteError (ns[0].lengthBeats, 1.5, 1e-9);
            // a two-note chord at 0, next onset at 2 -> both extend to length 2
            std::vector<Note> ch { {60,0,1,0.8f}, {64,0,1,0.8f}, {67,2,1,0.7f} };
            legatoNotes (ch, 1.0f);
            expectWithinAbsoluteError (ch[0].lengthBeats, 2.0, 1e-9);
            expectWithinAbsoluteError (ch[1].lengthBeats, 2.0, 1e-9);
            expectWithinAbsoluteError (ch[2].lengthBeats, 1.0, 1e-9);   // last onset: unchanged
        }

        beginTest ("velocity ramp crescendos linearly by onset; last unchanged position gets `to`");
        {
            std::vector<Note> ns { {60,0,1,0.5f}, {62,2,1,0.5f}, {64,4,1,0.5f} };
            rampVelocities (ns, 0.2f, 1.0f);                 // onsets 0/2/4 -> t 0/0.5/1
            expectWithinAbsoluteError (ns[0].velocity, 0.2f, 1e-5f);
            expectWithinAbsoluteError (ns[1].velocity, 0.6f, 1e-5f);
            expectWithinAbsoluteError (ns[2].velocity, 1.0f, 1e-5f);
            expect (ns.size() == 3 && ns[0].pitch == 60 && ns[2].pitch == 64);   // size + order preserved
        }
        beginTest ("velocity ramp decrescendos; chord shares velocity; single onset gets `to`");
        {
            std::vector<Note> ns { {60,0,1,0.9f}, {62,4,1,0.9f} };
            rampVelocities (ns, 1.0f, 0.2f);                 // decrescendo
            expectWithinAbsoluteError (ns[0].velocity, 1.0f, 1e-5f);
            expectWithinAbsoluteError (ns[1].velocity, 0.2f, 1e-5f);
            std::vector<Note> ch { {60,0,1,0.5f}, {64,0,1,0.5f} };   // one onset
            rampVelocities (ch, 0.3f, 0.8f);
            expectWithinAbsoluteError (ch[0].velocity, 0.8f, 1e-5f);
            expectWithinAbsoluteError (ch[1].velocity, 0.8f, 1e-5f);
        }

        beginTest ("time-scale multiplies note starts and lengths, preserves pitch/velocity");
        {
            std::vector<Note> ns { {60,0,1,0.7f}, {64,2,1,0.8f}, {67,3,0.5f,0.9f} };
            scaleNoteTimes (ns, 0.5);                        // double-time (twice as fast)
            expectWithinAbsoluteError (ns[0].startBeat, 0.0, 1e-9);
            expectWithinAbsoluteError (ns[1].startBeat, 1.0, 1e-9);
            expectWithinAbsoluteError (ns[1].lengthBeats, 0.5, 1e-9);
            expectWithinAbsoluteError (ns[2].startBeat, 1.5, 1e-9);
            expectWithinAbsoluteError (ns[2].lengthBeats, 0.25, 1e-9);
            expect (ns[1].pitch == 64 && ns[1].velocity == 0.8f);   // pitch/velocity untouched
            scaleNoteTimes (ns, 2.0);                        // scale back -> original
            expectWithinAbsoluteError (ns[1].startBeat, 2.0, 1e-9);
            expectWithinAbsoluteError (ns[2].lengthBeats, 0.5, 1e-9);
        }

        beginTest ("ratchet subdivides each note into equal same-pitch hits");
        {
            std::vector<Note> ns { {60,0,1,0.8f} };
            ratchetNotes (ns, 4);                            // 1-beat note -> 4 hits of 0.25
            expect (ns.size() == 4);
            std::sort (ns.begin(), ns.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
            for (int k = 0; k < 4; ++k)
            {
                expectWithinAbsoluteError (ns[(size_t) k].startBeat, k * 0.25, 1e-9);
                expectWithinAbsoluteError (ns[(size_t) k].lengthBeats, 0.25, 1e-9);
                expect (ns[(size_t) k].pitch == 60 && ns[(size_t) k].velocity == 0.8f);
            }
            // a chord (two notes) ratcheted x2 -> 4 hits (2 per note).
            std::vector<Note> ch { {60,0,1,0.7f}, {64,0,1,0.7f} };
            ratchetNotes (ch, 2);
            expect (ch.size() == 4);
        }

        beginTest ("flatten sets every velocity to one value, keeping pitch/timing");
        {
            std::vector<Note> ns { {60,0.0,1.0f,0.3f}, {64,1.0,0.5f,0.9f}, {67,2.0,0.5f,0.6f} };
            flattenVelocities (ns, 0.5f);
            for (auto& n : ns) expectWithinAbsoluteError (n.velocity, 0.5f, 1e-6f);
            expect (ns[0].pitch == 60 && ns[1].pitch == 64 && ns[2].pitch == 67);   // pitch untouched
            expectWithinAbsoluteError (ns[1].startBeat, 1.0, 1e-9);                  // timing untouched
            expect (ns[1].lengthBeats == 0.5f);
            // out-of-range value is clamped to 0..1.
            std::vector<Note> hi { {60,0,1,0.5f} };
            flattenVelocities (hi, 2.0f);
            expectWithinAbsoluteError (hi[0].velocity, 1.0f, 1e-6f);
        }

        beginTest ("gate scales note lengths, keeping starts (staccato/tenuto)");
        {
            std::vector<Note> ns { {60,0.0,1.0f,0.8f}, {62,1.0,0.5f,0.7f} };
            gateNotes (ns, 0.5);                             // staccato: halve lengths
            expectWithinAbsoluteError (ns[0].lengthBeats, 0.5, 1e-6);
            expectWithinAbsoluteError (ns[1].lengthBeats, 0.25, 1e-6);
            expectWithinAbsoluteError (ns[0].startBeat, 0.0, 1e-9);   // starts untouched
            expectWithinAbsoluteError (ns[1].startBeat, 1.0, 1e-9);
            expect (ns[0].pitch == 60 && ns[1].pitch == 62);
            // tenuto: lengthen; and the 0.01 floor holds for a tiny factor.
            std::vector<Note> tn { {60,0.0,1.0f,0.8f} };
            gateNotes (tn, 1.5);
            expectWithinAbsoluteError (tn[0].lengthBeats, 1.5, 1e-6);
        }

        beginTest ("partial quantize moves note starts toward the grid by strength");
        {
            std::vector<Note> ns { {60,0.10,1.0f,0.8f}, {62,0.40,1.0f,0.8f} };
            quantizeNotes (ns, 0.25, 0.5);                   // grid 0.25, halfway
            // 0.10 -> nearest 0.0, halfway = 0.05; 0.40 -> nearest 0.5, halfway = 0.45.
            expectWithinAbsoluteError (ns[0].startBeat, 0.05, 1e-9);
            expectWithinAbsoluteError (ns[1].startBeat, 0.45, 1e-9);
            // strength 1.0 = full snap; strength 0.0 = no move.
            std::vector<Note> full { {60,0.10,1.0f,0.8f} };
            quantizeNotes (full, 0.25, 1.0);
            expectWithinAbsoluteError (full[0].startBeat, 0.0, 1e-9);
            std::vector<Note> none { {60,0.10,1.0f,0.8f} };
            quantizeNotes (none, 0.25, 0.0);
            expectWithinAbsoluteError (none[0].startBeat, 0.10, 1e-9);
        }

        beginTest ("harmonize adds a parallel interval voice, keeping the originals");
        {
            std::vector<Note> ns { {60,0,1,0.8f}, {64,1,0.5f,0.7f} };
            harmonizeNotes (ns, 7);                          // add a fifth above each
            expect (ns.size() == 4);
            // originals untouched at the front; two new voices +7 sharing start/len/vel.
            std::sort (ns.begin(), ns.end(), [] (auto& a, auto& b) { return a.pitch < b.pitch; });
            expect (ns[0].pitch == 60 && ns[1].pitch == 64 && ns[2].pitch == 67 && ns[3].pitch == 71);
            // the +7 of the first note keeps its timing/velocity.
            for (auto& n : ns) if (n.pitch == 67) { expectWithinAbsoluteError (n.startBeat, 0.0, 1e-9);
                                                    expect (n.lengthBeats == 1.0f && n.velocity == 0.8f); }
            // a voice that would fall off the keyboard is dropped, not folded.
            std::vector<Note> hi { {125,0,1,0.5f} };
            harmonizeNotes (hi, 7);                           // 125+7=132 > 127 -> dropped
            expect (hi.size() == 1 && hi[0].pitch == 125);
        }

        beginTest ("swing delays off-beat notes only, preserving length/pitch");
        {
            // grid 0.5 (1/8): notes at 0, 0.5, 1.0, 1.5 -> off-beats (0.5, 1.5) shift by 0.33*0.5=0.165.
            std::vector<Note> ns { {60,0.0,0.5f,0.8f}, {62,0.5,0.5f,0.8f}, {64,1.0,0.5f,0.8f}, {65,1.5,0.5f,0.8f} };
            swingNotes (ns, 0.5, 0.33f);
            std::sort (ns.begin(), ns.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
            expectWithinAbsoluteError (ns[0].startBeat, 0.0, 1e-9);      // on-beat: unmoved
            expectWithinAbsoluteError (ns[1].startBeat, 0.665, 1e-6);    // 0.5 + 0.165
            expectWithinAbsoluteError (ns[2].startBeat, 1.0, 1e-9);      // on-beat: unmoved
            expectWithinAbsoluteError (ns[3].startBeat, 1.665, 1e-6);    // 1.5 + 0.165
            for (auto& n : ns) expect (n.lengthBeats == 0.5f);           // length preserved
            // amount 0 = straight (no move).
            std::vector<Note> st { {60,0.5,0.5f,0.8f} };
            swingNotes (st, 0.5, 0.0f);
            expectWithinAbsoluteError (st[0].startBeat, 0.5, 1e-9);
        }

        beginTest ("chordify builds a named chord from each root, keeping the root");
        {
            // major triad {+4,+7} on middle C -> C/E/G (60/64/67), timing/velocity kept.
            std::vector<Note> ns { {60,0.0,1.0f,0.8f} };
            chordifyNotes (ns, { 4, 7 });
            expect (ns.size() == 3);
            std::sort (ns.begin(), ns.end(), [] (auto& a, auto& b) { return a.pitch < b.pitch; });
            expect (ns[0].pitch == 60 && ns[1].pitch == 64 && ns[2].pitch == 67);
            for (auto& n : ns) { expectWithinAbsoluteError (n.startBeat, 0.0, 1e-9);
                                 expect (n.lengthBeats == 1.0f && n.velocity == 0.8f); }
            // dominant 7th {+4,+7,+10} -> 4 voices.
            std::vector<Note> d7 { {50,0,1,0.6f} };
            chordifyNotes (d7, { 4, 7, 10 });
            expect (d7.size() == 4);
            // a voice off the top is dropped (124 + 7 = 131 > 127), root + the in-range voice survive.
            std::vector<Note> hi { {124,0,1,0.5f} };
            chordifyNotes (hi, { 3, 7 });   // 127 ok, 131 dropped
            expect (hi.size() == 2 && hi[0].pitch == 124 && hi[1].pitch == 127);
        }

        beginTest ("melodic inversion mirrors pitches around the earliest note; timing kept");
        {
            std::vector<Note> ns { {60,0,1,0.8f}, {64,1,1,0.7f}, {67,2,0.5f,0.6f} };
            invertNotes (ns);                               // pivot = 60 (earliest) -> 60, 56, 53
            expect (ns[0].pitch == 60 && ns[1].pitch == 56 && ns[2].pitch == 53);
            expectWithinAbsoluteError (ns[1].startBeat, 1.0, 1e-9);   // timing untouched
            expect (ns[1].lengthBeats == 1.0f && ns[1].velocity == 0.7f);
            invertNotes (ns);                               // inverting twice restores (pivot still 60)
            expect (ns[0].pitch == 60 && ns[1].pitch == 64 && ns[2].pitch == 67);
        }

        beginTest ("MIDI echo appends decaying repeats; faded copies dropped; originals kept");
        {
            std::vector<Note> ns { {60,0,0.5f,0.8f} };
            echoNotes (ns, 0.5, 3, 0.5f);                   // vels 0.4/0.2/0.1 at 0.5/1.0/1.5
            expect (ns.size() == 4);
            std::sort (ns.begin(), ns.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
            expectWithinAbsoluteError (ns[0].startBeat, 0.0, 1e-9); expectWithinAbsoluteError (ns[0].velocity, 0.8f, 1e-5f);
            expectWithinAbsoluteError (ns[1].startBeat, 0.5, 1e-9); expectWithinAbsoluteError (ns[1].velocity, 0.4f, 1e-5f);
            expectWithinAbsoluteError (ns[2].startBeat, 1.0, 1e-9); expectWithinAbsoluteError (ns[2].velocity, 0.2f, 1e-5f);
            expectWithinAbsoluteError (ns[3].startBeat, 1.5, 1e-9); expectWithinAbsoluteError (ns[3].velocity, 0.1f, 1e-5f);
            expect (ns[1].pitch == 60 && ns[1].lengthBeats == 0.5f);   // pitch/length preserved
            // a low feedback drops copies once they fade below ~1%.
            std::vector<Note> few { {64,0,1,0.2f} };
            echoNotes (few, 0.25, 8, 0.1f);                 // 0.02 then 0.002(<0.01 -> stop) => 1 echo
            expect (few.size() == 2);
        }

        beginTest ("arpeggiate up sequences a chord");
        {
            std::vector<Note> ns { {60,0,1,0.8f}, {64,0,1,0.8f}, {67,0,1,0.8f} };
            arpeggiateNotes (ns, 0.25, 0);
            expect ((int) ns.size() == 3);
            std::sort (ns.begin(), ns.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
            expect (ns[0].pitch == 60 && ns[1].pitch == 64 && ns[2].pitch == 67);
            expectWithinAbsoluteError (ns[2].startBeat, 0.5, 1e-9);
        }

        beginTest ("expandArp repeats a held chord over its duration");
        {
            // A 2-beat C-major chord at 1/4 rate, 1 octave, gate 0.5, up = 8 steps cycling C,E,G.
            std::vector<Note> chord { {60,0,2,0.8f}, {64,0,2,0.8f}, {67,0,2,0.8f} };
            auto a = expandArp (chord, 0.25, 1, 0.5f, 0);
            expect ((int) a.size() == 8);
            expect (a[0].pitch == 60 && a[1].pitch == 64 && a[2].pitch == 67 && a[3].pitch == 60);
            expectWithinAbsoluteError (a[1].startBeat, 0.25, 1e-9);
            expectWithinAbsoluteError (a[0].lengthBeats, 0.125, 1e-9);   // 0.25 * gate 0.5
        }

        beginTest ("expandArp octaves widen the pattern");
        {
            std::vector<Note> chord { {60,0,1,0.8f}, {64,0,1,0.8f} };
            auto a = expandArp (chord, 0.25, 2, 1.0f, 0);        // 1-beat / 0.25 = 4 steps: C,E,C+12,E+12
            expect ((int) a.size() == 4);
            expect (a[0].pitch == 60 && a[1].pitch == 64 && a[2].pitch == 72 && a[3].pitch == 76);
        }

        beginTest ("expandArp repeats a single sustained note (like a real arp)");
        {
            std::vector<Note> one { {60,0,1,0.8f} };            // 1-beat note, 1/4 rate = 4 repeats
            auto a = expandArp (one, 0.25, 1, 1.0f, 0);
            expect ((int) a.size() == 4);
            for (auto& n : a) expect (n.pitch == 60);
            expectWithinAbsoluteError (a[3].startBeat, 0.75, 1e-9);
        }

        beginTest ("expandArp cycles octaves on a single note");
        {
            std::vector<Note> one { {60,0,1,0.8f} };
            auto a = expandArp (one, 0.25, 2, 1.0f, 0);         // pattern [60,72] over 4 steps
            expect ((int) a.size() == 4);
            expect (a[0].pitch == 60 && a[1].pitch == 72 && a[2].pitch == 60 && a[3].pitch == 72);
        }

        beginTest ("expandArp arpeggiates only notes held at each step");
        {
            // C for beats 0-1, then E for 1-2 (no overlap) -> each repeats while held.
            std::vector<Note> mel { {60,0,1,0.8f}, {64,1,1,0.8f} };
            auto a = expandArp (mel, 0.5, 1, 1.0f, 0);          // 4 steps: 0,0.5 hold C ; 1,1.5 hold E
            expect ((int) a.size() == 4);
            expect (a[0].pitch == 60 && a[1].pitch == 60 && a[2].pitch == 64 && a[3].pitch == 64);
        }

        beginTest ("expandArp restarts the pattern when the chord changes (root-first)");
        {
            // C-E-G (0-1) then D-F-A (1-2). Each chord must arpeggiate from its own root, not
            // carry the running index over (which used to start the 2nd chord mid-pattern).
            std::vector<Note> two { {60,0,1,0.8f}, {64,0,1,0.8f}, {67,0,1,0.8f},
                                    {62,1,1,0.8f}, {65,1,1,0.8f}, {69,1,1,0.8f} };
            auto a = expandArp (two, 0.5, 1, 1.0f, 0);          // up; steps at 0,0.5,1,1.5
            expect ((int) a.size() == 4);
            expect (a[0].pitch == 60 && a[1].pitch == 64);     // C E (chord 1 from root)
            expect (a[2].pitch == 62 && a[3].pitch == 65);     // D F (chord 2 restarts at root)
        }

        beginTest ("expandArp is deterministic for random mode");
        {
            std::vector<Note> chord { {60,0,2,0.8f}, {64,0,2,0.8f}, {67,0,2,0.8f} };
            auto a = expandArp (chord, 0.25, 1, 1.0f, 3);
            auto b = expandArp (chord, 0.25, 1, 1.0f, 3);
            expect (a.size() == b.size());
            bool same = true;
            for (size_t i = 0; i < a.size(); ++i) same = same && a[i].pitch == b[i].pitch;
            expect (same);
        }

        beginTest ("expandArp swing delays every other step");
        {
            std::vector<Note> chord { {60,0,1,0.8f}, {64,0,1,0.8f} };   // 1 beat, rate 1/4 = 4 steps
            auto a = expandArp (chord, 0.25, 1, 1.0f, 0, 0.4f);          // swing 0.4 -> odd steps +0.05
            expect ((int) a.size() == 4);
            expectWithinAbsoluteError (a[0].startBeat, 0.0,  1e-9);      // even: on grid
            expectWithinAbsoluteError (a[1].startBeat, 0.30, 1e-9);      // odd: 0.25 + 0.4*0.5*0.25
            expectWithinAbsoluteError (a[2].startBeat, 0.50, 1e-9);
            expectWithinAbsoluteError (a[3].startBeat, 0.80, 1e-9);
        }

        beginTest ("expandArp hold latches the chord to fill the clip");
        {
            std::vector<Note> chord { {60,0,1,0.8f}, {64,0,1,0.8f} };   // sounds only beats 0-1
            auto off = expandArp (chord, 0.5, 1, 1.0f, 0, 0.0f, false, 4.0);
            expect ((int) off.size() == 2);                             // no hold: only while sounding
            auto on  = expandArp (chord, 0.5, 1, 1.0f, 0, 0.0f, true, 4.0);
            expect ((int) on.size() == 8);                             // hold: latched across the 4-beat clip
            for (auto& n : on) expect (n.pitch == 60 || n.pitch == 64);
        }
    }
};

//==============================================================================
// Live arpeggiator: hold a chord on the live input and it plays a stepped pattern,
// tracking the held set. Drives the engine block-by-block and reads out the note-ons.
struct LiveArpTests : juce::UnitTest
{
    LiveArpTests() : juce::UnitTest ("LiveArp") {}

    // Push `msgs` (at sample 0) into block 0, then run `blocks` blocks of `num` samples each,
    // returning the ordered list of note-on pitches the arp emitted across all blocks.
    static std::vector<int> runArp (LiveArp& arp, const std::vector<juce::MidiMessage>& msgs,
                                    int blocks, int num, double spb, double rate, int octaves,
                                    float gate, int mode, bool hold)
    {
        std::vector<int> ons;
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer in;
            if (b == 0) for (auto& m : msgs) in.addEvent (m, 0);
            juce::MidiBuffer out;
            arp.process (in, out, num, spb, rate, octaves, gate, mode, hold);
            for (const auto meta : out)
                if (meta.getMessage().isNoteOn()) ons.push_back (meta.getMessage().getNoteNumber());
        }
        return ons;
    }

    void runTest() override
    {
        const double spb = 100.0;   // 100 samples/beat; one block == one beat below

        beginTest ("a held chord arpeggiates up, one note per rate step");
        {
            LiveArp arp;
            std::vector<juce::MidiMessage> chord {
                juce::MidiMessage::noteOn (1, 60, 0.8f),
                juce::MidiMessage::noteOn (1, 64, 0.8f),
                juce::MidiMessage::noteOn (1, 67, 0.8f) };
            // rate 1 beat, block == 1 beat: expect C,E,G,C,E,G over 6 blocks (hold latches release).
            auto ons = runArp (arp, chord, 6, 100, spb, 1.0, 1, 1.0f, 0, true);
            expect ((int) ons.size() == 6);
            const int want[] = { 60, 64, 67, 60, 64, 67 };
            for (int i = 0; i < 6 && i < (int) ons.size(); ++i) expectEquals (ons[(size_t) i], want[i]);
        }

        beginTest ("down mode reverses the order");
        {
            LiveArp arp;
            std::vector<juce::MidiMessage> chord {
                juce::MidiMessage::noteOn (1, 60, 0.8f),
                juce::MidiMessage::noteOn (1, 64, 0.8f),
                juce::MidiMessage::noteOn (1, 67, 0.8f) };
            auto ons = runArp (arp, chord, 3, 100, spb, 1.0, 1, 1.0f, 1, true);
            const int want[] = { 67, 64, 60 };
            expect ((int) ons.size() == 3);
            for (int i = 0; i < 3 && i < (int) ons.size(); ++i) expectEquals (ons[(size_t) i], want[i]);
        }

        beginTest ("octaves widen the pattern (+12)");
        {
            LiveArp arp;
            std::vector<juce::MidiMessage> one { juce::MidiMessage::noteOn (1, 60, 0.8f) };
            auto ons = runArp (arp, one, 4, 100, spb, 1.0, 2, 1.0f, 0, true);   // pattern [60,72]
            const int want[] = { 60, 72, 60, 72 };
            expect ((int) ons.size() == 4);
            for (int i = 0; i < 4 && i < (int) ons.size(); ++i) expectEquals (ons[(size_t) i], want[i]);
        }

        beginTest ("releasing without hold stops the arp");
        {
            LiveArp arp;
            // Block 0: press C+E. Blocks 1..: release both (note-off) -> arp goes silent (no hold).
            std::vector<int> ons;
            for (int b = 0; b < 4; ++b)
            {
                juce::MidiBuffer in, out;
                if (b == 0) { in.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
                              in.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 0); }
                if (b == 1) { in.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                              in.addEvent (juce::MidiMessage::noteOff (1, 64), 0); }
                arp.process (in, out, 100, spb, 1.0, 1, 1.0f, 0, false);
                for (const auto meta : out) if (meta.getMessage().isNoteOn()) ons.push_back (meta.getMessage().getNoteNumber());
            }
            expect (ons.size() == 1);          // only the first step (C) fired before release
            if (! ons.empty()) expectEquals (ons[0], 60);
        }

        beginTest ("a new chord restarts the pattern from its root");
        {
            LiveArp arp;
            std::vector<int> ons;
            for (int b = 0; b < 4; ++b)
            {
                juce::MidiBuffer in, out;
                if (b == 0) { in.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);   // C major-ish
                              in.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 0);
                              in.addEvent (juce::MidiMessage::noteOn (1, 67, 0.8f), 0); }
                if (b == 2) { in.addEvent (juce::MidiMessage::noteOff (1, 60), 0);         // swap to D-F-A
                              in.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
                              in.addEvent (juce::MidiMessage::noteOff (1, 67), 0);
                              in.addEvent (juce::MidiMessage::noteOn (1, 62, 0.8f), 0);
                              in.addEvent (juce::MidiMessage::noteOn (1, 65, 0.8f), 0);
                              in.addEvent (juce::MidiMessage::noteOn (1, 69, 0.8f), 0); }
                arp.process (in, out, 100, spb, 1.0, 1, 1.0f, 0, false);
                for (const auto meta : out) if (meta.getMessage().isNoteOn()) ons.push_back (meta.getMessage().getNoteNumber());
            }
            // Blocks: C, E, then new chord roots from D: D, F.
            const int want[] = { 60, 64, 62, 65 };
            expect ((int) ons.size() == 4);
            for (int i = 0; i < 4 && i < (int) ons.size(); ++i) expectEquals (ons[(size_t) i], want[i]);
        }

        beginTest ("gate shortens the note within the step (note-off before next step)");
        {
            LiveArp arp;
            std::vector<juce::MidiMessage> one { juce::MidiMessage::noteOn (1, 60, 0.8f) };
            juce::MidiBuffer in, out;
            for (auto& m : one) in.addEvent (m, 0);
            arp.process (in, out, 100, spb, 1.0, 1, 0.5f, 0, true);   // gate 0.5 -> off at sample ~50
            int onOff = -1, offOff = -1;
            for (const auto meta : out)
            {
                if (meta.getMessage().isNoteOn())  onOff  = meta.samplePosition;
                if (meta.getMessage().isNoteOff()) offOff = meta.samplePosition;
            }
            expectEquals (onOff, 0);
            expect (offOff > 40 && offOff < 60);     // ~half the 100-sample beat
        }
    }
};

//==============================================================================
// Session view model: the clip-launch grid stays rectangular (every track's slot column
// matches the scene count) as scenes are inserted/removed.
struct SessionModelTests : juce::UnitTest
{
    SessionModelTests() : juce::UnitTest ("SessionModel") {}

    static std::shared_ptr<Clip> namedClip (const juce::String& n)
    {
        auto c = std::make_shared<Clip>();
        c->name = n;
        return c;
    }

    void runTest() override
    {
        beginTest ("ensureSlotCount null-pads and truncates");
        {
            SessionSlots s;
            ensureSlotCount (s, 3);
            expectEquals ((int) s.size(), 3);
            for (auto& c : s) expect (c == nullptr);       // new slots are empty
            s[1] = namedClip ("keep");
            ensureSlotCount (s, 5);
            expectEquals ((int) s.size(), 5);
            expect (s[1] != nullptr && s[1]->name == "keep");   // existing slots preserved
            ensureSlotCount (s, 2);
            expectEquals ((int) s.size(), 2);
            expect (s[1] != nullptr && s[1]->name == "keep");
        }

        beginTest ("insertSceneSlot shifts later slots down, adds an empty");
        {
            SessionSlots s { namedClip ("A"), namedClip ("B"), namedClip ("C") };
            insertSceneSlot (s, 1);                        // A _ B C
            expectEquals ((int) s.size(), 4);
            expect (s[0]->name == "A");
            expect (s[1] == nullptr);
            expect (s[2]->name == "B");
            expect (s[3]->name == "C");
        }

        beginTest ("insertSceneSlot clamps out-of-range index to the end");
        {
            SessionSlots s { namedClip ("A") };
            insertSceneSlot (s, 99);
            expectEquals ((int) s.size(), 2);
            expect (s[0]->name == "A");
            expect (s[1] == nullptr);
        }

        beginTest ("removeSceneSlot drops the right slot (and no-ops out of range)");
        {
            SessionSlots s { namedClip ("A"), namedClip ("B"), namedClip ("C") };
            removeSceneSlot (s, 1);                         // A C
            expectEquals ((int) s.size(), 2);
            expect (s[0]->name == "A");
            expect (s[1]->name == "C");
            removeSceneSlot (s, 42);                        // out of range -> unchanged
            expectEquals ((int) s.size(), 2);
        }

        beginTest ("a scene op keeps every track column rectangular");
        {
            std::vector<Scene> scenes { {"Intro", {}}, {"Verse", {}} };
            std::vector<SessionSlots> tracks (3);
            for (auto& t : tracks) ensureSlotCount (t, (int) scenes.size());
            for (auto& t : tracks) expectEquals ((int) t.size(), 2);

            // Add a scene at the end -> insert a slot in every track column.
            scenes.push_back ({ "Chorus", {} });
            for (auto& t : tracks) insertSceneSlot (t, (int) scenes.size() - 1);
            for (auto& t : tracks) expectEquals ((int) t.size(), 3);

            // Delete the middle scene -> remove that slot in every track column.
            scenes.erase (scenes.begin() + 1);
            for (auto& t : tracks) removeSceneSlot (t, 1);
            for (auto& t : tracks) expectEquals ((int) t.size(), (int) scenes.size());
        }

        beginTest ("slotClip is bounds-safe");
        {
            SessionSlots s { namedClip ("A"), nullptr };
            expect (slotClip (s, 0) != nullptr && slotClip (s, 0)->name == "A");
            expect (slotClip (s, 1) == nullptr);           // empty slot
            expect (slotClip (s, -1) == nullptr);          // out of range
            expect (slotClip (s, 5) == nullptr);
        }
    }
};

//==============================================================================
// Session launch engine: quantized clip/scene launching, per-track override, back-to-arrangement.
struct SessionLauncherTests : juce::UnitTest
{
    SessionLauncherTests() : juce::UnitTest ("SessionLauncher") {}

    static bool contains (const std::vector<int>& v, int x)
    {
        return std::find (v.begin(), v.end(), x) != v.end();
    }

    void runTest() override
    {
        beginTest ("a launch fires at the next quantum boundary, not before");
        {
            SessionLauncher L; L.setTrackCount (2); L.setQuantumBeats (4.0);
            L.requestClip (0, 2);
            L.advance (1.0, 1.05);                         // mid-bar: nothing yet
            expectEquals (L.playingSlot (0), -1);          // still arrangement
            expect (L.changedTracks().empty());
            expectEquals (L.pendingSlot (0), 2);           // queued
            L.advance (3.98, 4.02);                        // crosses the bar line
            expectEquals (L.playingSlot (0), 2);
            expectWithinAbsoluteError (L.launchBeat (0), 4.0, 1e-9);
            expect (contains (L.changedTracks(), 0));
            expectEquals (L.pendingSlot (0), (int) SessionLauncher::kNone);
        }

        beginTest ("quantum 0 launches immediately");
        {
            SessionLauncher L; L.setTrackCount (1); L.setQuantumBeats (0.0);
            L.requestClip (0, 1);
            L.advance (1.0, 1.05);
            expectEquals (L.playingSlot (0), 1);
            expectWithinAbsoluteError (L.launchBeat (0), 1.0, 1e-9);
        }

        beginTest ("a boundary exactly at the block start still fires");
        {
            SessionLauncher L; L.setTrackCount (1); L.setQuantumBeats (4.0);
            L.requestClip (0, 3);
            L.advance (8.0, 8.05);                         // block begins on a bar line
            expectEquals (L.playingSlot (0), 3);
            expectWithinAbsoluteError (L.launchBeat (0), 8.0, 1e-9);
        }

        beginTest ("the latest request before the boundary wins (one clip per track)");
        {
            SessionLauncher L; L.setTrackCount (1); L.setQuantumBeats (4.0);
            L.requestClip (0, 0);
            L.requestClip (0, 1);                          // overwrites the queued launch
            L.advance (3.9, 4.1);
            expectEquals (L.playingSlot (0), 1);
        }

        beginTest ("re-launching the same slot retriggers (new launchBeat)");
        {
            SessionLauncher L; L.setTrackCount (1); L.setQuantumBeats (4.0);
            L.requestClip (0, 0); L.advance (3.9, 4.1);
            expectWithinAbsoluteError (L.launchBeat (0), 4.0, 1e-9);
            L.requestClip (0, 0);                          // same slot again
            L.advance (7.9, 8.1);
            expectWithinAbsoluteError (L.launchBeat (0), 8.0, 1e-9);   // phase reset
            expect (contains (L.changedTracks(), 0));
        }

        beginTest ("stop returns a track to arrangement; stopping arrangement is a no-op");
        {
            SessionLauncher L; L.setTrackCount (1); L.setQuantumBeats (4.0);
            L.requestClip (0, 2); L.advance (3.9, 4.1);
            expectEquals (L.playingSlot (0), 2);
            L.requestStop (0); L.advance (7.9, 8.1);
            expectEquals (L.playingSlot (0), -1);
            expect (contains (L.changedTracks(), 0));
            L.requestStop (0); L.advance (11.9, 12.1);     // already arrangement
            expect (L.changedTracks().empty());
        }

        beginTest ("scene launch fires occupied slots only, leaving empty ones alone");
        {
            SessionLauncher L; L.setTrackCount (3); L.setQuantumBeats (4.0);
            std::vector<bool> occupied { true, false, true };
            L.requestScene (1, occupied);
            L.advance (3.9, 4.1);
            expectEquals (L.playingSlot (0), 1);
            expectEquals (L.playingSlot (1), -1);          // empty slot: unchanged
            expectEquals (L.playingSlot (2), 1);
        }

        beginTest ("stopAll and reset clear all tracks");
        {
            SessionLauncher L; L.setTrackCount (2); L.setQuantumBeats (4.0);
            L.requestClip (0, 0); L.requestClip (1, 1); L.advance (3.9, 4.1);
            expect (L.anyPlaying());
            L.requestStopAll(); L.advance (7.9, 8.1);
            expect (! L.anyPlaying());
            L.requestClip (0, 0); L.advance (11.9, 12.1);
            expect (L.anyPlaying());
            L.reset();                                     // immediate, no advance needed
            expect (! L.anyPlaying());
        }

        beginTest ("advance with nothing pending does nothing");
        {
            SessionLauncher L; L.setTrackCount (1); L.setQuantumBeats (4.0);
            L.advance (0.0, 100.0);
            expectEquals (L.playingSlot (0), -1);
            expect (L.changedTracks().empty());
        }
    }
};

//==============================================================================
// Session clip playback: a launched clip loops from its launch beat, phased into the block.
struct SessionClipTests : juce::UnitTest
{
    SessionClipTests() : juce::UnitTest ("SessionClip") {}

    static std::vector<int> noteOnOffsets (const juce::MidiBuffer& m)
    {
        std::vector<int> offs;
        for (const auto meta : m) if (meta.getMessage().isNoteOn()) offs.push_back (meta.samplePosition);
        std::sort (offs.begin(), offs.end());
        return offs;
    }

    void runTest() override
    {
        TempoConv tc; tc.setMarkers (nullptr, nullptr, 0, 44100.0, 100.0);   // constant, 100 samples/beat
        const double spb = 100.0;
        std::vector<Note> oneNote { { 60, 0.0, 0.5, 0.8f } };                // pitch 60 @ beat 0, len 0.5

        beginTest ("a 1-beat loop repeats across the block");
        {
            juce::MidiBuffer m;
            // launchBeat 0, block [0, 2.5 beats) = 250 samples -> reps at beats 0,1,2.
            collectSessionClip (oneNote, m, tc, spb, 0.0, 0.0, 0, 250, 1.0);
            auto on = noteOnOffsets (m);
            expect (on.size() == 3);
            const int want[] = { 0, 100, 200 };
            for (int i = 0; i < 3 && i < (int) on.size(); ++i) expectEquals (on[(size_t) i], want[i]);
        }

        beginTest ("phase follows the monotonic session beat across blocks");
        {
            juce::MidiBuffer m;
            // Second block: session beat starts at 1.0; one beat wide -> the rep at beat 1 fires at offset 0.
            collectSessionClip (oneNote, m, tc, spb, 0.0, 1.0, 0, 100, 1.0);
            auto on = noteOnOffsets (m);
            expect (on.size() == 1);
            if (! on.empty()) expectEquals (on[0], 0);
        }

        beginTest ("a block starting mid-loop places the next rep correctly");
        {
            juce::MidiBuffer m;
            // Block covers session beats [0.5, 1.5): rep at beat 0 already passed; rep at beat 1 -> offset 50.
            collectSessionClip (oneNote, m, tc, spb, 0.0, 0.5, 0, 100, 1.0);
            auto on = noteOnOffsets (m);
            expect (on.size() == 1);
            if (! on.empty()) expectEquals (on[0], 50);
        }

        beginTest ("firstSample sub-range offsets events into the back of the block (launch split)");
        {
            juce::MidiBuffer m;
            // Render only [firstSample=40, +100) of a block whose session beat starts at 0.
            // The rep at beat 1 (sample 100) lands in the sub-range at offset 100.
            collectSessionClip (oneNote, m, tc, spb, 0.0, 0.0, 40, 100, 1.0);
            auto on = noteOnOffsets (m);
            // beat-0 rep (sample 0) is before the sub-range [40,140); beat-1 rep (sample 100) is inside.
            expect (on.size() == 1);
            if (! on.empty()) expectEquals (on[0], 100);
        }

        beginTest ("transpose and empty/degenerate inputs are handled");
        {
            juce::MidiBuffer m;
            collectSessionClip (oneNote, m, tc, spb, 0.0, 0.0, 0, 100, 1.0, /*transpose*/ 12);
            bool sawTransposed = false;
            for (const auto meta : m) if (meta.getMessage().isNoteOn()) sawTransposed = (meta.getMessage().getNoteNumber() == 72);
            expect (sawTransposed);

            juce::MidiBuffer empty;
            collectSessionClip ({}, empty, tc, spb, 0.0, 0.0, 0, 100, 1.0);         // no notes
            collectSessionClip (oneNote, empty, tc, spb, 0.0, 0.0, 0, 0, 1.0);      // zero count
            collectSessionClip (oneNote, empty, tc, spb, 0.0, 0.0, 0, 100, 0.0);    // zero loop len
            expect (noteOnOffsets (empty).empty());
        }
    }
};

//==============================================================================
// Drag-and-drop routing: the classifier decides which load op a dropped file gets.
struct FileDropTests : juce::UnitTest
{
    FileDropTests() : juce::UnitTest ("FileDrop") {}

    void runTest() override
    {
        beginTest ("classifyDroppedFile routes by extension (case-insensitive)");
        {
            auto k = [] (const char* name) { return classifyDroppedFile (juce::File ("/x/" + juce::String (name))); };
            expect (k ("song.gloopy") == DroppedFileKind::Project);
            expect (k ("song.zip")    == DroppedFileKind::Project);
            expect (k ("beat.mid")    == DroppedFileKind::Midi);
            expect (k ("beat.MIDI")   == DroppedFileKind::Midi);
            expect (k ("vox.wav")     == DroppedFileKind::Audio);
            expect (k ("vox.AIFF")    == DroppedFileKind::Audio);
            expect (k ("horn.flac")   == DroppedFileKind::Audio);
            expect (k ("notes.txt")   == DroppedFileKind::Unsupported);
            expect (k ("noext")       == DroppedFileKind::Unsupported);   // not an existing dir
        }

        beginTest ("a composition folder (no extension, is a directory) is a Project");
        {
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("gloopy-filedrop-test-dir");
            dir.createDirectory();
            expect (classifyDroppedFile (dir) == DroppedFileKind::Project);
            dir.deleteRecursively();
        }

        beginTest ("browser drag payload round-trips, keeping spaces in the ref (tab-delimited)");
        {
            // a sample path with spaces (the reason we tab-delimit, not space-delimit)
            const auto ref = juce::String ("/home/me/My Samples/kick 01.wav");
            const auto it  = parseBrowserDrag (makeBrowserDrag ("sample", ref, "kick 01.wav"));
            expect (it.valid);
            expectEquals (it.kind, juce::String ("sample"));
            expectEquals (it.ref, ref);                          // path + spaces intact
            expectEquals (it.label, juce::String ("kick 01.wav"));
            // label defaults to ref when omitted; empty/garbage is invalid
            expectEquals (parseBrowserDrag ("plugin\tacme.synth").label, juce::String ("acme.synth"));
            expect (! parseBrowserDrag ("").valid);
            expect (! parseBrowserDrag ("templateonly").valid);  // no tab -> not a valid payload
        }

        beginTest ("browser drop targets a track only for a sample dropped on a lane");
        {
            expect (  browserDropPlacesClip ("sample", 0));   // sample on the first track lane
            expect (  browserDropPlacesClip ("sample", 3));   // sample on some track lane
            expect (! browserDropPlacesClip ("sample", -1));  // sample missed the lanes -> new track
            expect (! browserDropPlacesClip ("plugin", 2));   // a plugin on a track -> generic action
            expect (! browserDropPlacesClip ("template", 0)); // a template on a track -> seed a project
            expect (! browserDropPlacesClip ("preset", 1));
        }

        beginTest ("ieq: alloc-free case-insensitive param-name match (applyParamValue dispatch)");
        {
            expect (  gloopy::ieq ("cutoff", "cutoff"));
            expect (  gloopy::ieq ("Cutoff", "cutoff"));   // the old toLowerCase() behaviour, now alloc-free
            expect (  gloopy::ieq ("SYNTH",  "synth"));
            expect (  gloopy::ieq ("",       ""));
            expect (! gloopy::ieq ("cutoff", "reso"));
            expect (! gloopy::ieq ("cut",    "cutoff"));   // prefix is not a match
            expect (! gloopy::ieq ("cutoff", "cut"));
            expect (! gloopy::ieq ("",       "x"));
        }
    }
};

//==============================================================================
// Stereo widener (mid/side): width scales the side signal (L-R) while the mono
// sum (L+R) is invariant. Verified directly on a buffer with a known L/R split.
struct StereoWidenerTests : juce::UnitTest
{
    StereoWidenerTests() : juce::UnitTest ("StereoWidener") {}

    void runTest() override
    {
        beginTest ("width 1 is identity");
        {
            float l = 0.8f, r = 0.2f;                  // mid 0.5, side 0.3
            widenSample (l, r, 1.0f);
            expectWithinAbsoluteError (l, 0.8f, 1.0e-6f);
            expectWithinAbsoluteError (r, 0.2f, 1.0e-6f);
        }

        beginTest ("width 0 collapses to mono; width 2 doubles the side; mono sum invariant");
        {
            for (float w : { 0.0f, 2.0f })
            {
                float l = 0.8f, r = 0.2f;
                widenSample (l, r, w);
                expectWithinAbsoluteError (l - r, 0.6f * w, 1.0e-6f);   // (L-R) = 2*side = 0.6*w
                expectWithinAbsoluteError (l + r, 1.0f,     1.0e-6f);   // 2*mid, width-invariant
            }
        }

        beginTest ("an already-mono signal is untouched at any width");
        {
            for (float w : { 0.0f, 1.0f, 2.0f })
            {
                float l = 0.4f, r = 0.4f;
                widenSample (l, r, w);
                expectWithinAbsoluteError (l, 0.4f, 1.0e-6f);
                expectWithinAbsoluteError (r, 0.4f, 1.0e-6f);
            }
        }
    }
};

//==============================================================================
// LFO math: a tempo-synced LFO's phase tracks the beat position (one cycle per
// syncBeats beats), independent of rate/seconds; a free LFO uses rate*seconds.
struct LfoTests : juce::UnitTest
{
    LfoTests() : juce::UnitTest ("Lfo") {}

    void runTest() override
    {
        beginTest ("tempo-synced phase follows beats, ignores rate/time");
        {
            // sync=2 beats/cycle: beat 1 -> 0.5 cycles, beat 3 -> 1.5 cycles.
            expectWithinAbsoluteError (lfoPhaseCycles (2.0f, 1.0, /*rate*/ 99.0f, /*t*/ 99.0), 0.5, 1e-12);
            expectWithinAbsoluteError (lfoPhaseCycles (2.0f, 3.0, 99.0f, 99.0), 1.5, 1e-12);
            // sync=4 (one cycle per 4/4 bar): beat 2 -> 0.5 cycles.
            expectWithinAbsoluteError (lfoPhaseCycles (4.0f, 2.0, 0.0f, 0.0), 0.5, 1e-12);
        }

        beginTest ("free-running phase is rate*seconds, ignores beats");
        {
            expectWithinAbsoluteError (lfoPhaseCycles (0.0f, /*beat*/ 99.0, 2.0f, 3.0), 6.0, 1e-12);
            expectWithinAbsoluteError (lfoPhaseCycles (0.0f, 99.0, 0.5f, 4.0), 2.0, 1e-12);
        }

        beginTest ("osc shapes at known phases (bipolar)");
        {
            expectWithinAbsoluteError (lfoOsc (0, 0.25), 1.0, 1e-9);    // sine peak
            expectWithinAbsoluteError (lfoOsc (0, 0.75), -1.0, 1e-9);   // sine trough
            expectWithinAbsoluteError (lfoOsc (1, 0.0), 1.0, 1e-9);     // triangle peaks at cycle start
            expectWithinAbsoluteError (lfoOsc (1, 0.5), -1.0, 1e-9);    // triangle troughs at mid
            expectWithinAbsoluteError (lfoOsc (2, 0.5), 0.0, 1e-9);     // saw zero at mid
            expect (lfoOsc (3, 0.25) == 1.0 && lfoOsc (3, 0.75) == -1.0);   // square
            // phase wraps: integer cycles land at the cycle start.
            expectWithinAbsoluteError (lfoOsc (0, 5.25), lfoOsc (0, 0.25), 1e-12);
        }

        beginTest ("sample-and-hold: held within a cycle, stepped + deterministic across cycles");
        {
            // Same integer cycle -> the value is held (constant) regardless of fraction.
            expect (lfoOsc (4, 3.0) == lfoOsc (4, 3.4));
            expect (lfoOsc (4, 3.4) == lfoOsc (4, 3.99));
            // A new cycle picks a new value (with overwhelming probability the hash differs).
            expect (lfoOsc (4, 3.0) != lfoOsc (4, 4.0));
            expect (lfoOsc (4, 4.0) != lfoOsc (4, 5.0));
            // Deterministic: the same step always yields the same value (reproducible renders).
            expect (lfoOsc (4, 7.2) == lfoOsc (4, 7.8));
            // In range [-1, 1) and not all-zero across a run of steps.
            bool anyNonZero = false;
            for (int s = 0; s < 32; ++s)
            {
                const double v = lfoOsc (4, (double) s + 0.5);
                expect (v >= -1.0 && v < 1.0);
                if (std::abs (v) > 1.0e-6) anyNonZero = true;
            }
            expect (anyNonZero);
        }

        beginTest ("phase offset + unipolar folding (lfoUnit)");
        {
            // Phase offset shifts the waveform: a 0.25-cycle offset makes phase 0 read
            // like phase 0.25 (sine peak).
            expectWithinAbsoluteError (lfoUnit (0, 0.0, 0.25f, false), 1.0, 1e-9);
            expectWithinAbsoluteError (lfoUnit (0, 0.5, 0.25f, false), -1.0, 1e-9);   // 0.5+0.25=0.75 trough
            // Bipolar passes the osc through unchanged.
            expectWithinAbsoluteError (lfoUnit (0, 0.25, 0.0f, false), 1.0, 1e-9);
            // Unipolar folds [-1,1] -> [0,1]: the sine peak/trough/zero map to 1/0/0.5.
            expectWithinAbsoluteError (lfoUnit (0, 0.25, 0.0f, true), 1.0, 1e-9);     // peak -> 1
            expectWithinAbsoluteError (lfoUnit (0, 0.75, 0.0f, true), 0.0, 1e-9);     // trough -> 0
            expectWithinAbsoluteError (lfoUnit (0, 0.0,  0.0f, true), 0.5, 1e-9);     // zero -> 0.5 (never below centre)
        }
    }
};

//==============================================================================
struct ParamScaleTests : juce::UnitTest
{
    ParamScaleTests() : juce::UnitTest ("ParamScale") {}
    void runTest() override
    {
        beginTest ("log taper: half-way is the geometric mean");
        {
            // A filter cutoff 20..20000 Hz. Position 0.5 -> sqrt(20*20000) = 632.46 Hz.
            expectWithinAbsoluteError ((double) paramDenormalize (0.5f, 20.f, 20000.f, "log"), 632.4555, 0.5);
            expectWithinAbsoluteError ((double) paramDenormalize (0.0f, 20.f, 20000.f, "log"), 20.0, 1e-4);
            expectWithinAbsoluteError ((double) paramDenormalize (1.0f, 20.f, 20000.f, "log"), 20000.0, 1e-2);
            // Round-trips.
            expectWithinAbsoluteError ((double) paramNormalize (632.4555f, 20.f, 20000.f, "log"), 0.5, 1e-4);
            expectWithinAbsoluteError ((double) paramNormalize (paramDenormalize (0.3f, 20.f, 20000.f, "log"),
                                                                20.f, 20000.f, "log"), 0.3, 1e-5);
        }

        beginTest ("linear taper is (min + pos*range)");
        {
            expectWithinAbsoluteError ((double) paramDenormalize (0.5f, -1.f, 1.f, "linear"), 0.0, 1e-6);
            expectWithinAbsoluteError ((double) paramDenormalize (0.25f, 0.f, 8.f, "linear"), 2.0, 1e-6);
            expectWithinAbsoluteError ((double) paramNormalize (2.0f, 0.f, 8.f, "linear"), 0.25, 1e-6);
        }

        beginTest ("dB taper travels in decibels, floors at min=0");
        {
            // amplitude 0..1, position 0.5 -> -30 dB (halfway between the -60 floor and 0 dB).
            expectWithinAbsoluteError ((double) (20.0 * std::log10 (paramDenormalize (0.5f, 0.f, 1.f, "dB"))), -30.0, 0.1);
            expectWithinAbsoluteError ((double) paramDenormalize (1.0f, 0.f, 1.f, "dB"), 1.0, 1e-4);
        }
    }
};

struct NotesJsonTests : juce::UnitTest
{
    NotesJsonTests() : juce::UnitTest ("NotesJson") {}
    void runTest() override
    {
        beginTest ("notes round-trip through JSON");
        {
            std::vector<Note> ns { {60, 0.0, 1.0, 0.8f}, {64, 1.5, 0.5, 1.0f}, {67, 2.0, 0.25, 0.3f} };
            const auto back = notesFromJson (notesToJson (ns));
            expectEquals ((int) back.size(), 3);
            for (size_t i = 0; i < back.size(); ++i)
            {
                expectEquals (back[i].pitch, ns[i].pitch);
                expectWithinAbsoluteError (back[i].startBeat, ns[i].startBeat, 1e-9);
                expectWithinAbsoluteError (back[i].lengthBeats, ns[i].lengthBeats, 1e-9);
                expectWithinAbsoluteError ((double) back[i].velocity, (double) ns[i].velocity, 1e-6);
            }
        }
        beginTest ("tolerant of missing keys, a {notes:[...]} wrapper, and clamps");
        {
            const auto a = notesFromJson ("[{\"pitch\":72}]");   // defaults for start/length/velocity
            expectEquals ((int) a.size(), 1);
            expectEquals (a[0].pitch, 72);
            expectWithinAbsoluteError (a[0].lengthBeats, 1.0, 1e-9);   // default
            const auto b = notesFromJson ("{\"notes\":[{\"pitch\":200,\"velocity\":9}]}");
            expectEquals ((int) b.size(), 1);
            expectEquals (b[0].pitch, 127);                            // clamped 0..127
            expectWithinAbsoluteError ((double) b[0].velocity, 1.0, 1e-6);   // clamped 0..1
        }
        beginTest ("garbage / empty parses to no notes");
        {
            expect (notesFromJson ("not json").empty());
            expect (notesFromJson ("").empty());
            expect (notesFromJson ("[]").empty());
        }
    }
};

struct AllpassPhaserTests : juce::UnitTest
{
    AllpassPhaserTests() : juce::UnitTest ("AllpassPhaser") {}
    void runTest() override
    {
        beginTest ("coefficient stays in (-1,1) and moves with frequency");
        {
            const double sr = 44100.0;
            const float lo = phaserCoeff (200.0f, sr), hi = phaserCoeff (6000.0f, sr);
            expect (lo > -1.0f && lo < 1.0f);
            expect (hi > -1.0f && hi < 1.0f);
            expect (hi > lo);                                  // higher fc -> larger coefficient
        }
        beginTest ("a cascade of allpass stages preserves amplitude (allpass property)");
        {
            const double sr = 44100.0;
            const float a = phaserCoeff (1000.0f, sr);
            std::array<float, 6> z {}; z.fill (0.0f);
            const int n = 8000;
            float inPeak = 0.0f, outPeak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float x = std::sin (2.0 * 3.14159265358979323846 * 440.0 * i / sr);
                float y = x;
                for (auto& s : z) y = allpassStage (y, a, s);
                if (i > n / 2)                                  // ignore the settling transient
                {
                    inPeak  = juce::jmax (inPeak,  std::abs (x));
                    outPeak = juce::jmax (outPeak, std::abs (y));
                }
            }
            expectWithinAbsoluteError ((double) outPeak, (double) inPeak, 0.02);   // magnitude preserved
        }
    }
};

struct BiquadEqTests : juce::UnitTest
{
    BiquadEqTests() : juce::UnitTest ("BiquadEq") {}
    void runTest() override
    {
        const double sr = 44100.0;
        beginTest ("low shelf hits its gain in the bass and is flat up high");
        {
            const auto c = eqLowShelf (200.0, 12.0, sr);
            expectWithinAbsoluteError (biquadMagnitudeDb (c, 30.0, sr),   12.0, 0.6);   // deep bass ~ +12 dB
            expectWithinAbsoluteError (biquadMagnitudeDb (c, 8000.0, sr),  0.0, 0.6);   // highs untouched
        }
        beginTest ("high shelf hits its gain up high and is flat in the bass");
        {
            const auto c = eqHighShelf (5000.0, -12.0, sr);
            expectWithinAbsoluteError (biquadMagnitudeDb (c, 18000.0, sr), -12.0, 0.8);  // top ~ -12 dB
            expectWithinAbsoluteError (biquadMagnitudeDb (c, 100.0, sr),     0.0, 0.6);  // bass untouched
        }
        beginTest ("peaking band boosts at its centre and is flat far away");
        {
            const auto c = eqPeak (1000.0, 12.0, 2.0, sr);
            expectWithinAbsoluteError (biquadMagnitudeDb (c, 1000.0, sr), 12.0, 0.3);   // +12 dB at centre
            expectWithinAbsoluteError (biquadMagnitudeDb (c, 60.0, sr),    0.0, 0.6);   // flat well below
            expect (biquadMagnitudeDb (c, 16000.0, sr) < 1.0);                          // flat well above
        }
        beginTest ("0 dB is a flat unity response");
        {
            const auto c = eqLowShelf (200.0, 0.0, sr);
            expectWithinAbsoluteError (biquadMagnitudeDb (c, 100.0, sr), 0.0, 1e-6);
            expectWithinAbsoluteError (biquadMagnitudeDb (c, 5000.0, sr), 0.0, 1e-6);
        }
    }
};

struct EffectSyncTests : juce::UnitTest
{
    EffectSyncTests() : juce::UnitTest ("EffectSync") {}
    void runTest() override
    {
        beginTest ("synced rate = bpm / (60*beats); tracks tempo and beat division");
        {
            expectWithinAbsoluteError ((double) effectSyncedRate (120.0, 1.0f, 5.0f), 2.0, 1e-5);   // 1 beat @120 = 2 Hz
            expectWithinAbsoluteError ((double) effectSyncedRate (120.0, 0.5f, 5.0f), 4.0, 1e-5);   // 1/8 @120 = 4 Hz
            expectWithinAbsoluteError ((double) effectSyncedRate (120.0, 2.0f, 5.0f), 1.0, 1e-5);   // 2 beats = 1 Hz
            expectWithinAbsoluteError ((double) effectSyncedRate (240.0, 1.0f, 5.0f), 4.0, 1e-5);   // faster tempo -> faster
        }
        beginTest ("syncBeats 0 uses the free rate (clamped to 0.05..8)");
        {
            expectWithinAbsoluteError ((double) effectSyncedRate (120.0, 0.0f, 3.0f), 3.0, 1e-5);
            expectWithinAbsoluteError ((double) effectSyncedRate (120.0, 0.0f, 99.0f), 8.0, 1e-5);   // clamp high
            expectWithinAbsoluteError ((double) effectSyncedRate (120.0, 0.0f, 0.0f), 0.05, 1e-5);   // clamp low
        }
        beginTest ("synced rate clamps to 0.01..20 Hz");
        {
            expect (effectSyncedRate (600.0, 0.1f, 1.0f) <= 20.0f);   // 600/(60*0.1)=100 -> clamped
            expect (effectSyncedRate (1.0, 4.0f, 1.0f) >= 0.01f);
        }
    }
};

struct FadeShapeTests : juce::UnitTest
{
    FadeShapeTests() : juce::UnitTest ("FadeShape") {}

    void runTest() override
    {
        beginTest ("endpoints are 0 and 1 for every shape (no edge discontinuity)");
        {
            for (int s = 0; s <= 2; ++s)
            {
                expectWithinAbsoluteError (fadeShapeGain (s, 0.0f), 0.0f, 1e-6f);
                expectWithinAbsoluteError (fadeShapeGain (s, 1.0f), 1.0f, 1e-6f);
            }
        }

        beginTest ("linear is the identity");
        {
            expectWithinAbsoluteError (fadeShapeGain (0, 0.25f), 0.25f, 1e-6f);
            expectWithinAbsoluteError (fadeShapeGain (0, 0.5f),  0.5f,  1e-6f);
            expectWithinAbsoluteError (fadeShapeGain (0, 0.75f), 0.75f, 1e-6f);
        }

        beginTest ("equal-power is louder than linear in the interior; exponential is quieter");
        {
            for (float t : { 0.25f, 0.5f, 0.75f })
            {
                expect (fadeShapeGain (1, t) > fadeShapeGain (0, t));   // sin(t·π/2) > t
                expect (fadeShapeGain (2, t) < fadeShapeGain (0, t));   // t² < t
            }
            // equal-power midpoint is sin(π/4) = 1/√2 ≈ 0.7071 (constant-power law).
            expectWithinAbsoluteError (fadeShapeGain (1, 0.5f), 0.70710678f, 1e-6f);
            expectWithinAbsoluteError (fadeShapeGain (2, 0.5f), 0.25f, 1e-6f);
        }

        beginTest ("monotonic rising and clamped to [0,1] outside the range");
        {
            for (int s = 0; s <= 2; ++s)
            {
                float prev = -1.0f;
                for (int i = 0; i <= 20; ++i) { float g = fadeShapeGain (s, i / 20.0f); expect (g >= prev - 1e-6f); prev = g; }
                expectWithinAbsoluteError (fadeShapeGain (s, -0.5f), 0.0f, 1e-6f);   // clamp low
                expectWithinAbsoluteError (fadeShapeGain (s,  1.5f), 1.0f, 1e-6f);   // clamp high
            }
            // unknown shape falls back to linear.
            expectWithinAbsoluteError (fadeShapeGain (99, 0.3f), 0.3f, 1e-6f);
        }
    }
};

// ---- strong time types (Source/Time.h) --------------------------------------------------------
namespace {
using namespace gloopy::time;

template <typename A, typename B, typename = void> struct hasAdd : std::false_type {};
template <typename A, typename B>
struct hasAdd<A, B, std::void_t<decltype (std::declval<A>() + std::declval<B>())>> : std::true_type {};
template <typename A, typename B, typename = void> struct hasSub : std::false_type {};
template <typename A, typename B>
struct hasSub<A, B, std::void_t<decltype (std::declval<A>() - std::declval<B>())>> : std::true_type {};

// Legal combinations compile:
static_assert (hasAdd<BeatPosition, BeatDuration>::value, "position + duration must compile");
static_assert (hasSub<BeatPosition, BeatPosition>::value, "position - position must compile (=> duration)");
static_assert (hasAdd<BeatDuration, BeatDuration>::value, "duration + duration must compile");
static_assert (hasAdd<TimePosition, TimeDuration>::value, "time position + duration must compile");
// Illegal combinations are rejected at compile time (the whole point of the strong types):
static_assert (! hasAdd<BeatPosition, BeatPosition>::value, "position + position must NOT compile");
static_assert (! hasAdd<BeatPosition, double>::value,       "typed time + bare double must NOT compile");
static_assert (! hasAdd<BeatPosition, TimePosition>::value, "beats + seconds must NOT compile");
static_assert (! hasAdd<BeatDuration, TimeDuration>::value, "beat-duration + time-duration must NOT compile");
static_assert (! hasSub<BeatPosition, TimePosition>::value, "beats - seconds must NOT compile");
// Sample domain (discrete): same legal/illegal shape, and it does not mix with beats or seconds.
static_assert (  hasSub<SamplePosition, SamplePosition>::value, "sample position - position must compile (=> duration)");
static_assert (  hasAdd<SamplePosition, SampleDuration>::value, "sample position + duration must compile");
static_assert (! hasAdd<SamplePosition, SamplePosition>::value, "sample position + position must NOT compile");
static_assert (! hasAdd<SamplePosition, double>::value,         "samples + bare double must NOT compile");
static_assert (! hasAdd<SamplePosition, BeatPosition>::value,   "samples + beats must NOT compile");
static_assert (! hasAdd<SampleDuration, TimeDuration>::value,   "sample-duration + time-duration must NOT compile");
} // namespace

struct TimeTypesTests : juce::UnitTest
{
    TimeTypesTests() : juce::UnitTest ("TimeTypes") {}
    void runTest() override
    {
        using namespace gloopy::time;

        beginTest ("beat position / duration algebra");
        expectWithinAbsoluteError ((BeatPosition { 4.0 } - BeatPosition { 1.0 }).inBeats(), 3.0, 1e-12);
        expectWithinAbsoluteError ((BeatPosition { 1.0 } + BeatDuration { 2.0 }).inBeats(), 3.0, 1e-12);
        expectWithinAbsoluteError ((BeatDuration { 2.0 } + BeatPosition { 1.0 }).inBeats(), 3.0, 1e-12);   // commutes
        expectWithinAbsoluteError ((BeatDuration { 2.0 } * 1.5).inBeats(), 3.0, 1e-12);
        expectWithinAbsoluteError (BeatDuration { 6.0 } / BeatDuration { 2.0 }, 3.0, 1e-12);                // ratio
        expect (BeatPosition { 2.0 } < BeatPosition { 3.0 });
        expect (BeatPosition { 3.0 } == BeatPosition { 3.0 });
        expect (BeatPosition { 3.0 } != BeatPosition { 2.0 });

        beginTest ("time position / duration algebra (seconds)");
        expectWithinAbsoluteError ((TimePosition { 1.0 } + TimeDuration { 0.5 }).inSeconds(), 1.5, 1e-12);
        expectWithinAbsoluteError ((TimePosition { 2.0 } - TimePosition { 0.5 }).inSeconds(), 1.5, 1e-12);
        expect (TimeDuration { 0.5 } < TimeDuration { 1.0 });

        beginTest ("sample domain algebra (discrete)");
        expect ((SamplePosition { 44100 } - SamplePosition { 100 }).inSamples() == 44000);
        expect ((SamplePosition { 100 } + SampleDuration { 44000 }).inSamples() == 44100);
        expect ((SampleDuration { 22050 } * (long long) 2).inSamples() == 44100);
        expectWithinAbsoluteError (SampleDuration { 44100 } / SampleDuration { 22050 }, 2.0, 1e-12);   // ratio
        expect (SamplePosition { 100 } < SamplePosition { 200 });
        expect (SamplePosition { 200 } == SamplePosition { 200 });

        beginTest ("cross-domain conversions (constant tempo / rate)");
        // 120 bpm -> 1 beat = 0.5 s; at 48 kHz that's 24000 samples.
        expectWithinAbsoluteError (toSeconds (BeatPosition { 1.0 }, 120.0).inSeconds(), 0.5, 1e-12);
        expectWithinAbsoluteError (toBeats (TimePosition { 0.5 }, 120.0).inBeats(), 1.0, 1e-12);
        expect (toSamples (TimeDuration { 1.0 }, 44100.0).inSamples() == 44100);
        expectWithinAbsoluteError (toSeconds (SampleDuration { 44100 }, 44100.0).inSeconds(), 1.0, 1e-12);
        expect (toSamples (BeatPosition { 1.0 }, 120.0, 48000.0).inSamples() == 24000);          // beats -> samples
        expectWithinAbsoluteError (toBeats (SamplePosition { 24000 }, 120.0, 48000.0).inBeats(), 1.0, 1e-12);  // samples -> beats

        beginTest ("illegal combinations are rejected at compile time");
        expect (! hasAdd<BeatPosition, BeatPosition>::value, "position + position must be rejected");
        expect (! hasAdd<BeatPosition, TimePosition>::value, "beats + seconds must be rejected");
        expect (! hasAdd<BeatPosition, double>::value,       "typed + bare double must be rejected");
        expect (! hasAdd<SamplePosition, BeatPosition>::value, "samples + beats must be rejected");
        expect (! hasAdd<SamplePosition, SamplePosition>::value, "sample position + position must be rejected");
        expect (  hasSub<BeatPosition, BeatPosition>::value, "position - position must be allowed");
        expect (  hasAdd<BeatPosition, BeatDuration>::value, "position + duration must be allowed");
    }
};

static FadeShapeTests    fadeShapeTests;
static EffectSyncTests   effectSyncTests;
static BiquadEqTests     biquadEqTests;
static AllpassPhaserTests allpassPhaserTests;
static NotesJsonTests    notesJsonTests;
static ParamScaleTests   paramScaleTests;
static NoteSchedulerTests noteSchedulerTests;
static LfoTests          lfoTests;
static StereoWidenerTests stereoWidenerTests;
static TomlTests         tomlTests;
static SerializationTests serializationTests;
static NoteEditTests     noteEditTests;
static LiveArpTests      liveArpTests;
static SessionModelTests sessionModelTests;
static SessionLauncherTests sessionLauncherTests;
static SessionClipTests  sessionClipTests;
static FileDropTests     fileDropTests;
static TimeTypesTests    timeTypesTests;

int main (int, char**)
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    if (failures == 0) std::cout << "\nAll Gloopy unit tests passed.\n";
    else               std::cout << "\n" << failures << " unit-test failure(s).\n";

    return failures == 0 ? 0 : 1;
}
