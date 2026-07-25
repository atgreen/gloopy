// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Audio input recording (design: recording.md). Phase 1 = one take -> referencing
// clip. Phase 2 = multiple armed tracks, per-track monitoring, count-in + punch
// range, and recording events on Subscribe. Real-time discipline: the audio
// thread only copies input samples into JUCE ThreadedWriters (bounded FIFO +
// background writer thread); the message thread opens/closes writers, gates by
// the punch range, and creates clips on stop.

#include "MainComponent.h"
#include <iostream>

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
    auto dir = recordingsDir();
    dir.createDirectory();

    std::vector<std::unique_ptr<TakeRecorder>> recs;
    juce::WavAudioFormat fmt;
    for (auto* t : armed)
    {
        const int nch = juce::jlimit (1, 2, t->recordChannels.load());
        auto file = dir.getNonexistentChildFile (takeSlug (t->name), ".wav", false);
        std::unique_ptr<juce::FileOutputStream> os (file.createOutputStream());
        if (os == nullptr) { std::cout << "[rec] cannot write " << file.getFullPathName() << std::endl;
                             emitChange ("recording_error", t->id); continue; }
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
        finalizeTake (*r);
        emitChange ("recording_stopped", r->trackId);
    }
}

void MainComponent::finalizeTake (const TakeRecorder& rec)
{
    Track* t = resolveTrack (rec.trackId);
    if (t == nullptr) return;

    std::unique_ptr<juce::AudioFormatReader> r (formatManager.createReaderFor (rec.file));
    if (r == nullptr || r->lengthInSamples <= 0) return;

    auto buf = std::make_shared<juce::AudioBuffer<float>> ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (buf.get(), 0, (int) r->lengthInSamples, 0, true, true);
    const double durSec = (double) r->lengthInSamples / r->sampleRate;

    pushUndoSnapshot();
    Clip c;
    c.type            = ClipType::Audio;
    c.name            = rec.file.getFileNameWithoutExtension();
    c.startBeat       = rec.startBeat;
    c.lengthBeats     = juce::jmax (0.25, durSec * transport.getBpm() / 60.0);
    c.audio           = buf;
    c.audioSourceRate = r->sampleRate;
    c.audioGain       = 1.0f;
    c.peaks           = std::make_shared<std::vector<float>> (buildPeaks (*buf));
    c.audioFile       = portableSamplePath (rec.file.getFullPathName());
    c.takeId          = rec.file.getFileNameWithoutExtension();
    const auto ref      = c.audioFile;
    const auto lenBeats = c.lengthBeats;
    { const juce::ScopedLock sl (engineLock); t->clips.push_back (std::move (c)); }

    if (currentProjectFile.getFileName() == "gloopy.toml")
    {
        auto tf = currentProjectFile.getParentDirectory().getChildFile ("recordings").getChildFile ("takes.toml");
        tf.getParentDirectory().createDirectory();
        juce::String block;
        block << "\n[[takes]]\n"
              << "id = \"" << rec.file.getFileNameWithoutExtension() << "\"\n"
              << "file = \"" << ref << "\"\n"
              << "track = \"" << t->name << "\"\n"
              << "channels = " << (int) r->numChannels << "\n"
              << "sample_rate = " << (int) r->sampleRate << "\n"
              << "start_beat = " << juce::String (rec.startBeat, 6) << "\n"
              << "length_beats = " << juce::String (lenBeats, 6) << "\n"
              << "latency_compensation_seconds = 0.0\n";
        tf.appendText (block);
    }

    emitChange ("take_created", rec.trackId);
    std::cout << "[rec] take " << rec.file.getFileName() << " -> clip on '" << t->name
              << "' (" << juce::String (durSec, 2) << "s @ beat " << juce::String (rec.startBeat, 2) << ")" << std::endl;
    if (arrangeView) arrangeView->rebuild();
}
