// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Phase 1 of audio input recording (design: recording.md). Recording writes an
// ordinary WAV take under the composition's assets/recordings/ and drops a
// *referencing* audio clip (no embedded blob). The real-time discipline matches
// the design: the audio thread only copies input samples into a JUCE
// ThreadedWriter (bounded FIFO + background writer thread); the message thread
// opens/closes writers and creates clips.

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

// Where recorded takes live: a composition's assets/recordings/, else a folder
// beside the .gloopy project, else ~/Gloopy Recordings.
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

bool MainComponent::apiArmTrack (int trackId, bool armed, int input, int channels)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        t->recordInput.store (juce::jmax (0, input));
        t->recordChannels.store (juce::jlimit (1, 2, channels));
        t->recordArmed.store (armed);
        return true;
    });
}

// --- message thread: open a take writer for the first armed audio track -------
bool MainComponent::startAudioRecording()
{
    Track* target = nullptr;
    for (auto& t : tracks) if (t->recordArmed.load()) { target = t.get(); break; }
    if (target == nullptr) return false;

    const int    nch  = juce::jlimit (1, 2, target->recordChannels.load());
    const double rate = currentSampleRate;

    auto dir = recordingsDir();
    dir.createDirectory();
    const auto file = dir.getNonexistentChildFile (takeSlug (target->name), ".wav", false);

    std::unique_ptr<juce::FileOutputStream> os (file.createOutputStream());
    if (os == nullptr) { std::cout << "[rec] cannot write take to " << file.getFullPathName() << std::endl; return false; }
    juce::WavAudioFormat fmt;
    auto* writer = fmt.createWriterFor (os.get(), rate, (unsigned) nch, 24, {}, 0);
    if (writer == nullptr) return false;
    os.release();

    if (! recordThread.isThreadRunning()) recordThread.startThread();
    audioRecFrames.store (0);
    recordTonePhase = 0.0;
    {
        const juce::ScopedLock sl (takeWriterLock);
        audioTakeWriter.reset (new juce::AudioFormatWriter::ThreadedWriter (writer, recordThread, 1 << 16));
    }
    audioRecTrackId   = target->id;
    audioRecChannels  = nch;
    audioRecInput     = juce::jmax (0, target->recordInput.load());
    audioRecRate      = rate;
    audioRecStartBeat = transport.getPlayheadBeats();
    audioTakeFile     = file;
    std::cout << "[rec] recording " << nch << "ch take -> " << file.getFullPathName() << std::endl;
    return true;
}

// --- audio thread: copy input (or a test tone) into the writer's FIFO ----------
void MainComponent::captureRecordingInput (const juce::AudioSourceChannelInfo& info)
{
    const juce::ScopedTryLock sl (takeWriterLock);
    if (! sl.isLocked() || audioTakeWriter == nullptr) return;

    const int num = info.numSamples;
    const int nch = audioRecChannels;
    auto* buf = info.buffer;

    const double toneHz = recordTestToneHz.load();
    const float* chans[2] = { nullptr, nullptr };
    juce::AudioBuffer<float> tone;

    if (toneHz > 0.0)                       // self-test signal in place of the mic
    {
        tone.setSize (nch, num);
        for (int i = 0; i < num; ++i)
        {
            const float s = 0.5f * (float) std::sin (recordTonePhase * juce::MathConstants<double>::twoPi);
            for (int ch = 0; ch < nch; ++ch) tone.setSample (ch, i, s);
            recordTonePhase += toneHz / audioRecRate;
            if (recordTonePhase >= 1.0) recordTonePhase -= 1.0;
        }
        for (int ch = 0; ch < nch; ++ch) chans[ch] = tone.getReadPointer (ch);
    }
    else
    {
        for (int ch = 0; ch < nch; ++ch)
        {
            const int src = juce::jmin (audioRecInput + ch, buf->getNumChannels() - 1);
            chans[ch] = buf->getReadPointer (src, info.startSample);
        }
    }
    if (audioTakeWriter->write (chans, num))
        audioRecFrames.fetch_add (num);
}

// --- message thread: close the writer, create the referencing clip -------------
void MainComponent::stopAudioRecording()
{
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> w;
    { const juce::ScopedLock sl (takeWriterLock); w = std::move (audioTakeWriter); }
    if (w == nullptr) return;
    w.reset();                              // flush FIFO + close the WAV

    const int frames = audioRecFrames.load();
    if (frames <= 0)                        // never captured a sample — no empty clip
    {
        audioTakeFile.deleteFile();
        std::cout << "[rec] empty take discarded" << std::endl;
        return;
    }
    finalizeTake (audioRecTrackId, audioTakeFile, audioRecChannels, audioRecStartBeat);
}

void MainComponent::finalizeTake (int trackId, const juce::File& take, int /*channels*/, double startBeat)
{
    Track* t = resolveTrack (trackId);
    if (t == nullptr) return;

    std::unique_ptr<juce::AudioFormatReader> r (formatManager.createReaderFor (take));
    if (r == nullptr || r->lengthInSamples <= 0) return;

    auto buf = std::make_shared<juce::AudioBuffer<float>> ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (buf.get(), 0, (int) r->lengthInSamples, 0, true, true);
    const double durSec = (double) r->lengthInSamples / r->sampleRate;

    pushUndoSnapshot();
    Clip c;
    c.type            = ClipType::Audio;
    c.name            = take.getFileNameWithoutExtension();
    c.startBeat       = startBeat;
    c.lengthBeats     = juce::jmax (0.25, durSec * transport.getBpm() / 60.0);
    c.audio           = buf;
    c.audioSourceRate = r->sampleRate;
    c.audioGain       = 1.0f;
    c.peaks           = std::make_shared<std::vector<float>> (buildPeaks (*buf));
    c.audioFile       = portableSamplePath (take.getFullPathName());   // reference, not embed
    c.takeId          = take.getFileNameWithoutExtension();
    const auto ref    = c.audioFile;                                   // capture before the move
    const auto lenBeats = c.lengthBeats;
    { const juce::ScopedLock sl (engineLock); t->clips.push_back (std::move (c)); }

    // Take metadata (recordings/takes.toml) — only for a composition project.
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
              << "length_beats = " << juce::String (lenBeats, 6) << "\n"
              << "latency_compensation_seconds = 0.0\n";
        tf.appendText (block);
    }

    std::cout << "[rec] take " << take.getFileName() << " -> clip on '" << t->name
              << "' (" << juce::String (durSec, 2) << "s)" << std::endl;
    if (arrangeView) arrangeView->rebuild();
}
