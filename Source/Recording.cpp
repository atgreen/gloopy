// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Audio input recording (design: recording.md). Phase 1 = one take -> referencing
// clip. Phase 2 = multiple armed tracks, per-track monitoring, count-in + punch
// range, and recording events on Subscribe. Real-time discipline: the audio
// thread only copies input samples into JUCE ThreadedWriters (bounded FIFO +
// background writer thread); the message thread opens/closes writers, gates by
// the punch range, and creates clips on stop.

#include "MainComponent.h"
#include "EngineLock.h"
#include <iostream>
#include <set>

namespace
{
juce::String takeSlug (const juce::String& name)
{
    juce::String s;
    for (auto c : name.toLowerCase())
        s << (juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (c) : juce::String ("-"));
    while (s.contains ("--")) s = s.replace ("--", "-");
    s = s.trimCharactersAtStart ("-").trimCharactersAtEnd ("-");
    return s.isEmpty() ? "take" : s;
}
}

juce::File MainComponent::recordingsDir() const
{
    if (currentProjectFile.getFileName() == "gloopy.toml")
        return currentProjectFile.getParentDirectory().getChildFile ("assets").getChildFile ("recordings");
    if (currentProjectFile.existsAsFile())
        return currentProjectFile.getParentDirectory()
                 .getChildFile (currentProjectFile.getFileNameWithoutExtension() + "-recordings");
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Gloopy Recordings");
}

// New takes go into raw/ (scratch, git-ignored by default); PromoteTake moves a
// keeper up into the recordings dir.
juce::File MainComponent::rawTakesDir() const { return recordingsDir().getChildFile ("raw"); }

double MainComponent::recordLatencySeconds() const
{
    double s = recordLatencyOffset.load();
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        s += (double) dev->getInputLatencyInSamples() / juce::jmax (1.0, dev->getCurrentSampleRate());
    return juce::jmax (0.0, s);
}

std::vector<juce::String> MainComponent::apiListAudioInputs()
{
    std::vector<juce::String> out;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        const auto names = dev->getInputChannelNames();
        const auto active = dev->getActiveInputChannels();
        for (int i = 0; i < names.size(); ++i)
            if (active[i]) out.push_back (names[i]);
    }
    return out;
}

bool MainComponent::apiArmTrack (int trackId, bool armed, int input, int channels, bool monitor)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        t->recordInput.store (juce::jmax (0, input));
        t->recordChannels.store (juce::jlimit (1, 2, channels));
        t->recordMonitor.store (monitor);
        t->recordArmed.store (armed);
        return true;
    });
}

bool MainComponent::apiSetPunchRange (bool enabled, double inBeat, double outBeat, double countIn)
{
    return callOnMessageThread ([&] () -> bool
    {
        punchEnabled.store (enabled);
        punchInBeat.store (juce::jmax (0.0, inBeat));
        punchOutBeat.store (outBeat > inBeat ? outBeat : 1.0e12);
        countInBeats.store (juce::jmax (0.0, countIn));
        return true;
    });
}

// --- message thread: open a take writer for every armed audio track ------------
bool MainComponent::startAudioRecording()
{
    std::vector<Track*> armed;
    for (auto& t : tracks) if (t->recordArmed.load()) armed.push_back (t.get());
    if (armed.empty()) return false;

    // Anchor = where takes/clips begin. Punch-in if enabled, else the playhead.
    const double anchor = punchEnabled.load() ? punchInBeat.load() : transport.getPlayheadBeats();
    if (! punchEnabled.load())
    {
        punchInBeat.store (anchor);
        punchOutBeat.store (1.0e12);
    }
    // Count-in: rewind so the performer hears lead-in; writing still starts at anchor.
    const double countIn = countInBeats.load();
    const double spb = juce::jmax (1.0, transport.samplesPerBeat());
    transport.setPlayheadSamples ((juce::int64) (juce::jmax (0.0, anchor - countIn) * spb));

    const double rate = currentSampleRate;
    auto dir = rawTakesDir();
    dir.createDirectory();

    const bool flac = recordFormat.load() == 1;
    const juce::String ext = flac ? ".flac" : ".wav";

    std::vector<std::unique_ptr<TakeRecorder>> recs;
    juce::WavAudioFormat  wav;
    juce::FlacAudioFormat flacFmt;
    for (auto* t : armed)
    {
        const int nch = juce::jlimit (1, 2, t->recordChannels.load());
        auto file = dir.getNonexistentChildFile (takeSlug (t->name), ext, false);
        std::unique_ptr<juce::FileOutputStream> os (file.createOutputStream());
        if (os == nullptr) { std::cout << "[rec] cannot write " << file.getFullPathName() << std::endl;
                             emitChange ("recording_error", t->id); continue; }
        juce::AudioFormat& fmt = flac ? (juce::AudioFormat&) flacFmt : (juce::AudioFormat&) wav;
        auto* writer = fmt.createWriterFor (os.get(), rate, (unsigned) nch, 24, {}, 0);
        if (writer == nullptr) continue;
        os.release();

        auto r = std::make_unique<TakeRecorder>();
        r->trackId = t->id; r->channels = nch; r->input = juce::jmax (0, t->recordInput.load());
        r->file = file; r->startBeat = anchor;
        if (! recordThread.isThreadRunning()) recordThread.startThread();
        r->writer.reset (new juce::AudioFormatWriter::ThreadedWriter (writer, recordThread, 1 << 16));
        recs.push_back (std::move (r));
    }
    if (recs.empty()) return false;

    audioRecRate = rate;
    recordTonePhase = 0.0;
    lastRecPlayheadBeat = -1.0;
    loopRecRotate.store (false);
    {
        const juce::ScopedLock sl (takeWriterLock);
        takeRecorders = std::move (recs);
        audioRecActive.store (true);
    }
    for (auto& r : takeRecorders) emitChange ("recording_started", r->trackId);
    std::cout << "[rec] recording " << takeRecorders.size() << " armed track(s), anchor beat "
              << juce::String (anchor, 2) << (countIn > 0 ? ", count-in " + juce::String (countIn, 1) : "")
              << std::endl;
    return true;
}

// --- audio thread: gate by punch range, copy input into each writer ------------
void MainComponent::captureRecordingInput (const juce::AudioSourceChannelInfo& info)
{
    // Stash the raw input for monitoring before the buffer is cleared/overwritten.
    auto* buf = info.buffer;
    const int num = info.numSamples;
    monitorStash.setSize (buf->getNumChannels(), num, false, false, true);
    for (int ch = 0; ch < buf->getNumChannels(); ++ch)
        monitorStash.copyFrom (ch, 0, *buf, ch, info.startSample, num);

    const juce::ScopedTryLock sl (takeWriterLock);
    if (! sl.isLocked() || ! audioRecActive.load()) return;

    // Within the punch range at this block?
    const double beat = transport.getPlayheadBeats();
    const bool writing = beat >= punchInBeat.load() && beat < punchOutBeat.load();

    // Loop recording: the playhead wrapping back signals a new take pass.
    if (transport.isLoopEnabled() && lastRecPlayheadBeat >= 0.0 && beat + 0.25 < lastRecPlayheadBeat)
        loopRecRotate.store (true);
    lastRecPlayheadBeat = beat;

    // Optional self-test tone in place of the mic (2ch max).
    juce::AudioBuffer<float> tone;
    const double toneHz = recordTestToneHz.load();
    if (toneHz > 0.0)
    {
        tone.setSize (2, num);
        for (int i = 0; i < num; ++i)
        {
            const float s = 0.5f * (float) std::sin (recordTonePhase * juce::MathConstants<double>::twoPi);
            tone.setSample (0, i, s); tone.setSample (1, i, s);
            recordTonePhase += toneHz / audioRecRate;
            if (recordTonePhase >= 1.0) recordTonePhase -= 1.0;
        }
    }

    if (! writing) return;
    for (auto& r : takeRecorders)
    {
        const float* chans[2] = { nullptr, nullptr };
        for (int ch = 0; ch < r->channels; ++ch)
            chans[ch] = toneHz > 0.0 ? tone.getReadPointer (juce::jmin (ch, 1))
                                     : buf->getReadPointer (juce::jmin (r->input + ch, buf->getNumChannels() - 1), info.startSample);
        if (r->writer != nullptr && r->writer->write (chans, num))
            r->frames.fetch_add (num);
    }
}

// --- audio thread: dry input monitoring for armed+monitor tracks ---------------
void MainComponent::addMonitoring (const juce::AudioSourceChannelInfo& info)
{
    if (monitorStash.getNumChannels() == 0) return;
    auto* out = info.buffer;
    const int num = info.numSamples;
    for (auto& t : tracks)
    {
        if (! (t->recordArmed.load() && t->recordMonitor.load())) continue;
        const float g   = t->volume.load();
        const int   in0 = juce::jmax (0, t->recordInput.load());
        const int   nch = juce::jlimit (1, 2, t->recordChannels.load());
        for (int outCh = 0; outCh < juce::jmin (2, out->getNumChannels()); ++outCh)
        {
            const int src = juce::jmin (in0 + (nch == 1 ? 0 : outCh), monitorStash.getNumChannels() - 1);
            if (src >= 0)
                out->addFrom (outCh, info.startSample, monitorStash, src, 0, num, g);
        }
    }
}

// --- message thread: close writers, create clips -------------------------------
void MainComponent::stopAudioRecording()
{
    std::vector<std::unique_ptr<TakeRecorder>> recs;
    {
        const juce::ScopedLock sl (takeWriterLock);
        audioRecActive.store (false);
        recs = std::move (takeRecorders);
    }
    if (recs.empty()) return;

    for (auto& r : recs)
    {
        r->writer.reset();                  // flush FIFO + close the WAV
        if (r->frames.load() <= 0)          // never captured a sample — no empty clip
        {
            r->file.deleteFile();
            std::cout << "[rec] empty take discarded (" << r->file.getFileName() << ")" << std::endl;
            emitChange ("recording_stopped", r->trackId);
            continue;
        }
        finalizeTake (r->trackId, r->file, r->startBeat, /*muted*/ false);
        emitChange ("recording_stopped", r->trackId);
    }
}

void MainComponent::finalizeTake (int trackId, const juce::File& take, double startBeat, bool muted)
{
    Track* t = resolveTrack (trackId);
    if (t == nullptr) return;

    std::unique_ptr<juce::AudioFormatReader> r (formatManager.createReaderFor (take));
    if (r == nullptr || r->lengthInSamples <= 0) return;

    auto buf = std::make_shared<juce::AudioBuffer<float>> ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (buf.get(), 0, (int) r->lengthInSamples, 0, true, true);
    const double durSec = (double) r->lengthInSamples / r->sampleRate;

    // Latency compensation: input arrives late, so shift the clip earlier.
    const double latSec   = recordLatencySeconds();
    const double latBeats = latSec * transport.getBpm() / 60.0;

    pushUndoSnapshot();
    Clip c;
    c.type            = ClipType::Audio;
    c.name            = take.getFileNameWithoutExtension();
    c.startBeat       = juce::jmax (0.0, startBeat - latBeats);
    c.lengthBeats     = juce::jmax (0.25, durSec * transport.getBpm() / 60.0);
    c.audio           = buf;
    c.audioSourceRate = r->sampleRate;
    c.audioGain       = 1.0f;
    c.muted           = muted;   // loop-recording: earlier passes are inactive take lanes
    c.peaks           = std::make_shared<std::vector<float>> (buildPeaks (*buf));
    c.audioFile       = portableSamplePath (take.getFullPathName());
    c.takeId          = take.getFileNameWithoutExtension();
    const auto ref      = c.audioFile;
    const auto lenBeats = c.lengthBeats;
    { GLOOPY_ELOCK(sl); t->clips.push_back (std::move (c)); }

    if (currentProjectFile.getFileName() == "gloopy.toml")
    {
        auto tf = currentProjectFile.getParentDirectory().getChildFile ("recordings").getChildFile ("takes.toml");
        tf.getParentDirectory().createDirectory();
        juce::String block;
        block << "\n[[takes]]\n"
              << "id = \"" << take.getFileNameWithoutExtension() << "\"\n"
              << "file = \"" << ref << "\"\n"
              << "track = \"" << t->name << "\"\n"
              << "channels = " << (int) r->numChannels << "\n"
              << "sample_rate = " << (int) r->sampleRate << "\n"
              << "start_beat = " << juce::String (startBeat, 6) << "\n"
              << "length_beats = " << juce::String (lenBeats.toBeats(), 6) << "\n"
              << "latency_compensation_seconds = " << juce::String (latSec, 6) << "\n";
        tf.appendText (block);
    }

    emitChange ("take_created", trackId);
    std::cout << "[rec] take " << take.getFileName() << " -> " << (muted ? "muted " : "") << "clip on '" << t->name
              << "' (" << juce::String (durSec, 2) << "s @ beat " << juce::String (startBeat, 2) << ")" << std::endl;
    if (arrangeView) arrangeView->rebuild();
}

// Loop recording: each loop pass closes its take (as a muted alternate) and opens
// a fresh take per recorder, all stacked at the same anchor (take lanes).
void MainComponent::rotateLoopTakes()
{
    struct Done { int trackId; juce::File file; double startBeat; };
    std::vector<Done> done;
    const bool flac = recordFormat.load() == 1;
    const juce::String ext = flac ? ".flac" : ".wav";
    const double rate = audioRecRate;
    juce::WavAudioFormat wav; juce::FlacAudioFormat flacFmt;
    auto dir = rawTakesDir();

    {
        const juce::ScopedLock sl (takeWriterLock);
        for (auto& r : takeRecorders)
        {
            r->writer.reset();                              // close the completed pass
            if (r->frames.load() > 0) done.push_back ({ r->trackId, r->file, r->startBeat });
            // open a fresh take for the next pass
            auto* t = resolveTrack (r->trackId);
            const juce::String name = t ? t->name : juce::String ("take");
            auto file = dir.getNonexistentChildFile (name.toLowerCase().replaceCharacter (' ', '-'), ext, false);
            std::unique_ptr<juce::FileOutputStream> os (file.createOutputStream());
            if (os == nullptr) continue;
            juce::AudioFormat& fmt = flac ? (juce::AudioFormat&) flacFmt : (juce::AudioFormat&) wav;
            if (auto* w = fmt.createWriterFor (os.get(), rate, (unsigned) r->channels, 24, {}, 0))
            {
                os.release();
                r->file = file; r->frames.store (0);
                r->writer.reset (new juce::AudioFormatWriter::ThreadedWriter (w, recordThread, 1 << 16));
            }
        }
    }
    // Finalize completed passes as muted alternates (outside the writer lock).
    for (auto& d : done) finalizeTake (d.trackId, d.file, d.startBeat, /*muted*/ true);
}

// ── Phase 3: settings / take management ──────────────────────────────────────
bool MainComponent::apiSetRecordSettings (int format, double latencyOffsetSeconds)
{
    recordFormat.store (juce::jlimit (0, 1, format));
    recordLatencyOffset.store (latencyOffsetSeconds);
    return true;
}

// Move a scratch take from raw/ up into the recordings dir and repoint its clip(s).
bool MainComponent::apiPromoteTake (const juce::String& takeId)
{
    return callOnMessageThread ([&] () -> bool
    {
        juce::File src;
        for (auto& f : rawTakesDir().findChildFiles (juce::File::findFiles, false))
            if (f.getFileNameWithoutExtension() == takeId) { src = f; break; }
        if (! src.existsAsFile()) return false;

        recordingsDir().createDirectory();
        auto dest = recordingsDir().getChildFile (src.getFileName());
        if (! src.moveFileTo (dest)) return false;
        const auto newRef = portableSamplePath (dest.getFullPathName());

        GLOOPY_ELOCK(sl);
        for (auto& t : tracks)
            for (auto& c : t->clips)
                if (c.takeId == takeId) c.audioFile = newRef;
        std::cout << "[rec] promoted take " << takeId << " -> " << dest.getFullPathName() << std::endl;
        if (arrangeView) arrangeView->repaint();
        return true;
    });
}

// Delete take files (raw/ + recordings/) that no clip references.
int MainComponent::apiCleanupTakes()
{
    return callOnMessageThread ([&] () -> int
    {
        std::set<juce::String> referenced;
        { GLOOPY_ELOCK(sl);
          for (auto& t : tracks) for (auto& c : t->clips)
              if (c.takeId.isNotEmpty()) referenced.insert (c.takeId); }

        int removed = 0;
        for (auto d : { rawTakesDir(), recordingsDir() })
            for (auto& f : d.findChildFiles (juce::File::findFiles, false))
                if ((f.hasFileExtension ("wav") || f.hasFileExtension ("flac"))
                    && referenced.find (f.getFileNameWithoutExtension()) == referenced.end())
                    if (f.deleteFile()) ++removed;
        std::cout << "[rec] cleanup removed " << removed << " unreferenced take(s)" << std::endl;
        return removed;
    });
}

// Create clips for take files not referenced by any clip (crash recovery).
int MainComponent::apiRecoverTakes()
{
    return callOnMessageThread ([&] () -> int
    {
        std::set<juce::String> referenced;
        { GLOOPY_ELOCK(sl);
          for (auto& t : tracks) for (auto& c : t->clips)
              if (c.takeId.isNotEmpty()) referenced.insert (c.takeId); }

        Track* target = nullptr;
        for (auto& t : tracks) if (t->type == TrackType::Audio) { target = t.get(); break; }

        int recovered = 0;
        for (auto d : { rawTakesDir(), recordingsDir() })
            for (auto& f : d.findChildFiles (juce::File::findFiles, false))
            {
                if (! (f.hasFileExtension ("wav") || f.hasFileExtension ("flac"))) continue;
                if (referenced.count (f.getFileNameWithoutExtension())) continue;
                std::unique_ptr<juce::AudioFormatReader> r (formatManager.createReaderFor (f));
                if (r == nullptr || r->lengthInSamples <= 0) continue;

                if (target == nullptr)
                {
                    auto t = std::make_unique<Track> ("Recovered", nullptr, 60,
                                 paletteColour ((int) tracks.size()), TrackType::Audio);
                    target = t.get();
                    addTrack (std::move (t));
                }
                auto buf = std::make_shared<juce::AudioBuffer<float>> ((int) r->numChannels, (int) r->lengthInSamples);
                r->read (buf.get(), 0, (int) r->lengthInSamples, 0, true, true);
                Clip c;
                c.type = ClipType::Audio; c.name = f.getFileNameWithoutExtension();
                c.startBeat = 0.0;
                c.lengthBeats = juce::jmax (0.25, (double) r->lengthInSamples / r->sampleRate * transport.getBpm() / 60.0);
                c.audio = buf; c.audioSourceRate = r->sampleRate; c.audioGain = 1.0f;
                c.peaks = std::make_shared<std::vector<float>> (buildPeaks (*buf));
                c.audioFile = portableSamplePath (f.getFullPathName());
                c.takeId = f.getFileNameWithoutExtension();
                { GLOOPY_ELOCK(sl); target->clips.push_back (std::move (c)); }
                ++recovered;
            }
        if (recovered > 0 && arrangeView) arrangeView->rebuild();
        std::cout << "[rec] recovered " << recovered << " orphan take(s)" << std::endl;
        return recovered;
    });
}
