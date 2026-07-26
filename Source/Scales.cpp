// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Scales — a project-level scale (root + semitone intervals) used for snap-to-scale,
// piano-roll highlighting, and as a hint for generative clients. Pure metadata plus a
// note-pitch snapping operation: no audio-thread involvement. Serialised in the
// composition manifest (see Composition.cpp) and toValueTree/loadFromTree.

#include "MainComponent.h"
#include "SynthGenerator.h"
#include <set>
#include <cmath>

namespace
{
// Built-in scales by name -> semitone offsets from the root.
std::vector<int> builtinScale (const juce::String& n)
{
    const auto k = n.toLowerCase().trim();
    if (k == "major" || k == "ionian")     return { 0,2,4,5,7,9,11 };
    if (k == "minor" || k == "aeolian")    return { 0,2,3,5,7,8,10 };
    if (k == "harmonic-minor")             return { 0,2,3,5,7,8,11 };
    if (k == "melodic-minor")              return { 0,2,3,5,7,9,11 };
    if (k == "dorian")                     return { 0,2,3,5,7,9,10 };
    if (k == "phrygian")                   return { 0,1,3,5,7,8,10 };
    if (k == "lydian")                     return { 0,2,4,6,7,9,11 };
    if (k == "mixolydian")                 return { 0,2,4,5,7,9,10 };
    if (k == "locrian")                    return { 0,1,3,5,6,8,10 };
    if (k == "pentatonic-major")           return { 0,2,4,7,9 };
    if (k == "pentatonic-minor")           return { 0,3,5,7,10 };
    if (k == "blues")                      return { 0,3,5,6,7,10 };
    if (k == "whole-tone")                 return { 0,2,4,6,8,10 };
    if (k == "chromatic")                  return { 0,1,2,3,4,5,6,7,8,9,10,11 };
    return {};   // unknown name
}
}

bool MainComponent::apiSetScale (int root, const juce::String& name, const std::vector<int>& intervals)
{
    return callOnMessageThread ([&] () -> bool
    {
        std::vector<int> iv = intervals;
        juce::String nm = name.trim();
        if (iv.empty())                              // resolve by name
        {
            iv = builtinScale (nm);
            if (iv.empty()) return false;            // unknown name and no intervals
        }
        else if (nm.isEmpty()) nm = "custom";

        // Normalise: unique, in 0..11, sorted, always include the root degree (0).
        std::set<int> s;
        for (int i : iv) s.insert (((i % 12) + 12) % 12);
        s.insert (0);
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        scaleRoot = ((root % 12) + 12) % 12;
        scaleName = nm;
        scaleIntervals.assign (s.begin(), s.end());
        editorPanel.roll.setScale (scaleRoot, scaleIntervals);   // piano-roll highlight
        refreshScaleToolbar();                                    // keep the toolbar combos in sync (API/script too)
        std::cout << "[scale] " << scaleName << " root=" << scaleRoot
                  << " (" << scaleIntervals.size() << " degrees)" << std::endl;
        return true;
    });
}

void MainComponent::apiGetScale (int& root, juce::String& name, std::vector<int>& intervals)
{
    callOnMessageThread ([&]
    {
        const juce::ScopedLock sl (engineLock);
        root = scaleRoot; name = scaleName; intervals = scaleIntervals;
        return true;
    });
}

// ── Microtuning ──────────────────────────────────────────────────────────────
// Push the project cents table into every synth's voice params (index = pitch class).
// Built-in synth only for now (SFZ/plugins ignore it). Caller holds engineLock.
void MainComponent::applyTuningToSynths()
{
    for (auto& t : tracks)
        if (auto* sg = dynamic_cast<SynthGenerator*> (t->generator.get()))
            for (int i = 0; i < 12; ++i)
                sg->engine.params.tuning[(size_t) i].store ((float) projectTuning[(size_t) i]);
}

bool MainComponent::apiSetTuning (const std::vector<double>& cents12)
{
    if (cents12.size() != 12) return false;
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        for (int i = 0; i < 12; ++i) projectTuning[(size_t) i] = cents12[(size_t) i];
        applyTuningToSynths();
        return true;
    });
}

std::vector<double> MainComponent::apiGetTuning()
{
    return callOnMessageThread ([&]
    {
        const juce::ScopedLock sl (engineLock);
        return std::vector<double> (projectTuning.begin(), projectTuning.end());
    });
}

// Parse a Scala .scl file into a 12-entry cents-offset-from-ET table. The file lists a
// pitch (description line, note count, then that many degrees as cents "x.y" or ratios
// "a/b"/"n"); degree i is pitch class i's absolute cents from the root, so the stored
// offset is (degree cents - i*100). Only 12-note scales map cleanly to the chromatic keys.
bool MainComponent::apiImportScl (const juce::String& path)
{
    juce::File f (path);
    if (! f.existsAsFile()) return false;
    juce::StringArray lines;
    lines.addLines (f.loadFileAsString());
    std::vector<double> degrees;                 // absolute cents from the root, degree 1..N
    int count = 0;
    bool haveDesc = false, haveCount = false;
    for (auto raw : lines)
    {
        auto line = raw.trim();
        if (line.startsWithChar ('!') || line.isEmpty()) continue;   // comment / blank
        if (! haveDesc)  { haveDesc = true; continue; }               // first non-comment = description (skip)
        if (! haveCount) { count = line.getIntValue(); haveCount = true; continue; }   // second = note count
        double cents;
        if (line.containsChar ('.'))                                  // cents value
            cents = line.getDoubleValue();
        else if (line.containsChar ('/'))                             // ratio a/b
        {
            const double a = line.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
            const double b = line.fromFirstOccurrenceOf ("/", false, false).getDoubleValue();
            cents = (b > 0.0 && a > 0.0) ? 1200.0 * std::log2 (a / b) : 0.0;
        }
        else                                                          // integer ratio n/1
            cents = line.getDoubleValue() > 0.0 ? 1200.0 * std::log2 (line.getDoubleValue()) : 0.0;
        degrees.push_back (cents);
        if ((int) degrees.size() >= count) break;
    }
    if (degrees.size() < 12) return false;                            // need 12 degrees (11 + octave)
    std::vector<double> tuning (12, 0.0);                             // pitch class 0 = root = 0
    for (int i = 1; i < 12; ++i) tuning[(size_t) i] = degrees[(size_t) (i - 1)] - i * 100.0;
    return apiSetTuning (tuning);
}

// Toolbar scale selector -> model. Reads both combo boxes and applies via the same
// apiSetScale the control API uses, so the UI and scripts set scales identically.
void MainComponent::applyScaleFromToolbar()
{
    const int root = juce::jmax (0, scaleRootBox.getSelectedId() - 1);   // 0=C
    const juce::String name = scaleNameBox.getText();
    if (name.isNotEmpty())
        apiSetScale (root, name, {});   // resolve intervals from the built-in name
}

// Model -> toolbar (after load / NewProject), without re-firing onChange.
void MainComponent::refreshScaleToolbar()
{
    int root; juce::String name; std::vector<int> iv;
    apiGetScale (root, name, iv);
    scaleRootBox.setSelectedId (juce::jlimit (1, 12, root + 1), juce::dontSendNotification);
    for (int i = 1; i <= scaleNameBox.getNumItems(); ++i)
        if (scaleNameBox.getItemText (i - 1).equalsIgnoreCase (name))
        {
            scaleNameBox.setSelectedId (scaleNameBox.getItemId (i - 1), juce::dontSendNotification);
            break;
        }
}

int MainComponent::snapPitchToScale (int pitch) const
{
    // Allowed pitch classes for the current scale.
    std::set<int> allowed;
    for (int i : scaleIntervals) allowed.insert (((scaleRoot + i) % 12 + 12) % 12);
    if (allowed.empty() || (int) allowed.size() >= 12) return pitch;   // chromatic / empty -> no-op

    const int pc = ((pitch % 12) + 12) % 12;
    // Search outward; +d before -d so ties round up.
    for (int d = 0; d <= 6; ++d)
    {
        if (allowed.count ((pc + d) % 12)) return juce::jlimit (0, 127, pitch + d);
        if (d > 0 && allowed.count (((pc - d) % 12 + 12) % 12)) return juce::jlimit (0, 127, pitch - d);
    }
    return pitch;
}

int MainComponent::apiSnapClipToScale (int trackId, int clipIndex)
{
    return callOnMessageThread ([&] () -> int
    {
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return -1;
        pushUndoSnapshot();
        int changed = 0;
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (clipIndex, (int) t->clips.size())) return -1;
            for (auto& n : t->clips[(size_t) clipIndex].notes)
            {
                const int snapped = snapPitchToScale (n.pitch);
                if (snapped != n.pitch) { n.pitch = snapped; ++changed; }
            }
        }
        emitChange ("clip_changed", trackId);
        if (arrangeView) arrangeView->repaint();
        return changed;
    });
}
