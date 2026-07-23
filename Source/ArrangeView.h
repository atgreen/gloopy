#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <functional>
#include "Track.h"
#include "Transport.h"

/** The linear arrangement: track headers on the left, a timeline of clips on
    the right, one playhead. Create/move/resize/delete clips; selecting a clip
    notifies the owner so it can open the clip in the editor below. */
class ArrangeView : public juce::Component,
                    private juce::Timer
{
public:
    ArrangeView (std::vector<std::unique_ptr<Track>>& tracksRef,
                 Transport& transportRef,
                 juce::CriticalSection& engineLockRef);
    ~ArrangeView() override;

    /** Recreate per-track header controls after tracks are added/removed. */
    void rebuild();

    int preferredHeight() const;

    void setSelection (int track, int clip) { selTrack = track; selClip = clip; repaint(); }

    /** (track, clip) — clip = -1 means "no clip / just a track selected". */
    std::function<void (int, int)> onClipSelected;
    /** A clip's content/position changed (repaint editor previews etc.). */
    std::function<void()> onChanged;
    /** The loop region was enabled/disabled/moved from the ruler. */
    std::function<void()> onLoopChanged;
    /** Open the plugin editor for a plugin-instrument track (arg = track index). */
    std::function<void (int)> onOpenTrackEditor;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    int    numBars() const;
    float  barWidth() const;
    float  xForBeat (double beat) const;
    double beatForX (float x) const;
    int    trackAtY (float y) const;
    double snapToBar (double beat) const;
    int    clipAt (int track, juce::Point<float> p) const;
    void   drawClip (juce::Graphics&, const Track&, const Clip&, juce::Rectangle<float>, bool selected) const;

    static constexpr int headerWidth = 190;
    static constexpr int rulerHeight  = 22;
    static constexpr int trackHeight  = 64;
    static constexpr double beatsPerBar = 4.0;

    std::vector<std::unique_ptr<Track>>& tracks;
    Transport&             transport;
    juce::CriticalSection& engineLock;

    std::vector<std::unique_ptr<juce::TextButton>> muteButtons;
    std::vector<std::unique_ptr<juce::TextButton>> soloButtons;
    std::vector<std::unique_ptr<juce::TextButton>> editButtons;   // plugin UI (plugin tracks only)
    std::vector<std::unique_ptr<juce::Slider>>     volSliders;

    int selTrack { -1 }, selClip { -1 };

    enum class Drag { none, move, resize };
    Drag   drag { Drag::none };
    int    dragTrack { -1 }, dragClip { -1 };
    double dragBeatOffset { 0.0 };

    // Ruler drag (seek / loop region).
    bool   rulerDrag { false }, loopDragged { false };
    double rulerStartBeat { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeView)
};
