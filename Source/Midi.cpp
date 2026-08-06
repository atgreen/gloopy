// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Standard MIDI file (SMF) import/export — interop with other DAWs and a fast path
// for generative tooling that speaks .mid. Gloopy's note model is beat-based
// (Note.startBeat/lengthBeats), so the mapping is just beats <-> ticks via the
// file's ticks-per-quarter-note. Export writes a Type-1 file (one track per Gloopy
// instrument track + a tempo track); import creates one synth track + clip per MIDI
// track that carries notes, reusing the same apiAddSynthTrack/apiAddClip paths the
// GUI and gRPC use.

#include "MainComponent.h"
#include "EngineLock.h"
#include <cmath>

namespace
{
juce::File resolveOut (const juce::String& path)
{
    return juce::File::isAbsolutePath (path) ? juce::File (path)
             : juce::File::getCurrentWorkingDirectory().getChildFile (path);
}
constexpr int kTPQN = 960;   // export resolution
}

bool MainComponent::apiExportMidi (const juce::String& path)
{
    return callOnMessageThread ([&] () -> bool
    {
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (kTPQN);

        // Tempo track first.
        juce::MidiMessageSequence tempoSeq;
        auto tempo = juce::MidiMessage::tempoMetaEvent ((int) (60'000'000.0 / juce::jmax (1.0, transport.getBpm())));
        tempo.setTimeStamp (0.0);
        tempoSeq.addEvent (tempo);
        mf.addTrack (tempoSeq);

        int noteCount = 0;
        {
            GLOOPY_ELOCK(sl);
            for (auto& t : tracks)
            {
                if (t->type != TrackType::Instrument) continue;
                juce::MidiMessageSequence seq;
                auto nameEv = juce::MidiMessage::textMetaEvent (3, t->name);   // 3 = track name
                nameEv.setTimeStamp (0.0);
                seq.addEvent (nameEv);

                // Emit a note-on/off pair at absolute beats.
                auto emit = [&] (double onBeat, int pitch, float velocity, double len)
                {
                    const int vel = juce::jlimit (1, 127, (int) std::lround (velocity * 127.0f));
                    auto on = juce::MidiMessage::noteOn (1, pitch, (juce::uint8) vel);
                    on.setTimeStamp (onBeat * kTPQN);
                    auto off = juce::MidiMessage::noteOff (1, pitch);
                    off.setTimeStamp ((onBeat + juce::jmax (0.01, len)) * kTPQN);
                    seq.addEvent (on); seq.addEvent (off);
                    ++noteCount;
                };

                for (auto& c : t->clips)
                {
                    if (c.isAudio()) continue;
                    // A LOOPED clip tiles its content window across its arrangement length, so the
                    // export matches playback (previously only one iteration was written). Notes are
                    // clamped so they don't ring past the clip end. A one-shot clip emits once.
                    if (c.looped && c.contentLenBeats > 0.0 && c.contentLenBeats < c.lengthBeats - 1.0e-6)
                    {
                        for (double off = 0.0; off < c.lengthBeats - 1.0e-6; off += c.contentLenBeats.toBeats())
                            for (auto& n : c.notes)
                            {
                                const double localOn = off + n.startBeat.toBeats();
                                if (localOn >= c.lengthBeats - 1.0e-6) continue;   // starts at/after the clip end
                                emit (c.startBeat.toBeats() + localOn, n.pitch, n.velocity,
                                      juce::jmin (n.lengthBeats.toBeats(), c.lengthBeats.toBeats() - localOn));
                            }
                    }
                    else
                        for (auto& n : c.notes)
                            emit (c.startBeat.toBeats() + n.startBeat.toBeats(), n.pitch, n.velocity, n.lengthBeats.toBeats());
                }
                seq.updateMatchedPairs();
                mf.addTrack (seq);
            }
        }

        auto f = resolveOut (path);
        f.deleteFile();
        auto os = f.createOutputStream();
        if (os == nullptr) return false;
        const bool ok = mf.writeTo (*os, 1);   // Type-1
        std::cout << "[midi] exported " << noteCount << " notes -> " << f.getFullPathName() << std::endl;
        return ok;
    });
}

int MainComponent::apiImportMidi (const juce::String& path)
{
    auto f = resolveOut (path);
    if (! f.existsAsFile()) return -1;
    auto is = f.createInputStream();
    if (is == nullptr) return -1;

    juce::MidiFile mf;
    if (! mf.readFrom (*is)) return -1;
    const int tf = mf.getTimeFormat();
    const double tpqn = tf > 0 ? (double) tf : (double) kTPQN;   // SMPTE unsupported -> assume PPQ

    return callOnMessageThread ([&] () -> int
    {
        // Tempo: first tempo meta anywhere in the file.
        for (int i = 0; i < mf.getNumTracks() && true; ++i)
        {
            auto* seq = mf.getTrack (i);
            for (int e = 0; e < seq->getNumEvents(); ++e)
            {
                const auto& m = seq->getEventPointer (e)->message;
                if (m.isTempoMetaEvent())
                {
                    const double spqn = m.getTempoSecondsPerQuarterNote();
                    if (spqn > 0.0) transport.setBpm (juce::jlimit (20.0, 400.0, 60.0 / spqn));
                    i = mf.getNumTracks();   // stop outer loop
                    break;
                }
            }
        }

        int imported = 0;
        for (int i = 0; i < mf.getNumTracks(); ++i)
        {
            juce::MidiMessageSequence seq (*mf.getTrack (i));   // copy so we can match pairs
            seq.updateMatchedPairs();

            std::vector<Note> notes;
            double maxEnd = 0.0;
            juce::String name;
            for (int e = 0; e < seq.getNumEvents(); ++e)
            {
                auto* ev = seq.getEventPointer (e);
                const auto& m = ev->message;
                if (m.isTrackNameEvent() && name.isEmpty()) name = m.getTextFromTextMetaEvent();
                if (m.isNoteOn() && ev->noteOffObject != nullptr)
                {
                    const double on  = m.getTimeStamp() / tpqn;
                    const double off = ev->noteOffObject->message.getTimeStamp() / tpqn;
                    Note n;
                    n.pitch       = m.getNoteNumber();
                    n.startBeat   = juce::jmax (0.0, on);
                    n.lengthBeats = juce::jmax (0.0625, off - on);
                    n.velocity    = m.getFloatVelocity();
                    notes.push_back (n);
                    maxEnd = juce::jmax (maxEnd, off);
                }
            }
            if (notes.empty()) continue;
            if (name.isEmpty()) name = "MIDI " + juce::String (i + 1);

            const int tid = apiAddSynthTrack (name, 1, 0.01f, 0.1f, 0.8f, 0.2f, 0.8f);
            if (tid < 0) continue;
            apiAddClip (tid, 0.0, maxEnd, maxEnd, false, notes, name);
            ++imported;
        }
        std::cout << "[midi] imported " << imported << " track(s) from " << f.getFullPathName() << std::endl;
        return imported;
    });
}
