// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Headless unit tests for Gloopy's pure logic. These deliberately avoid the
// GUI / audio-device machinery so they run fast in CI. They cover the pieces
// that have bitten us before: note scheduling / swing, and the ValueTree
// serialisation that save/load round-trips through.

#include <JuceHeader.h>
#include "Note.h"
#include "NoteScheduler.h"

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

        beginTest ("collectNotes places note-on/off in the right block");
        {
            const double spb = 100.0;                     // 100 samples per beat
            std::vector<Note> notes { { 60, 1.0, 2.0, 0.8f } };  // on@beat1 off@beat3

            juce::MidiBuffer midi;
            // Block covering samples [0, 400): should contain both on(100) and off(300).
            collectNotes (notes, midi, 0, 400, 0, spb);

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
            const double spb = 100.0;
            std::vector<Note> notes { { 60, 1.0, 2.0, 0.8f } };  // on@100 off@300

            juce::MidiBuffer midi;
            // Block [0,200): should have the note-on only.
            collectNotes (notes, midi, 0, 200, 0, spb);
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
            const double spb = 100.0;
            std::vector<Note> notes { { 60, 0.5, 0.5, 0.8f } };  // off-eighth note

            juce::MidiBuffer straight, swung;
            collectNotes (notes, straight, 0, 400, 0, spb, 0.5);
            collectNotes (notes, swung,    0, 400, 0, spb, 0.66);

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
static NoteSchedulerTests noteSchedulerTests;
static SerializationTests serializationTests;

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
