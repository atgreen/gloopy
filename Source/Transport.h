// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cmath>

/** Owns the musical clock: tempo, loop length, play state and the playhead.
    All fields are atomic so the GUI thread can read/write them while the audio
    thread advances the playhead. The audio thread is the sole writer of the
    playhead position (except for the reset request). */
class Transport
{
public:
    void prepare (double sr) noexcept          { sampleRate = sr; }
    double getSampleRate() const noexcept      { return sampleRate; }

    void   setBpm (double b) noexcept          { bpm = b; }
    double getBpm() const noexcept             { return bpm.load(); }

    double samplesPerBeat() const noexcept     { return sampleRate * 60.0 / bpm.load(); }

    void setLoopBeats (int b) noexcept         { loopBeats = juce::jmax (1, b); }
    int  getLoopBeats() const noexcept         { return loopBeats.load(); }

    juce::int64 loopLengthSamples() const noexcept
    {
        return (juce::int64) std::llround (loopBeats.load() * samplesPerBeat());
    }

    void setPlaying (bool p) noexcept          { playing = p; }
    bool isPlaying() const noexcept            { return playing.load(); }

    enum PlayMode { PatternMode = 0, SongMode = 1 };
    void setPlayMode (int m) noexcept          { playMode = m; }
    int  getPlayMode() const noexcept          { return playMode.load(); }
    bool isSongMode() const noexcept           { return playMode.load() == SongMode; }

    /** GUI asks for a rewind-to-zero; the audio thread performs it safely. */
    void requestReset() noexcept               { resetRequested = true; }
    bool consumeReset() noexcept               { return resetRequested.exchange (false); }

    /** Seek the playhead (from dragging it) to a beat position. */
    void requestSeek (double beats) noexcept   { seekBeats = beats; seekRequested = true; }
    bool consumeSeek (double& outBeats) noexcept
    {
        if (! seekRequested.exchange (false)) return false;
        outBeats = seekBeats.load();
        return true;
    }

    // Loop region.
    void setLoopEnabled (bool e) noexcept          { loopEnabled = e; }
    bool isLoopEnabled() const noexcept            { return loopEnabled.load(); }
    void setLoopRegion (double s, double e) noexcept { loopStartBeats = s; loopEndBeats = e; }
    double getLoopStartBeats() const noexcept      { return loopStartBeats.load(); }
    double getLoopEndBeats() const noexcept        { return loopEndBeats.load(); }

    void        setPlayheadSamples (juce::int64 s) noexcept { playheadSamples = s; }
    juce::int64 getPlayheadSamples() const noexcept         { return playheadSamples.load(); }

    double getPlayheadBeats() const noexcept
    {
        const auto spb = samplesPerBeat();
        return spb > 0.0 ? (double) playheadSamples.load() / spb : 0.0;
    }

private:
    double sampleRate { 44100.0 };
    std::atomic<double>      bpm            { 120.0 };
    std::atomic<int>         loopBeats      { 16 };
    std::atomic<bool>        playing        { false };
    std::atomic<int>         playMode       { PatternMode };
    std::atomic<bool>        resetRequested { false };
    std::atomic<juce::int64> playheadSamples { 0 };

    std::atomic<bool>        seekRequested { false };
    std::atomic<double>      seekBeats      { 0.0 };
    std::atomic<bool>        loopEnabled    { false };
    std::atomic<double>      loopStartBeats { 0.0 };
    std::atomic<double>      loopEndBeats   { 16.0 };
};
