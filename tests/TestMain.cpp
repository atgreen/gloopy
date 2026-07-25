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
#include "FileDrop.h"
#include "StereoWiden.h"
#include "Lfo.h"
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
