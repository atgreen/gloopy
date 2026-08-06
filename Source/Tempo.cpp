// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Tempo map — sorted tempo markers {beat, bpm} and exact piecewise beat<->seconds
// conversion. Between two markers the tempo is constant, so a beat span integrates to
// seconds segment-by-segment. Before the first marker the tempo equals the first
// marker's bpm. An empty map means constant transport.bpm (unchanged behaviour).
//
// The render path follows this map: renderBlock snapshots the markers into an
// allocation-free TempoConv (NoteScheduler.h) once per block and converts beats<->
// samples through it, so collectClip/collectNotes, the loop window, song length, the
// offline render range, and MIDI-record beats all honour mid-song tempo changes. With
// an empty map every conversion reduces to beat*spb, byte-identical to before.

#include "MainComponent.h"
#include "EngineLock.h"
#include <algorithm>
#include <limits>
#include <cmath>

namespace
{
constexpr double kEps = 1.0e-6;
}

using gloopy::time::BeatPosition;   // tempo markers store their beat position typed; double at the edges

// Effective, sorted marker list (never empty): the map, or a single marker at beat 0
// carrying the transport's constant bpm.
static std::vector<MainComponent::TempoMarker> effectiveMarkers (
    const std::vector<MainComponent::TempoMarker>& map, double fallbackBpm)
{
    if (map.empty()) return { { BeatPosition { 0.0 }, fallbackBpm } };
    auto m = map;
    std::sort (m.begin(), m.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
    return m;
}

double MainComponent::tempoAtBeat (double beat)
{
    GLOOPY_ELOCK(sl);
    const auto m = effectiveMarkers (tempoMap, transport.getBpm());
    double bpm = m.front().bpm;                    // before the first marker -> first bpm
    for (auto& mk : m) { if (mk.beat.inBeats() <= beat + kEps) bpm = mk.bpm; else break; }
    return bpm;
}

double MainComponent::apiBeatsToSeconds (double beat)
{
    if (beat <= 0.0) return 0.0;
    GLOOPY_ELOCK(sl);
    const auto m = effectiveMarkers (tempoMap, transport.getBpm());

    double sec = 0.0;
    auto addSeg = [&] (double s, double e, double bpm)
    {
        const double lo = juce::jmax (0.0, s), hi = juce::jmin (beat, e);   // clip to [0, beat]
        if (hi > lo) sec += (hi - lo) * 60.0 / juce::jmax (1.0, bpm);
    };
    if (m.front().beat.inBeats() > 0.0) addSeg (0.0, m.front().beat.inBeats(), m.front().bpm);   // before first marker
    for (size_t i = 0; i < m.size(); ++i)
        addSeg (m[i].beat.inBeats(), (i + 1 < m.size()) ? m[i + 1].beat.inBeats() : beat, m[i].bpm);
    return sec;
}

double MainComponent::apiSecondsToBeats (double seconds)
{
    if (seconds <= 0.0) return 0.0;
    GLOOPY_ELOCK(sl);
    const auto m = effectiveMarkers (tempoMap, transport.getBpm());

    double acc = 0.0;
    auto seg = [&] (double s, double e, double bpm) -> double   // returns beat if target in segment, else -1
    {
        const double b = juce::jmax (1.0, bpm);
        const double dur = (e - s) * 60.0 / b;   // e may be +inf for the open last segment
        if (acc + dur >= seconds - kEps || ! std::isfinite (e)) return s + (seconds - acc) * b / 60.0;
        acc += dur; return -1.0;
    };
    if (m.front().beat.inBeats() > 0.0) { double r = seg (0.0, m.front().beat.inBeats(), m.front().bpm); if (r >= 0.0) return r; }
    for (size_t i = 0; i < m.size(); ++i)
    {
        const double e = (i + 1 < m.size()) ? m[i + 1].beat.inBeats() : std::numeric_limits<double>::infinity();
        const double r = seg (m[i].beat.inBeats(), e, m[i].bpm);
        if (r >= 0.0) return r;
    }
    return m.back().beat.inBeats();
}

bool MainComponent::apiSetTimeSignature (int num, int denom)
{
    if (num < 1 || num > 32 || denom < 1 || denom > 32) return false;
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        transport.setTimeSignature (num, denom);
        if (arrangeView) arrangeView->rebuild();     // bar grid + position display follow beatsPerBar
        emitChange ("time_signature");
        return true;
    });
}

void MainComponent::apiGetTimeSignature (int& num, int& denom)
{
    num   = transport.getTimeSigNumerator();
    denom = transport.getTimeSigDenominator();
}

// The project's meter map: the transport's initial (bar-1) signature plus the mid-song
// time-signature markers. Every bar-grid / bar-number / snap query goes through this so a
// mid-song change is honoured everywhere.
//
// No engineLock (deliberately). timeSigMap is only ever MUTATED on the message thread (the
// tempo API + project load), and every caller of meterMap() runs on the message thread — the
// two gRPC entry points below marshal via callOnMessageThread. The audio thread reads
// timeSigMap DIRECTLY in renderBlock (under engineLock) and never calls meterMap(), so a
// concurrent const read here is safe. Taking engineLock here — at the 30 Hz position-label
// rate — was a steady-state dropout source: slice-1 tracing caught this exact spot holding
// the lock and stalling the audio callback's try-lock (see Source/EngineLock.h). Same
// message-thread-serialised reasoning as scheduleLiveClips.
gloopy::time::MeterMap MainComponent::meterMap()
{
    std::vector<gloopy::time::MeterChange> ch;
    ch.reserve (timeSigMap.size());
    for (auto& m : timeSigMap) ch.push_back ({ m.beat.inBeats(), m.num, m.den });
    return { transport.getTimeSigNumerator(), transport.getTimeSigDenominator(), std::move (ch) };
}

// Absolute beat -> (bar, beat-in-bar), both 1-based to match the "1.1.00" readout. Marshalled
// to the message thread so meterMap()'s lock-free timeSigMap read stays message-thread-only
// (gRPC calls this from its own thread; callOnMessageThread runs inline when already on it).
void MainComponent::apiBeatsToBarBeat (double beat, int& bar, double& beatInBar)
{
    const auto r = callOnMessageThread ([&]
    {
        int b = 1; double bib = 1.0;
        meterMap().beatToBarBeat (beat, b, bib);
        return std::pair<int, double> { b, bib };
    });
    bar = r.first; beatInBar = r.second;
}

double MainComponent::apiBarBeatToBeats (int bar, double beatInBar)
{
    return callOnMessageThread ([&] { return meterMap().barBeatToBeats (bar, beatInBar); });
}

bool MainComponent::apiAddTimeSigMarker (double beat, int num, int denom)
{
    if (beat < 0.0 || num < 1 || num > 32 || denom < 1 || denom > 32) return false;
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const double snapped = meterMap().snapToBar (beat);   // land the change on a bar boundary
        GLOOPY_ELOCK(sl);
        if (snapped <= kEps)                                  // a change at bar 1 IS the initial signature
        {
            transport.setTimeSignature (num, denom);
        }
        else
        {
            auto it = std::find_if (timeSigMap.begin(), timeSigMap.end(),
                        [&] (const TimeSigMarker& m) { return std::abs (m.beat.inBeats() - snapped) < kEps; });
            if (it != timeSigMap.end()) { it->num = num; it->den = denom; }      // upsert
            else timeSigMap.push_back ({ BeatPosition { snapped }, num, denom });
            std::sort (timeSigMap.begin(), timeSigMap.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
        }
        if (arrangeView) arrangeView->rebuild();
        emitChange ("time_signature");
        std::cout << "[timesig] marker beat=" << snapped << " " << num << "/" << denom << std::endl;
        return true;
    });
}

bool MainComponent::apiRemoveTimeSigMarker (double beat)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        GLOOPY_ELOCK(sl);
        const auto before = timeSigMap.size();
        timeSigMap.erase (std::remove_if (timeSigMap.begin(), timeSigMap.end(),
                            [&] (const TimeSigMarker& m) { return std::abs (m.beat.inBeats() - beat) < kEps; }),
                          timeSigMap.end());
        const bool changed = timeSigMap.size() != before;
        if (changed) { if (arrangeView) arrangeView->rebuild(); emitChange ("time_signature"); }
        return changed;
    });
}

std::vector<MainComponent::TimeSigMarker> MainComponent::apiListTimeSigMarkers()
{
    return callOnMessageThread ([&] { GLOOPY_ELOCK(sl); return timeSigMap; });
}

bool MainComponent::apiAddTempoMarker (double beat, double bpm)
{
    if (beat < 0.0 || bpm < 20.0 || bpm > 400.0) return false;
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        GLOOPY_ELOCK(sl);
        auto it = std::find_if (tempoMap.begin(), tempoMap.end(),
                                [&] (const TempoMarker& m) { return std::abs (m.beat.inBeats() - beat) < kEps; });
        if (it != tempoMap.end()) it->bpm = bpm;                 // upsert
        else                       tempoMap.push_back ({ BeatPosition { beat }, bpm });
        std::sort (tempoMap.begin(), tempoMap.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
        std::cout << "[tempo] marker beat=" << beat << " bpm=" << bpm << std::endl;
        return true;
    });
}

bool MainComponent::apiRemoveTempoMarker (double beat)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        GLOOPY_ELOCK(sl);
        const auto before = tempoMap.size();
        tempoMap.erase (std::remove_if (tempoMap.begin(), tempoMap.end(),
                            [&] (const TempoMarker& m) { return std::abs (m.beat.inBeats() - beat) < kEps; }),
                        tempoMap.end());
        return tempoMap.size() != before;
    });
}

std::vector<MainComponent::TempoMarker> MainComponent::apiListTempoMarkers()
{
    return callOnMessageThread ([&] { GLOOPY_ELOCK(sl); return tempoMap; });
}

// Tempo-aware conversions built on the (already-tested) beat<->seconds integration.
// With an empty map these reduce to beat*spb / samples/spb exactly, so wiring them
// into the render path is a no-op until a tempo map is set.
juce::int64 MainComponent::beatToSamples (gloopy::time::BeatPosition beat)
{
    return (juce::int64) std::llround (apiBeatsToSeconds (beat.inBeats()) * currentSampleRate);
}

gloopy::time::BeatPosition MainComponent::samplesToBeats (juce::int64 samples)
{
    return gloopy::time::BeatPosition { currentSampleRate > 0.0
        ? apiSecondsToBeats ((double) samples / currentSampleRate) : 0.0 };
}
