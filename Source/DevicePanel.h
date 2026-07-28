// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <functional>
#include "Palette.h"

/** Device panel — the selected track's effect chain, shown in the bottom area (the Ableton
    "Device View"): add / remove / bypass effects and edit the selected device's parameters as
    knobs, without opening the mixer. The owner wires the callbacks to the api* effect calls for
    the selected track's mixer insert; call refresh() when the selection or chain changes. */
class DevicePanel : public juce::Component
{
public:
    struct Param { juce::String name; float value, min, max; };

    DevicePanel()
    {
        title.setJustificationType (juce::Justification::centredLeft);
        title.setColour (juce::Label::textColourId, Palette::textDim);
        title.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (title);

        clipBtn.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x80")) + " Clip");
        clipBtn.onClick = [this] { if (onShowClip) onShowClip(); };
        addAndMakeVisible (clipBtn);

        addBtn.setButtonText ("+ Device");
        addBtn.onClick = [this] { showAddMenu(); };
        addAndMakeVisible (addBtn);

        bypassBtn.setButtonText ("Bypass");
        bypassBtn.setClickingTogglesState (true);
        bypassBtn.setColour (juce::TextButton::buttonOnColourId, Palette::warm);
        bypassBtn.onClick = [this] { if (selectedSlot >= 0 && onSetBypass) onSetBypass (selectedSlot, bypassBtn.getToggleState()); };
        addAndMakeVisible (bypassBtn);

        removeBtn.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")));   // ×
        removeBtn.onClick = [this] { if (selectedSlot >= 0 && onRemoveEffect) { onRemoveEffect (selectedSlot); selectedSlot = -1; refresh(); } };
        addAndMakeVisible (removeBtn);

        addChildComponent (scope);   // shown only when the selected device is a Scope
    }

    // Owner callbacks — routed to the selected track's mixer insert.
    std::function<juce::String()> getTitle;                                   // "DEVICES  •  TRACK"
    std::function<std::vector<std::pair<juce::String, bool>>()> getChain;     // (effect name, bypassed) per slot
    std::function<juce::StringArray()> getEffectTypes;                        // built-in effect type names
    std::function<void (int)> onAddEffect;                                    // add by type index
    std::function<void (int)> onRemoveEffect;                                 // remove slot
    std::function<void (int, bool)> onSetBypass;                              // (slot, bypassed)
    std::function<std::vector<Param> (int)> getParams;                        // params of a slot
    std::function<void (int, const juce::String&, float)> onSetParam;         // (slot, name, value)
    std::function<std::vector<float> (int)> getAnalyzerData;                  // scope/analyzer snapshot for a slot
    std::function<void()> onShowClip;                                         // switch back to the clip editor

    // Standalone mode (a detached device-chain window): no "Clip" button, since there's no clip
    // editor to return to. The chain is pinned to one insert regardless of selection.
    void setStandalone (bool s) { standalone = s; clipBtn.setVisible (! s); resized(); }

    void refresh()
    {
        title.setText (getTitle ? getTitle() : "DEVICES", juce::dontSendNotification);
        deviceBtns.clear();
        const auto chain = getChain ? getChain() : std::vector<std::pair<juce::String, bool>>{};
        if (selectedSlot >= (int) chain.size()) selectedSlot = chain.empty() ? -1 : (int) chain.size() - 1;
        for (int i = 0; i < (int) chain.size(); ++i)
        {
            auto b = std::make_unique<juce::TextButton> (chain[(size_t) i].first);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7001);
            b->setToggleState (i == selectedSlot, juce::dontSendNotification);
            b->setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            if (chain[(size_t) i].second) b->setColour (juce::TextButton::textColourOffId, Palette::textDim);  // bypassed = dim
            b->onClick = [this, i] { selectedSlot = i; rebuildParams(); resized(); repaint(); };
            addAndMakeVisible (*b);
            deviceBtns.push_back (std::move (b));
        }
        rebuildParams();
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::panel);
        g.setColour (Palette::header);
        g.fillRect (getLocalBounds().removeFromTop (26));
        g.setColour (Palette::line);
        g.fillRect (0, 25, getWidth(), 1);
        if (deviceBtns.empty())
        {
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("No devices on this track \xe2\x80\x94 click \"+ Device\" to add one")),
                        getLocalBounds().withTrimmedTop (60), juce::Justification::centredTop, false);
        }
        // Value readouts under each param knob.
        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (10.0f));
        for (int i = 0; i < (int) paramSliders.size(); ++i)
        {
            auto r = paramSliders[(size_t) i]->getBounds();
            g.setColour (Palette::text);
            g.drawText (paramNames[(size_t) i], r.getX() - 6, r.getBottom() - 2, r.getWidth() + 12, 14,
                        juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        auto a = getLocalBounds();
        auto h = a.removeFromTop (26).reduced (4, 3);
        if (! standalone) { clipBtn.setBounds (h.removeFromLeft (54)); h.removeFromLeft (8); }
        title.setBounds (h.removeFromLeft (juce::jmin (240, h.getWidth() - 200)));
        addBtn.setBounds (h.removeFromRight (78)); h.removeFromRight (6);
        removeBtn.setBounds (h.removeFromRight (30)); h.removeFromRight (4);
        bypassBtn.setBounds (h.removeFromRight (64));

        // Device chain row.
        auto chainRow = a.removeFromTop (30).reduced (6, 4);
        for (auto& b : deviceBtns) { b->setBounds (chainRow.removeFromLeft (110).reduced (2, 0)); chainRow.removeFromLeft (2); }

        // Scope display fills the device body when a Scope is selected (it has no knobs).
        if (scope.isVisible()) { scope.setBounds (a.reduced (10, 8)); return; }

        // Param knobs grid.
        auto grid = a.reduced (10, 8);
        const int kw = 66, kh = 76;
        int x = grid.getX(), y = grid.getY();
        for (auto& s : paramSliders)
        {
            if (x + kw > grid.getRight()) { x = grid.getX(); y += kh; }
            s->setBounds (x, y, kw, kh - 14);
            x += kw + 6;
        }
    }

private:
    // Live oscilloscope display for a Scope analyzer device — polls its snapshot and draws
    // the waveform. Passive (a Timer), so it never touches the audio thread directly.
    struct ScopeView : juce::Component, juce::Timer
    {
        std::function<std::vector<float>()> pull;   // fetch the latest snapshot
        std::vector<float> data;
        ScopeView() { startTimerHz (24); }
        void timerCallback() override { if (isShowing() && pull) { data = pull(); repaint(); } }
        void paint (juce::Graphics& g) override
        {
            g.setColour (Palette::bg);           g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
            g.setColour (Palette::lineSoft);     g.drawHorizontalLine (getHeight() / 2, 0.0f, (float) getWidth());
            if (data.size() < 2) { g.setColour (Palette::textDim); g.setFont (11.0f);
                                   g.drawText ("scope — play to see the signal", getLocalBounds(), juce::Justification::centred); return; }
            const float w = (float) getWidth(), h = (float) getHeight(), mid = h * 0.5f;
            juce::Path p;
            for (int i = 0; i < (int) data.size(); ++i)
            {
                const float x = w * (float) i / (float) (data.size() - 1);
                const float y = mid - juce::jlimit (-1.0f, 1.0f, data[(size_t) i]) * (mid - 2.0f);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (Palette::accent);
            g.strokePath (p, juce::PathStrokeType (1.4f));
        }
    };

    void rebuildParams()
    {
        paramSliders.clear();
        paramNames.clear();
        bypassBtn.setEnabled (selectedSlot >= 0);
        removeBtn.setEnabled (selectedSlot >= 0);
        // Reflect the selected device's bypass state.
        const auto chain = getChain ? getChain() : std::vector<std::pair<juce::String, bool>>{};
        // Show the scope display iff the selected device is a Scope analyzer.
        const bool isScope = selectedSlot >= 0 && selectedSlot < (int) chain.size()
                             && chain[(size_t) selectedSlot].first == "Scope";
        scope.setVisible (isScope);
        if (isScope)
            scope.pull = [this, slot = selectedSlot] { return getAnalyzerData ? getAnalyzerData (slot) : std::vector<float>{}; };
        if (selectedSlot < 0 || ! getParams) { repaint(); return; }
        if (selectedSlot < (int) chain.size()) bypassBtn.setToggleState (chain[(size_t) selectedSlot].second, juce::dontSendNotification);

        for (const auto& p : getParams (selectedSlot))
        {
            auto s = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            s->setRange (p.min, p.max, 0.0);
            s->setValue (p.value, juce::dontSendNotification);
            const juce::String name = p.name;
            auto* raw = s.get();
            s->onValueChange = [this, name, raw] { if (selectedSlot >= 0 && onSetParam) onSetParam (selectedSlot, name, (float) raw->getValue()); };
            addAndMakeVisible (*s);
            paramSliders.push_back (std::move (s));
            paramNames.push_back (name);
        }
    }

    void showAddMenu()
    {
        juce::PopupMenu m;
        const auto types = getEffectTypes ? getEffectTypes() : juce::StringArray{};
        for (int i = 0; i < types.size(); ++i) m.addItem (i + 1, types[i]);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addBtn),
                         [this] (int r) { if (r > 0 && onAddEffect) { onAddEffect (r - 1); selectedSlot = 9999; refresh(); } });
    }

    juce::Label      title;
    juce::TextButton clipBtn, addBtn, bypassBtn, removeBtn;
    std::vector<std::unique_ptr<juce::TextButton>> deviceBtns;
    std::vector<std::unique_ptr<juce::Slider>>     paramSliders;
    std::vector<juce::String>                      paramNames;
    ScopeView scope;
    int selectedSlot { -1 };
    bool standalone { false };
};
