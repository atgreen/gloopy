// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include "Palette.h"

/** A flat transport button that draws a vector icon (play / pause / stop /
    record), styled to the app palette. */
class IconButton : public juce::Button
{
public:
    enum Icon { Play, Pause, Stop, Record };

    explicit IconButton (Icon i) : juce::Button ({}), icon (i) {}

    void setIcon (Icon i) { icon = i; repaint(); }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.5f);
        auto base = getToggleState() ? Palette::accentDim : Palette::header;
        if (down)             base = base.brighter (0.10f);
        else if (highlighted) base = base.brighter (0.06f);

        g.setColour (base);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (getToggleState() ? Palette::accent : Palette::line);
        g.drawRoundedRectangle (b, 4.0f, 1.0f);

        const auto c = b.getCentre();
        const float s = juce::jmin (b.getWidth(), b.getHeight()) * 0.30f;

        switch (icon)
        {
            case Play:
            {
                juce::Path p;
                p.addTriangle (c.x - s * 0.72f, c.y - s, c.x - s * 0.72f, c.y + s, c.x + s * 0.85f, c.y);
                g.setColour (getToggleState() ? Palette::textBright : Palette::green.brighter (0.1f));
                g.fillPath (p);
                break;
            }
            case Pause:
                g.setColour (Palette::textBright);
                g.fillRoundedRectangle (c.x - s * 0.75f, c.y - s, s * 0.55f, s * 2.0f, 1.0f);
                g.fillRoundedRectangle (c.x + s * 0.20f, c.y - s, s * 0.55f, s * 2.0f, 1.0f);
                break;
            case Stop:
                g.setColour (Palette::textBright);
                g.fillRoundedRectangle (juce::Rectangle<float> (s * 1.7f, s * 1.7f).withCentre (c), 1.5f);
                break;
            case Record:
                g.setColour (getToggleState() ? Palette::red : Palette::red.withAlpha (0.85f));
                g.fillEllipse (juce::Rectangle<float> (s * 1.8f, s * 1.8f).withCentre (c));
                break;
        }
    }

private:
    Icon icon;
};
