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
#include "NotesJson.h"
#include "AllpassPhaser.h"
#include "Biquad.h"
#include "EffectSync.h"
#include "Onsets.h"
#include "ParamScale.h"
#include "FileDrop.h"
#include "StereoWiden.h"
#include "Lfo.h"
#include "FadeShape.h"
#include "Toml.h"

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
static FileDropTests     fileDropTests;

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
