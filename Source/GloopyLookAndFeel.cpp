// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#include "GloopyLookAndFeel.h"

GloopyLookAndFeel::GloopyLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::bg);
    setColour (juce::DocumentWindow::textColourId,        Palette::text);

    setColour (juce::PopupMenu::backgroundColourId,            Palette::panel);
    setColour (juce::PopupMenu::textColourId,                  Palette::text);
    setColour (juce::PopupMenu::headerTextColourId,            Palette::textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId,       Palette::textBright);

    setColour (juce::TextButton::buttonColourId,   Palette::header);
    setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
    setColour (juce::TextButton::textColourOnId,   Palette::textBright);
    setColour (juce::TextButton::textColourOffId,  Palette::text);

    setColour (juce::ComboBox::backgroundColourId, Palette::inset);
    setColour (juce::ComboBox::textColourId,       Palette::accent);
    setColour (juce::ComboBox::outlineColourId,    Palette::line);
    setColour (juce::ComboBox::arrowColourId,      Palette::accent);

    setColour (juce::Slider::textBoxTextColourId,       Palette::accent);
    setColour (juce::Slider::textBoxBackgroundColourId, Palette::inset);
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId,  Palette::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, Palette::inset);
    setColour (juce::Slider::thumbColourId,             Palette::textBright);
    setColour (juce::Slider::trackColourId,             Palette::accent);
    setColour (juce::Slider::backgroundColourId,        Palette::inset);

    setColour (juce::Label::textColourId,     Palette::text);
    setColour (juce::ScrollBar::thumbColourId, Palette::line);
}

juce::Font GloopyLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) buttonHeight * 0.55f), juce::Font::bold));
}

void GloopyLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                              const juce::Colour& backgroundColour,
                                              bool highlighted, bool down)
{
    auto bounds = b.getLocalBounds().toFloat().reduced (0.5f);
    auto base = backgroundColour;
    if (down)             base = base.brighter (0.10f);
    else if (highlighted) base = base.brighter (0.06f);

    g.setColour (base);
    g.fillRoundedRectangle (bounds, Palette::radius);
    if (b.getToggleState())   // separate by shade; only an active button gets an accent edge
    {
        g.setColour (Palette::accent.withAlpha (0.9f));
        g.drawRoundedRectangle (bounds, Palette::radius, 1.0f);
    }
}

void GloopyLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float startAngle, float endAngle,
                                          juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto cx = bounds.getCentreX();
    const auto cy = bounds.getCentreY();
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);
    const float thick = juce::jmax (2.5f, radius * 0.16f);

    juce::Path track;
    track.addCentredArc (cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (Palette::inset);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (cx, cy, radius, radius, 0.0f, startAngle, angle, true);
    g.setColour (Palette::accent);
    g.strokePath (value, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Point<float> tip (cx + radius * std::cos (angle - juce::MathConstants<float>::halfPi),
                            cy + radius * std::sin (angle - juce::MathConstants<float>::halfPi));
    g.setColour (Palette::textBright);
    g.fillEllipse (juce::Rectangle<float> (thick + 1.0f, thick + 1.0f).withCentre (tip));
}

void GloopyLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float, float,
                                          juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearVertical)
    {
        const float cx = (float) x + (float) width * 0.5f;
        const float tw = 4.0f;
        const float top = (float) y, bot = (float) (y + height);
        // Sunken track.
        g.setColour (Palette::inset);
        g.fillRoundedRectangle (cx - tw * 0.5f, top, tw, (float) height, 2.0f);
        // Faint "nominal" reference tick (~80% of the throw), like a hardware fader's 0 mark.
        g.setColour (Palette::line);
        g.fillRect (cx - 6.0f, top + (float) height * 0.2f, 12.0f, 1.0f);
        // Accent level fill below the cap.
        g.setColour (Palette::accent.withAlpha (0.9f));
        g.fillRoundedRectangle (cx - tw * 0.5f, sliderPos, tw, bot - sliderPos, 2.0f);
        // Cap: a rounded handle, lighter at the top (elevation via shading, no drop shadow),
        // a crisp outline instead of a shadow, and an accent centre grip line.
        const float capW = juce::jmin ((float) width - 2.0f, 22.0f);
        const float capH = 13.0f;
        juce::Rectangle<float> cap (cx - capW * 0.5f, sliderPos - capH * 0.5f, capW, capH);
        g.setGradientFill (juce::ColourGradient (Palette::headerHi, cap.getX(), cap.getY(),
                                                 Palette::header,   cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (Palette::line);
        g.drawRoundedRectangle (cap.reduced (0.5f), 3.0f, 1.0f);
        g.setColour (Palette::accent);
        g.fillRoundedRectangle (cap.getX() + 3.0f, sliderPos - 1.0f, cap.getWidth() - 6.0f, 2.0f, 1.0f);
    }
    else if (style == juce::Slider::LinearHorizontal)
    {
        const float cy = (float) y + (float) height * 0.5f;
        const float th = 4.0f;
        const float left = (float) x, right = (float) (x + width);
        const float centre = (left + right) * 0.5f;
        // Sunken track.
        g.setColour (Palette::inset);
        g.fillRoundedRectangle (left, cy - th * 0.5f, (float) width, th, 2.0f);
        // Centre detent — this is a PAN, so 0 is the middle, not the left edge.
        g.setColour (Palette::line);
        g.fillRect (centre - 0.5f, cy - 5.0f, 1.0f, 10.0f);
        // Accent fill from the centre outward toward the handle (left OR right).
        g.setColour (Palette::accent.withAlpha (0.9f));
        const float fL = juce::jmin (centre, sliderPos), fR = juce::jmax (centre, sliderPos);
        g.fillRoundedRectangle (fL, cy - th * 0.5f, fR - fL, th, 2.0f);
        // Vertical cap: lighter on the left (elevation), outline, accent centre grip line.
        const float capW = 11.0f, capH = juce::jmin ((float) height - 2.0f, 18.0f);
        juce::Rectangle<float> cap (sliderPos - capW * 0.5f, cy - capH * 0.5f, capW, capH);
        g.setGradientFill (juce::ColourGradient (Palette::headerHi, cap.getX(),     cap.getY(),
                                                 Palette::header,   cap.getRight(), cap.getY(), false));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (Palette::line);
        g.drawRoundedRectangle (cap.reduced (0.5f), 3.0f, 1.0f);
        g.setColour (Palette::accent);
        g.fillRoundedRectangle (sliderPos - 1.0f, cap.getY() + 3.0f, 2.0f, cap.getHeight() - 6.0f, 1.0f);
    }
    else
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, slider);
    }
}

void GloopyLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, Palette::radius);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, Palette::radius, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.5f, ax + 4.0f, ay - 2.5f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

void GloopyLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y,
                                       int width, int height, bool isVertical,
                                       int thumbStart, int thumbSize, bool over, bool)
{
    juce::Rectangle<int> thumb = isVertical
        ? juce::Rectangle<int> (x + 2, thumbStart, width - 4, thumbSize)
        : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, height - 4);
    g.setColour (Palette::line.withAlpha (over ? 0.95f : 0.7f));
    g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
}
