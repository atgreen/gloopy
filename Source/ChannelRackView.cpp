#include "ChannelRackView.h"

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
    g.fillAll (juce::Colour (0xff202024));

    auto* p = getPattern();
    if (p == nullptr)
        return;

    const float cw   = cellWidth();
    const int   cols = p->getLengthSteps();

    int currentStep = -1;
    if (transport.isPlaying() && cols > 0)
        currentStep = ((int) std::floor (transport.getPlayheadBeats() / p->stepDurationBeats())) % cols;

    for (int i = 0; i < (int) channels.size(); ++i)
    {
        const Channel* ch = channels[(size_t) i].get();
        const int y = i * rowHeight;
        auto rowArea = juce::Rectangle<int> (0, y, getWidth(), rowHeight);

        g.setColour ((i % 2 == 0) ? juce::Colour (0xff26262b) : juce::Colour (0xff222227));
        g.fillRect (rowArea);
        g.setColour (ch->colour.withAlpha (0.85f));
        g.fillRect (0, y, 5, rowHeight);

        if (i == selectedChannel)
        {
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRect (rowArea);
        }

        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (ch->name, 14, y + 3, headerWidth - 84, 18,
                    juce::Justification::centredLeft, true);

        for (int s = 0; s < cols; ++s)
        {
            juce::Rectangle<float> cell (headerWidth + s * cw + 1.5f, y + 4.0f,
                                         cw - 3.0f, rowHeight - 8.0f);
            const bool on   = p->isStepOn (i, s);
            const bool beat = (s % 4 == 0);

            g.setColour (on ? ch->colour
                            : (beat ? juce::Colour (0xff34343c) : juce::Colour (0xff2b2b31)));
            g.fillRoundedRectangle (cell, 3.0f);

            if (s == currentStep)
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.drawRoundedRectangle (cell, 3.0f, 1.6f);
            }
        }

        g.setColour (juce::Colour (0xff17171a));
        g.drawHorizontalLine (y + rowHeight - 1, 0.0f, (float) getWidth());
    }
}

void ChannelRackView::timerCallback()
{
    if (transport.isPlaying())
        repaint();
}
