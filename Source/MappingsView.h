// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <functional>
#include "Palette.h"

// A single place to see and remove every control mapping in the project: MIDI/OSC
// controller maps (source -> ParamModel target) and modulation routes (LFO -> target).
// The backend (Controllers.cpp / Modulation.cpp) owns the data; this view is a thin
// client — the owner fills rows via setRows() and handles onRemove().
class MappingsView : public juce::Component
{
public:
    struct Row { juce::String text, kind, key; };   // kind: "ctrl" | "mod"; key: source (ctrl) / target (mod)

    std::function<void (const juce::String& kind, const juce::String& key)> onRemove;

    MappingsView()
    {
        addAndMakeVisible (title);
        title.setText ("Mappings", juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        addAndMakeVisible (empty);
        empty.setText ("No controller maps or LFO routes yet.", juce::dontSendNotification);
        empty.setColour (juce::Label::textColourId, Palette::textDim);
        empty.setJustificationType (juce::Justification::centred);
    }

    void setRows (std::vector<Row> newRows)
    {
        rows = std::move (newRows);
        labels.clear();
        removeButtons.clear();
        for (auto& r : rows)
        {
            auto* lab = new juce::Label();
            lab->setText (r.text, juce::dontSendNotification);
            lab->setFont (juce::Font (juce::FontOptions (14.0f)));
            addAndMakeVisible (lab);
            labels.add (lab);

            auto* btn = new juce::TextButton ("Remove");
            const juce::String kind = r.kind, key = r.key;
            btn->onClick = [this, kind, key] { if (onRemove) onRemove (kind, key); };
            addAndMakeVisible (btn);
            removeButtons.add (btn);
        }
        empty.setVisible (rows.empty());
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override { g.fillAll (Palette::bg); }

    void resized() override
    {
        auto b = getLocalBounds().reduced (12);
        title.setBounds (b.removeFromTop (28));
        b.removeFromTop (6);
        empty.setBounds (b);
        const int rowH = 30;
        for (int i = 0; i < labels.size(); ++i)
        {
            auto row = b.removeFromTop (rowH);
            removeButtons[i]->setBounds (row.removeFromRight (80).reduced (2));
            labels[i]->setBounds (row);
            b.removeFromTop (4);
        }
    }

private:
    juce::Label title, empty;
    std::vector<Row> rows;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::TextButton> removeButtons;
};
