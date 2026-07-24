// SPDX-FileCopyrightText: 2026 Anthony Green <anthony@atgreen.org>
// SPDX-License-Identifier: GPL-3.0-only

#include "ChannelRackView.h"
#include "Palette.h"

ChannelRackView::ChannelRackView (std::vector<std::unique_ptr<Channel>>& channelsRef,
                                  std::function<Pattern*()> patternProvider,
                                  Transport& transportRef,
                                  juce::CriticalSection& engineLockRef)
    : channels (channelsRef), getPattern (std::move (patternProvider)),
      transport (transportRef), engineLock (engineLockRef)
{
    startTimerHz (30);
}

ChannelRackView::~ChannelRackView()
{
    stopTimer();
}

int ChannelRackView::preferredHeight() const
{
    return juce::jmax (1, (int) channels.size()) * rowHeight;
}

void ChannelRackView::rebuild()
{
    muteButtons.clear();
    pianoButtons.clear();
    volSliders.clear();
    removeAllChildren();

    for (int i = 0; i < (int) channels.size(); ++i)
    {
        Channel* ch = channels[(size_t) i].get();

        auto piano = std::make_unique<juce::TextButton> ("PR");
        piano->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff33333a));
        piano->onClick = [this, i] { if (onOpenPianoRoll) onOpenPianoRoll (i); };
        addAndMakeVisible (*piano);
        pianoButtons.push_back (std::move (piano));

        auto mute = std::make_unique<juce::TextButton> ("M");
        mute->setClickingTogglesState (true);
        mute->setToggleState (ch->mute.load(), juce::dontSendNotification);
        mute->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb03030));
        mute->onClick = [ch, m = mute.get()] { ch->mute.store (m->getToggleState()); };
        addAndMakeVisible (*mute);
        muteButtons.push_back (std::move (mute));

        auto vol = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                   juce::Slider::NoTextBox);
        vol->setRange (0.0, 1.0, 0.01);
        vol->setValue (ch->volume.load(), juce::dontSendNotification);
        vol->onValueChange = [ch, s = vol.get()] { ch->volume.store ((float) s->getValue()); };
        addAndMakeVisible (*vol);
        volSliders.push_back (std::move (vol));
    }

    setSize (getWidth(), preferredHeight());
    resized();
    repaint();
}

void ChannelRackView::resized()
{
    for (int i = 0; i < (int) channels.size(); ++i)
    {
        const int y = i * rowHeight;
        pianoButtons[(size_t) i]->setBounds (headerWidth - 68, y + 6, 28, 20);
        muteButtons [(size_t) i]->setBounds (headerWidth - 34, y + 6, 28, 20);
        volSliders  [(size_t) i]->setBounds (12, y + rowHeight - 16, headerWidth - 24, 12);
    }
}

float ChannelRackView::cellWidth() const
{
    auto* p = getPattern();
    const int cols = p != nullptr ? p->getLengthSteps() : 0;
    return cols > 0 ? (float) (getWidth() - headerWidth) / (float) cols : 0.0f;
}

int ChannelRackView::rowAt (float y) const
{
    const int r = (int) (y / rowHeight);
    return juce::isPositiveAndBelow (r, (int) channels.size()) ? r : -1;
}

int ChannelRackView::stepAt (float x) const
{
    auto* p = getPattern();
    const float cw = cellWidth();
    if (p == nullptr || cw <= 0.0f || x < headerWidth)
        return -1;
    const int s = (int) ((x - headerWidth) / cw);
    return juce::isPositiveAndBelow (s, p->getLengthSteps()) ? s : -1;
}

void ChannelRackView::mouseDown (const juce::MouseEvent& e)
{
    auto* p = getPattern();
    const int row  = rowAt (e.position.y);
    const int step = stepAt (e.position.x);
    if (p == nullptr || row < 0 || step < 0)
        return;

    Channel* ch = channels[(size_t) row].get();
    {
        const juce::ScopedLock sl (engineLock);
        p->toggleStep (row, step, ch->defaultPitch, 0.85f);
    }
    if (onPatternEdited) onPatternEdited (row);
    repaint();
}

void ChannelRackView::paint (juce::Graphics& g)
{
    g.fillAll (Palette::panel);

    auto* p = getPattern();
    if (p == nullptr)
        return;

    const float cw   = cellWidth();
    const int   cols = p->getLengthSteps();

    int currentStep = -1;
    if (transport.isPlaying() && cols > 0)
        currentStep = ((int) std::floor (transport.getPlayheadBeats() / p->stepDurationBeats())) % cols;

    // Full-height highlight column under the playing step.
    if (currentStep >= 0)
    {
        g.setColour (Palette::accent.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (headerWidth + currentStep * cw, 0.0f, cw,
                                            (float) (channels.size() * rowHeight)));
    }

    for (int i = 0; i < (int) channels.size(); ++i)
    {
        const Channel* ch = channels[(size_t) i].get();
        const int y = i * rowHeight;
        auto rowArea = juce::Rectangle<int> (0, y, getWidth(), rowHeight);

        // Header background (kept slightly distinct from the step area).
        g.setColour ((i % 2 == 0) ? Palette::panelAlt : Palette::panel);
        g.fillRect (0, y, headerWidth, rowHeight);
        g.setColour (ch->colour);
        g.fillRect (0, y + 3, 4, rowHeight - 6);

        if (i == selectedChannel)
        {
            g.setColour (Palette::accent.withAlpha (0.14f));
            g.fillRect (0, y, headerWidth, rowHeight);
        }

        g.setColour (i == selectedChannel ? Palette::textBright : Palette::text);
        g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        g.drawText (ch->name, 12, y + 3, headerWidth - 82, 17,
                    juce::Justification::centredLeft, true);

        for (int s = 0; s < cols; ++s)
        {
            juce::Rectangle<float> cell (headerWidth + s * cw + 2.0f, y + 5.0f,
                                         cw - 4.0f, rowHeight - 10.0f);
            const bool on   = p->isStepOn (i, s);
            const bool beat = (s % 4 == 0);

            if (on)
            {
                g.setColour (ch->colour);
                g.fillRoundedRectangle (cell, 3.0f);
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRoundedRectangle (cell.removeFromTop (cell.getHeight() * 0.45f), 3.0f);
            }
            else
            {
                g.setColour (beat ? Palette::header : Palette::inset);
                g.fillRoundedRectangle (cell, 3.0f);
            }

            if (s == currentStep)
            {
                g.setColour (Palette::accent);
                g.drawRoundedRectangle (cell, 3.0f, 1.4f);
            }
        }

        g.setColour (Palette::lineSoft);
        g.drawHorizontalLine (y + rowHeight - 1, 0.0f, (float) getWidth());
    }

    // Divider between header column and step grid.
    g.setColour (Palette::line);
    g.drawVerticalLine (headerWidth, 0.0f, (float) (channels.size() * rowHeight));
}

void ChannelRackView::timerCallback()
{
    if (transport.isPlaying())
        repaint();
}
