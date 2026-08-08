#pragma once

#include "ArrangeView.h"

/** The timeline ruler, pinned as a fixed strip directly above the arrangement's scrolling lane
    viewport, so it stays visible while the track lanes scroll vertically.

    It owns no state of its own: the ArrangeView holds the timeline coordinate map (zoom / scroll),
    the meter, tempo/time-signature/named markers, the loop + punch regions and the playhead, so the
    ruler delegates both painting and interaction back to it. Because the ruler shares the lanes'
    horizontal coordinate frame (same left edge, same headerWidth, same xForBeat), the mouse events
    it forwards land on the right beat. A light 30 Hz repaint keeps the playhead + live edits current
    (the ArrangeView refreshes the lanes on the same cadence). */
class ArrangeRuler : public juce::Component,
                     private juce::Timer
{
public:
    explicit ArrangeRuler (ArrangeView& view) : av (view) { startTimerHz (30); }

    void paint (juce::Graphics& g) override               { av.paintRuler (g, getWidth()); }
    void mouseDown (const juce::MouseEvent& e) override    { av.rulerMouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override    { av.rulerMouseDrag (e); }
    void mouseUp   (const juce::MouseEvent& e) override    { av.rulerMouseUp (e); }

private:
    void timerCallback() override { repaint(); }

    ArrangeView& av;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeRuler)
};
