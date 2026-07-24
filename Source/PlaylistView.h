// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <functional>
#include "PlaylistClip.h"
#include "Pattern.h"
#include "Transport.h"

/** The Playlist: a multi-track timeline where pattern clips are painted to
    arrange a song.

    Interaction:
      - click an empty cell        → place a 1-bar clip of the selected pattern
      - drag a clip's body          → move it (snapped to the bar grid / track)
      - drag a clip's right edge    → resize (the pattern repeats to fill)
      - right-click / double-click  → delete the clip */
class PlaylistView : public juce::Component,
                     private juce::Timer
{
public:
    static constexpr int kNumTracks = 6;

    PlaylistView (std::vector<PlaylistClip>& clipsRef,
                  std::vector<std::unique_ptr<Pattern>>& patternsRef,
                  Transport& transportRef,
                  juce::CriticalSection& engineLockRef,
                  std::function<int()> selectedPatternProvider);
    ~PlaylistView() override;

    std::function<void()> onChanged;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    int    numBars() const;
    float  barWidth() const;
    float  xForBeat (double beat) const;
    double beatForX (float x) const;
    float  yForTrack (int t) const;
    int    trackForY (float y) const;
    double snapToBar (double beat) const;
    int    clipIndexAt (juce::Point<float> p) const;

    static constexpr int gutter      = 46;
    static constexpr int rulerHeight = 20;
    static constexpr int trackHeight = 34;
    static constexpr double beatsPerBar = 4.0;

    std::vector<PlaylistClip>&             clips;
    std::vector<std::unique_ptr<Pattern>>& patterns;
    Transport&                             transport;
    juce::CriticalSection&                 engineLock;
    std::function<int()>                   selectedPattern;

    enum class Drag { none, move, resize };
    Drag   drag        { Drag::none };
    int    activeClip  { -1 };
    int    selectedClip { -1 };
    double dragBeatOffset { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistView)
};
