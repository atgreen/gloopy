#include "MixerView.h"

// ===========================================================================
// Strip
// ===========================================================================
class MixerView::Strip : public juce::Component
{
public:
    Strip (MixerTrack* t, int idx, MixerView& own) : track (t), index (idx), owner (own)
    {
        name.setText (t->name, juce::dontSendNotification);
        name.setJustificationType (juce::Justification::centred);
        name.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (name);

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (0.0, 1.0, 0.01);
        fader.setValue (t->volume.load(), juce::dontSendNotification);
        fader.onValueChange = [this] { track->volume.store ((float) fader.getValue()); };
        addAndMakeVisible (fader);

        pan.setSliderStyle (juce::Slider::LinearHorizontal);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setValue (t->pan.load(), juce::dontSendNotification);
        pan.onValueChange = [this] { track->pan.store ((float) pan.getValue()); };
        addAndMakeVisible (pan);

        mute.setClickingTogglesState (true);
        mute.setToggleState (t->mute.load(), juce::dontSendNotification);
        mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb03030));
        mute.onClick = [this] { track->mute.store (mute.getToggleState()); };
        addAndMakeVisible (mute);

        solo.setClickingTogglesState (true);
        solo.setToggleState (t->solo.load(), juce::dontSendNotification);
        solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb0a030));
        solo.onClick = [this] { track->solo.store (solo.getToggleState()); };
        addAndMakeVisible (solo);

        fx.onClick = [this] { owner.showFxMenu (index); };
        addAndMakeVisible (fx);
    }

    void updateMeter()
    {
        meterL = juce::jmax (track->peakL.load(), meterL * 0.82f);
        meterR = juce::jmax (track->peakR.load(), meterR * 0.82f);
        repaint (meterArea);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (index == 0 ? juce::Colour (0xff2a2a33) : juce::Colour (0xff232329));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 4.0f);

        // Meter.
        g.setColour (juce::Colour (0xff101013));
        g.fillRect (meterArea);
        auto drawBar = [&] (juce::Rectangle<int> r, float v)
        {
            const float level = juce::jlimit (0.0f, 1.0f, v);
            const int h = (int) (r.getHeight() * level);
            auto bar = r.removeFromBottom (h);
            g.setColour (level > 0.9f ? juce::Colours::red
                       : level > 0.6f ? juce::Colours::yellow : juce::Colours::limegreen);
            g.fillRect (bar);
        };
        auto m = meterArea.reduced (1);
        drawBar (m.removeFromLeft (m.getWidth() / 2).reduced (1, 0), meterL);
        drawBar (m.reduced (1, 0), meterR);
    }

    void resized() override
    {
        auto a = getLocalBounds().reduced (5, 4);
        name.setBounds (a.removeFromTop (16));
        fx.setBounds   (a.removeFromBottom (20));
        auto ms = a.removeFromBottom (20);
        mute.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (1, 0));
        solo.setBounds (ms.reduced (1, 0));
        pan.setBounds  (a.removeFromBottom (16));
        a.removeFromBottom (4);
        meterArea = a.removeFromRight (12);
        fader.setBounds (a);
    }

    MixerTrack* track;
    int index;

private:
    MixerView& owner;
    juce::Label name;
    juce::Slider fader, pan;
    juce::TextButton mute { "M" }, solo { "S" }, fx { "FX" };
    juce::Rectangle<int> meterArea;
    float meterL { 0.0f }, meterR { 0.0f };
};

// ===========================================================================
// MixerView
// ===========================================================================
MixerView::MixerView (std::vector<std::unique_ptr<MixerTrack>>& tracksRef,
                      juce::CriticalSection& engineLockRef,
                      std::function<std::unique_ptr<Effect> (const juce::String&)> makeEffectFn)
    : tracks (tracksRef), engineLock (engineLockRef), makeEffect (std::move (makeEffectFn))
{
    stripViewport.setViewedComponent (&stripHolder, false);
    stripViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (stripViewport);

    editorTitle.setText ("Select a track's FX to edit", juce::dontSendNotification);
    editorTitle.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    editorTitle.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0d0));
    addAndMakeVisible (editorTitle);

    bypassButton.setClickingTogglesState (true);
    bypassButton.onClick = [this]
    {
        if (selectedTrack < 0) return;
        const juce::ScopedLock sl (engineLock);
        auto& fx = tracks[(size_t) selectedTrack]->effects;
        if (juce::isPositiveAndBelow (selectedEffect, (int) fx.size()))
            fx[(size_t) selectedEffect]->bypassed.store (bypassButton.getToggleState());
    };
    addChildComponent (bypassButton);

    removeButton.onClick = [this]
    {
        if (selectedTrack < 0) return;
        {
            const juce::ScopedLock sl (engineLock);
            auto& fx = tracks[(size_t) selectedTrack]->effects;
            if (juce::isPositiveAndBelow (selectedEffect, (int) fx.size()))
                fx.erase (fx.begin() + selectedEffect);
        }
        selectedEffect = -1;
        rebuildEditor();
    };
    addChildComponent (removeButton);

    rebuild();
    startTimerHz (24);
}

MixerView::~MixerView() { stopTimer(); }

void MixerView::rebuild()
{
    strips.clear();
    stripHolder.removeAllChildren();
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        auto s = std::make_unique<Strip> (tracks[(size_t) i].get(), i, *this);
        stripHolder.addAndMakeVisible (*s);
        strips.push_back (std::move (s));
    }
    resized();
}

void MixerView::showFxMenu (int trackIndex)
{
    if (! juce::isPositiveAndBelow (trackIndex, (int) tracks.size()))
        return;

    juce::PopupMenu menu, addMenu;
    const auto types = EffectFactory::types();
    for (int t = 0; t < types.size(); ++t)
        addMenu.addItem (1000 + t, types[t]);
    menu.addSubMenu ("Add effect", addMenu);

    {
        const juce::ScopedLock sl (engineLock);
        auto& fx = tracks[(size_t) trackIndex]->effects;
        if (! fx.empty())
        {
            menu.addSeparator();
            for (int i = 0; i < (int) fx.size(); ++i)
                menu.addItem (100 + i, "Edit: " + fx[(size_t) i]->name()
                                        + (fx[(size_t) i]->bypassed.load() ? "  (bypassed)" : ""));
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options(),
        [this, trackIndex, types] (int result)
        {
            if (result == 0) return;
            if (result >= 1000)
            {
                auto fx = makeEffect (types[result - 1000]);
                if (fx == nullptr) return;
                int newIndex = -1;
                {
                    const juce::ScopedLock sl (engineLock);
                    tracks[(size_t) trackIndex]->effects.push_back (std::move (fx));
                    newIndex = (int) tracks[(size_t) trackIndex]->effects.size() - 1;
                }
                selectEffect (trackIndex, newIndex);
            }
            else if (result >= 100)
            {
                selectEffect (trackIndex, result - 100);
            }
        });
}

void MixerView::selectEffect (int trackIndex, int effectIndex)
{
    selectedTrack  = trackIndex;
    selectedEffect = effectIndex;
    rebuildEditor();
}

void MixerView::rebuildEditor()
{
    paramSliders.clear();
    paramLabels.clear();

    Effect* fx = nullptr;
    if (juce::isPositiveAndBelow (selectedTrack, (int) tracks.size()))
    {
        const juce::ScopedLock sl (engineLock);
        auto& list = tracks[(size_t) selectedTrack]->effects;
        if (juce::isPositiveAndBelow (selectedEffect, (int) list.size()))
            fx = list[(size_t) selectedEffect].get();
    }

    if (fx == nullptr)
    {
        editorTitle.setText ("Select a track's FX to edit", juce::dontSendNotification);
        bypassButton.setVisible (false);
        removeButton.setVisible (false);
        resized();
        return;
    }

    editorTitle.setText (tracks[(size_t) selectedTrack]->name + "  —  " + fx->name(),
                         juce::dontSendNotification);
    bypassButton.setVisible (true);
    bypassButton.setToggleState (fx->bypassed.load(), juce::dontSendNotification);
    removeButton.setVisible (true);

    for (auto& p : fx->parameters())
    {
        auto label = std::make_unique<juce::Label> (juce::String(), p.name);
        label->setJustificationType (juce::Justification::centred);
        label->setFont (juce::FontOptions (11.0f));
        addAndMakeVisible (*label);
        paramLabels.push_back (std::move (label));

        auto s = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag,
                                                 juce::Slider::TextBoxBelow);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        s->setRange (p.minValue, p.maxValue, (p.maxValue - p.minValue) / 1000.0);
        s->setValue (p.get(), juce::dontSendNotification);
        auto setter = p.set;
        s->onValueChange = [sl = s.get(), setter] { setter ((float) sl->getValue()); };
        addAndMakeVisible (*s);
        paramSliders.push_back (std::move (s));
    }
    resized();
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff17171b));
    g.setColour (juce::Colour (0xff0e0e11));
    g.fillRect (0, getHeight() - editorHeight, getWidth(), editorHeight);
}

void MixerView::resized()
{
    auto area = getLocalBounds();
    auto editor = area.removeFromBottom (editorHeight).reduced (8, 6);

    stripViewport.setBounds (area);
    stripHolder.setSize (juce::jmax (area.getWidth(), (int) strips.size() * stripWidth),
                         area.getHeight());
    for (int i = 0; i < (int) strips.size(); ++i)
        strips[(size_t) i]->setBounds (i * stripWidth, 0, stripWidth, stripHolder.getHeight());

    auto top = editor.removeFromTop (20);
    editorTitle.setBounds (top.removeFromLeft (260));
    removeButton.setBounds (top.removeFromRight (70));
    bypassButton.setBounds (top.removeFromRight (76).withTrimmedRight (6));

    editor.removeFromTop (4);
    for (int i = 0; i < (int) paramSliders.size(); ++i)
    {
        auto cell = editor.removeFromLeft (84);
        paramLabels[(size_t) i]->setBounds (cell.removeFromTop (14));
        paramSliders[(size_t) i]->setBounds (cell);
        editor.removeFromLeft (6);
    }
}

void MixerView::timerCallback()
{
    for (auto& s : strips)
        s->updateMeter();
}
