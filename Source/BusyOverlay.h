// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include "Palette.h"

/** A full-window "busy" overlay with an animated spinner and a label, shown while a
    long task runs on a background thread (see MainComponent::runBackground). Dims the
    UI and swallows clicks so nothing is touched mid-load. */
class BusyOverlay : public juce::Component,
                    private juce::Timer
{
public:
    BusyOverlay()
    {
        setInterceptsMouseClicks (true, true);   // block interaction while busy
        setVisible (false);
    }

    void show (const juce::String& text)
    {
        label = text;
        angle = 0.0f;
        setVisible (true);
        toFront (false);
        startTimerHz (30);
        repaint();
    }

    void hide()
    {
        stopTimer();
        setVisible (false);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.55f));

        const auto c = getLocalBounds().toFloat().getCentre();
        const float r = 20.0f;
        juce::Path arc;
        arc.addCentredArc (c.x, c.y - 14.0f, r, r, 0.0f, angle, angle + 4.2f, true);
        g.setColour (Palette::accent);
        g.strokePath (arc, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (label, getLocalBounds().withTrimmedTop (32),
                    juce::Justification::centred, false);
    }

private:
    void timerCallback() override { angle += 0.28f; repaint(); }

    juce::String label;
    float angle { 0.0f };
};
