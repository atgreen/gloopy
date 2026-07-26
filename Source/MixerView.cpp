// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#include "MixerView.h"
#include "Palette.h"

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
        mute.setColour (juce::TextButton::buttonOnColourId, Palette::mute);
        mute.onClick = [this] { track->mute.store (mute.getToggleState()); };
        addAndMakeVisible (mute);

        solo.setClickingTogglesState (true);
        solo.setToggleState (t->solo.load(), juce::dontSendNotification);
        solo.setColour (juce::TextButton::buttonOnColourId, Palette::solo);
        solo.setColour (juce::TextButton::textColourOnId, Palette::bg);
        solo.onClick = [this] { track->solo.store (solo.getToggleState()); };
        addAndMakeVisible (solo);

        fx.onClick = [this] { owner.showFxMenu (index); };
        addAndMakeVisible (fx);

        // Right-click the fader/pan to MIDI-learn that ParamModel target. The sliders
        // ignore right-clicks for dragging, so forwarding them here is safe.
        fader.addMouseListener (this, false);
        pan.addMouseListener (this, false);
        // Right-click the strip name -> control-group (VCA-lite) menu. Not for master.
        name.addMouseListener (this, false);
    }

    void updateMeter()
    {
        meterL = juce::jmax (track->peakL.load(), meterL * 0.82f);
        meterR = juce::jmax (track->peakR.load(), meterR * 0.82f);
        repaint (meterArea);
        repaint (clipLedArea);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Right-click on the fader or pan -> param menu (MIDI Learn / LFO) for that target.
        if (e.mods.isPopupMenu() && (e.eventComponent == &fader || e.eventComponent == &pan))
        {
            const juce::String field  = (e.eventComponent == &pan) ? "pan" : "volume";
            const juce::String target = "insert/" + juce::String (index) + "/" + field;
            owner.showParamMenu (target, track->name + "  " + field);
            return;
        }

        // Right-click the strip name -> control-group menu (inserts only, not master).
        if (e.mods.isPopupMenu() && e.eventComponent == &name && index > 0)
        {
            owner.showGroupMenu (index);
            return;
        }

        if (e.eventComponent == this && clipLedArea.contains (e.getPosition()))
        { track->clipped.store (false); repaint (clipLedArea); }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (index == 0 ? Palette::header : Palette::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 4.0f);
        g.setColour (index == 0 ? Palette::accent.withAlpha (0.5f) : Palette::line);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 4.0f, 1.0f);

        // Clip LED (red when the insert has hit >= 0 dBFS; click to reset).
        g.setColour (track->clipped.load() ? Palette::red : Palette::inset.brighter (0.15f));
        g.fillRect (clipLedArea.reduced (1));

        // Meter.
        g.setColour (Palette::inset);
        g.fillRect (meterArea);
        auto drawBar = [&] (juce::Rectangle<int> r, float v)
        {
            const float level = juce::jlimit (0.0f, 1.0f, v);
            const int h = (int) (r.getHeight() * level);
            auto bar = r.removeFromBottom (h);
            g.setColour (level > 0.9f ? Palette::red
                       : level > 0.6f ? Palette::solo : Palette::green);
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
        auto meterCol = a.removeFromRight (12);
        clipLedArea = meterCol.removeFromTop (6);
        meterArea = meterCol;
        fader.setBounds (a);
    }

    MixerTrack* track;
    int index;

private:
    MixerView& owner;
    juce::Label name;
    juce::Slider fader, pan;
    juce::TextButton mute { "M" }, solo { "S" }, fx { "FX" };
    juce::Rectangle<int> meterArea, clipLedArea;
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
        if (onBeforeStructuralChange) onBeforeStructuralChange();   // close any plugin editors
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

    openEditorButton.onClick = [this]
    {
        if (selectedTrack < 0 || onOpenPluginEditor == nullptr) return;
        juce::AudioProcessor* proc = nullptr;
        juce::String name;
        {
            const juce::ScopedLock sl (engineLock);
            auto& fx = tracks[(size_t) selectedTrack]->effects;
            if (juce::isPositiveAndBelow (selectedEffect, (int) fx.size()))
            {
                proc = fx[(size_t) selectedEffect]->getPluginInstance();
                name = fx[(size_t) selectedEffect]->name();
            }
        }
        if (proc != nullptr) onOpenPluginEditor (proc, name);
    };
    addChildComponent (openEditorButton);

    rebuild();
    startTimerHz (24);
}

MixerView::~MixerView() { stopTimer(); }

void MixerView::rebuild()
{
    selectedTrack  = -1;
    selectedEffect = -1;
    rebuildEditor();

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

    if (ensurePlugins) ensurePlugins();
    const auto pluginList = getEffectPlugins ? getEffectPlugins() : juce::Array<juce::PluginDescription>();

    juce::PopupMenu menu, addMenu, pluginMenu;
    const auto types = EffectFactory::types();
    for (int t = 0; t < types.size(); ++t)
        addMenu.addItem (1000 + t, types[t]);
    menu.addSubMenu ("Add effect", addMenu);

    if (! pluginList.isEmpty())
    {
        for (int i = 0; i < pluginList.size(); ++i)
            pluginMenu.addItem (2000 + i, pluginList[i].name + "  (" + pluginList[i].pluginFormatName + ")");
        menu.addSubMenu ("Add plugin", pluginMenu);
    }

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
        [this, trackIndex, types, pluginList] (int result)
        {
            if (result == 0) return;
            std::unique_ptr<Effect> fx;
            if (result >= 2000 && result - 2000 < pluginList.size())
            {
                if (makePluginEffect) fx = makePluginEffect (pluginList[result - 2000]);
            }
            else if (result >= 1000)
            {
                fx = makeEffect (types[result - 1000]);
            }
            else if (result >= 100)
            {
                selectEffect (trackIndex, result - 100);
                return;
            }

            if (fx == nullptr) return;
            int newIndex = -1;
            {
                const juce::ScopedLock sl (engineLock);
                tracks[(size_t) trackIndex]->effects.push_back (std::move (fx));
                newIndex = (int) tracks[(size_t) trackIndex]->effects.size() - 1;
            }
            selectEffect (trackIndex, newIndex);
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
    paramTargets.clear();

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
        openEditorButton.setVisible (false);
        resized();
        return;
    }

    editorTitle.setText (tracks[(size_t) selectedTrack]->name + "  —  " + fx->name(),
                         juce::dontSendNotification);
    bypassButton.setVisible (true);
    bypassButton.setToggleState (fx->bypassed.load(), juce::dontSendNotification);
    removeButton.setVisible (true);
    openEditorButton.setVisible (fx->getPluginInstance() != nullptr);

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
        s->addMouseListener (this, false);   // right-click -> MIDI Learn (see mouseDown)
        addAndMakeVisible (*s);
        paramSliders.push_back (std::move (s));
        paramTargets.push_back ("effect/" + juce::String (selectedTrack) + "/"
                                + juce::String (selectedEffect) + "/" + p.name);
    }
    resized();
}

void MixerView::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) return;
    // Right-click an FX param knob -> param menu for that effect/<t>/<slot>/<param> id.
    for (size_t i = 0; i < paramSliders.size(); ++i)
        if (e.eventComponent == paramSliders[i].get() && i < paramTargets.size())
        {
            showParamMenu (paramTargets[i], paramLabels[i]->getText());
            return;
        }
}

// Shared right-click menu for any ParamModel target: MIDI Learn, add/remove an LFO.
void MixerView::showParamMenu (const juce::String& target, const juce::String& label)
{
    const juce::String mappedSrc = onControllerSourceFor ? onControllerSourceFor (target) : juce::String();

    juce::PopupMenu m;
    m.addSectionHeader (mappedSrc.isNotEmpty() ? label + "  (" + mappedSrc + ")" : label);
    m.addItem (1, "MIDI Learn");
    if (mappedSrc.isNotEmpty()) m.addItem (6, "Remove mapping (" + mappedSrc + ")");
    m.addItem (2, "Add LFO...");
    m.addItem (3, "Remove LFO");
    m.addSeparator();
    m.addItem (4, "Automate at playhead");   // drop a keyframe (current value @ playhead)
    m.addItem (5, "Clear automation");
    const bool stepped = getAutomationStep ? getAutomationStep (target) : false;
    m.addItem (7, "Stepped automation", true, stepped);   // hold each value vs linear ramp
    const juce::String tgt = target;
    m.showMenuAsync (juce::PopupMenu::Options(), [this, tgt, stepped] (int r)
    {
        if      (r == 1 && onMidiLearn)          onMidiLearn (tgt);
        else if (r == 6 && onRemoveControllerMap) onRemoveControllerMap (tgt);
        else if (r == 2)                         promptAddLfo (tgt);
        else if (r == 3 && onRemoveModulation)   onRemoveModulation (tgt);
        else if (r == 4 && onAutomatePoint)      onAutomatePoint (tgt);
        else if (r == 5 && onClearAutomation)    onClearAutomation (tgt);
        else if (r == 7 && onSetAutomationStep)  onSetAutomationStep (tgt, ! stepped);
    });
}

// LFO prompt: rate (Hz), depth (param units), shape. Centre is resolved by the owner
// from the target's current value. Calls onSetModulation with the same shape as the API.
void MixerView::promptAddLfo (const juce::String& target)
{
    auto* aw = new juce::AlertWindow ("Add LFO", target, juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("rate",  "2.0",  "Rate (Hz)");
    aw->addTextEditor ("depth", "0.25", "Depth");
    aw->addTextEditor ("sync",  "0",    "Sync (beats, 0=free)");
    aw->addTextEditor ("phase", "0",    "Phase (0..1)");
    aw->addTextEditor ("slew",  "0",    "Smooth (ms, 0=off)");
    juce::StringArray shapes { "Sine", "Triangle", "Saw", "Square", "Random (S&H)" };
    aw->addComboBox ("shape", shapes, "Shape");
    juce::StringArray polar { "Bipolar (+/- depth)", "Unipolar (0..depth)" };
    aw->addComboBox ("polar", polar, "Range");
    aw->addButton ("Add",    1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, target] (int r)
    {
        // "Add LFO..." stacks: each invocation appends another source on the target
        // (multiple sources sum). Clear them all with "Remove LFO".
        if (r == 1 && onAddModulation)
        {
            const float rate  = aw->getTextEditorContents ("rate").getFloatValue();
            const float depth = aw->getTextEditorContents ("depth").getFloatValue();
            const float sync  = aw->getTextEditorContents ("sync").getFloatValue();
            const float phase = aw->getTextEditorContents ("phase").getFloatValue();
            const float slew  = aw->getTextEditorContents ("slew").getFloatValue();
            const int   shape = aw->getComboBoxComponent ("shape")->getSelectedItemIndex();
            const bool  uni   = aw->getComboBoxComponent ("polar")->getSelectedItemIndex() == 1;
            if (rate > 0.0f || sync > 0.0f)
                onAddModulation (target, rate, depth, juce::jmax (0, shape), juce::jmax (0.0f, sync),
                                 juce::jmax (0.0f, phase), uni, juce::jmax (0.0f, slew));
        }
        delete aw;
    }), false);
}

// VCA-lite: right-clicking a strip name opens this. Create/assign a control group,
// ride its fader (quantised choices keep it a quick menu), mute or delete it.
void MixerView::showGroupMenu (int insertIndex)
{
    if (! onListGroups || ! onAssignGroup) return;
    const auto groups  = onListGroups();
    const juce::String cur = onInsertGroup ? onInsertGroup (insertIndex) : juce::String();

    juce::PopupMenu m;
    m.addSectionHeader (cur.isNotEmpty() ? "Group: " + cur : "No control group");
    m.addItem (1, "New group...");

    juce::PopupMenu assign;
    for (int i = 0; i < (int) groups.size(); ++i)
        assign.addItem (200 + i, groups[(size_t) i].name, true, groups[(size_t) i].name == cur);
    if (groups.empty()) assign.addItem (999, "(none yet)", false, false);
    m.addSubMenu ("Assign to", assign);

    if (cur.isNotEmpty())
    {
        m.addItem (2, "Remove from group");
        juce::PopupMenu gain;
        const int pcts[] = { 0, 25, 50, 75, 100 };
        for (int i = 0; i < 5; ++i) gain.addItem (300 + i, juce::String (pcts[i]) + "%");
        m.addSubMenu ("Group gain", gain);
        bool muted = false;
        for (auto& gs : groups) if (gs.name == cur) muted = gs.mute;
        m.addItem (3, "Mute group", true, muted);
        m.addSeparator();
        m.addItem (4, "Delete group");
    }

    m.showMenuAsync (juce::PopupMenu::Options(), [this, insertIndex, cur, groups] (int r)
    {
        if (r == 0) return;
        if (r == 1) { promptNewGroup (insertIndex); return; }
        if (r == 2) { onAssignGroup (insertIndex, {}); return; }
        if (r == 3 && onGroupMute)
        {
            bool muted = false;
            for (auto& gs : groups) if (gs.name == cur) muted = gs.mute;
            onGroupMute (cur, ! muted);
            return;
        }
        if (r == 4 && onRemoveGroup) { onRemoveGroup (cur); return; }
        if (r >= 200 && r < 300) { onAssignGroup (insertIndex, groups[(size_t) (r - 200)].name); return; }
        if (r >= 300 && r < 305 && onGroupGain)
        {
            const float pct[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
            onGroupGain (cur, pct[r - 300]);
        }
    });
}

void MixerView::promptNewGroup (int insertIndex)
{
    auto* aw = new juce::AlertWindow ("New control group", "Group name", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", "VCA", "Name");
    aw->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, insertIndex] (int r)
    {
        if (r == 1 && onAssignGroup)
        {
            const juce::String nm = aw->getTextEditorContents ("name").trim();
            if (nm.isNotEmpty()) onAssignGroup (insertIndex, nm);
        }
        delete aw;
    }), false);
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (Palette::bg);

    // Effect editor panel.
    auto ed = juce::Rectangle<int> (0, getHeight() - editorHeight, getWidth(), editorHeight);
    g.setColour (Palette::panel);
    g.fillRect (ed);
    g.setColour (Palette::line);
    g.fillRect (ed.getX(), ed.getY(), ed.getWidth(), 1);
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
    editorTitle.setBounds (top.removeFromLeft (240));
    removeButton.setBounds (top.removeFromRight (70));
    bypassButton.setBounds (top.removeFromRight (76).withTrimmedRight (6));
    openEditorButton.setBounds (top.removeFromRight (86).withTrimmedRight (6));

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
