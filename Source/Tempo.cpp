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
#include <algorithm>
#include <limits>
#include <cmath>

namespace
{
constexpr double kEps = 1.0e-6;
}

// Effective, sorted marker list (never empty): the map, or a single marker at beat 0
// carrying the transport's constant bpm.
static std::vector<MainComponent::TempoMarker> effectiveMarkers (
    const std::vector<MainComponent::TempoMarker>& map, double fallbackBpm)
{
    if (map.empty()) return { { 0.0, fallbackBpm } };
    auto m = map;
    std::sort (m.begin(), m.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
    return m;
}

double MainComponent::tempoAtBeat (double beat)
{
    const juce::ScopedLock sl (engineLock);
    const auto m = effectiveMarkers (tempoMap, transport.getBpm());
    double bpm = m.front().bpm;                    // before the first marker -> first bpm
    for (auto& mk : m) { if (mk.beat <= beat + kEps) bpm = mk.bpm; else break; }
    return bpm;
}

double MainComponent::apiBeatsToSeconds (double beat)
{
    if (beat <= 0.0) return 0.0;
    const juce::ScopedLock sl (engineLock);
    const auto m = effectiveMarkers (tempoMap, transport.getBpm());

    double sec = 0.0;
    auto addSeg = [&] (double s, double e, double bpm)
    {
        const double lo = juce::jmax (0.0, s), hi = juce::jmin (beat, e);   // clip to [0, beat]
        if (hi > lo) sec += (hi - lo) * 60.0 / juce::jmax (1.0, bpm);
    };
    if (m.front().beat > 0.0) addSeg (0.0, m.front().beat, m.front().bpm);   // before first marker
    for (size_t i = 0; i < m.size(); ++i)
        addSeg (m[i].beat, (i + 1 < m.size()) ? m[i + 1].beat : beat, m[i].bpm);
    return sec;
}

double MainComponent::apiSecondsToBeats (double seconds)
{
    if (seconds <= 0.0) return 0.0;
    const juce::ScopedLock sl (engineLock);
    const auto m = effectiveMarkers (tempoMap, transport.getBpm());

    double acc = 0.0;
    auto seg = [&] (double s, double e, double bpm) -> double   // returns beat if target in segment, else -1
    {
        const double b = juce::jmax (1.0, bpm);
        const double dur = (e - s) * 60.0 / b;   // e may be +inf for the open last segment
        if (acc + dur >= seconds - kEps || ! std::isfinite (e)) return s + (seconds - acc) * b / 60.0;
        acc += dur; return -1.0;
    };
    if (m.front().beat > 0.0) { double r = seg (0.0, m.front().beat, m.front().bpm); if (r >= 0.0) return r; }
    for (size_t i = 0; i < m.size(); ++i)
    {
        const double e = (i + 1 < m.size()) ? m[i + 1].beat : std::numeric_limits<double>::infinity();
        const double r = seg (m[i].beat, e, m[i].bpm);
        if (r >= 0.0) return r;
    }
    return m.back().beat;
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

// Absolute beat -> (bar, beat-in-bar), both 1-based to match the "1.1.00" readout.
void MainComponent::apiBeatsToBarBeat (double beat, int& bar, double& beatInBar)
{
    const double bpb = juce::jmax (0.001, transport.beatsPerBar());
    const double b   = juce::jmax (0.0, beat);
    const int    b0  = (int) std::floor (b / bpb);
    bar       = b0 + 1;
    beatInBar = (b - (double) b0 * bpb) + 1.0;
}

double MainComponent::apiBarBeatToBeats (int bar, double beatInBar)
{
    return (double) (bar - 1) * transport.beatsPerBar() + (beatInBar - 1.0);
}

bool MainComponent::apiAddTempoMarker (double beat, double bpm)
{
    if (beat < 0.0 || bpm < 20.0 || bpm > 400.0) return false;
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        auto it = std::find_if (tempoMap.begin(), tempoMap.end(),
                                [&] (const TempoMarker& m) { return std::abs (m.beat - beat) < kEps; });
        if (it != tempoMap.end()) it->bpm = bpm;                 // upsert
        else                       tempoMap.push_back ({ beat, bpm });
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
        const juce::ScopedLock sl (engineLock);
        const auto before = tempoMap.size();
        tempoMap.erase (std::remove_if (tempoMap.begin(), tempoMap.end(),
                            [&] (const TempoMarker& m) { return std::abs (m.beat - beat) < kEps; }),
                        tempoMap.end());
        return tempoMap.size() != before;
    });
}

std::vector<MainComponent::TempoMarker> MainComponent::apiListTempoMarkers()
{
    return callOnMessageThread ([&] { const juce::ScopedLock sl (engineLock); return tempoMap; });
}

// Tempo-aware conversions built on the (already-tested) beat<->seconds integration.
// With an empty map these reduce to beat*spb / samples/spb exactly, so wiring them
// into the render path is a no-op until a tempo map is set.
juce::int64 MainComponent::beatToSamples (double beat)
{
    return (juce::int64) std::llround (apiBeatsToSeconds (beat) * currentSampleRate);
}

double MainComponent::samplesToBeats (juce::int64 samples)
{
    return currentSampleRate > 0.0 ? apiSecondsToBeats ((double) samples / currentSampleRate) : 0.0;
}
