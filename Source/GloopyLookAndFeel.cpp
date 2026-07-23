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
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (b.getToggleState() ? Palette::accent.withAlpha (0.9f) : Palette::line);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
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
        g.setColour (Palette::inset);
        g.fillRoundedRectangle (cx - tw * 0.5f, (float) y, tw, (float) height, 2.0f);
        g.setColour (Palette::accent.withAlpha (0.9f));
        g.fillRoundedRectangle (cx - tw * 0.5f, sliderPos, tw, (float) (y + height) - sliderPos, 2.0f);
        g.setColour (Palette::textBright);
        g.fillRoundedRectangle ((float) x + 1.0f, sliderPos - 3.5f, (float) width - 2.0f, 7.0f, 2.5f);
    }
    else if (style == juce::Slider::LinearHorizontal)
    {
        const float cy = (float) y + (float) height * 0.5f;
        const float th = 4.0f;
        g.setColour (Palette::inset);
        g.fillRoundedRectangle ((float) x, cy - th * 0.5f, (float) width, th, 2.0f);
        g.setColour (Palette::accent.withAlpha (0.9f));
        g.fillRoundedRectangle ((float) x, cy - th * 0.5f, sliderPos - (float) x, th, 2.0f);
        g.setColour (Palette::textBright);
        g.fillRoundedRectangle (sliderPos - 3.5f, (float) y + 1.0f, 7.0f, (float) height - 2.0f, 2.5f);
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
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

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
