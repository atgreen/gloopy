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
    // kind: "ctrl" | "mod". key = source (ctrl) / target (mod). For a ctrl row, source/target/
    // lo/hi/bypass carry the editable state so the row can offer Bypass + Edit-range in place.
    struct Row { juce::String text, kind, key, source, target; float lo { 0.0f }, hi { 1.0f }; bool bypass { false }; };

    std::function<void (const juce::String& kind, const juce::String& key)> onRemove;
    std::function<void (const juce::String& source, const juce::String& target, bool bypass)> onSetBypass;
    std::function<void (const juce::String& source, const juce::String& target, float lo, float hi)> onSetRange;

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
        bypassButtons.clear();
        editButtons.clear();
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

            // Controller maps also get in-place Bypass + Edit-range (LFO routes don't).
            const bool isCtrl = r.kind == "ctrl";
            auto* byp = new juce::TextButton (r.bypass ? "Bypassed" : "Bypass");
            if (isCtrl)
            {
                const juce::String src = r.source, tgt = r.target; const bool cur = r.bypass;
                byp->onClick = [this, src, tgt, cur] { if (onSetBypass) onSetBypass (src, tgt, ! cur); };
                addAndMakeVisible (byp);
            }
            bypassButtons.add (byp);

            auto* ed = new juce::TextButton ("Range...");
            if (isCtrl)
            {
                const juce::String src = r.source, tgt = r.target; const float lo = r.lo, hi = r.hi;
                ed->onClick = [this, src, tgt, lo, hi] { promptRange (src, tgt, lo, hi); };
                addAndMakeVisible (ed);
            }
            editButtons.add (ed);
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
            if (rows[(size_t) i].kind == "ctrl")   // controller rows also show Bypass + Range
            {
                bypassButtons[i]->setBounds (row.removeFromRight (84).reduced (2));
                editButtons[i]  ->setBounds (row.removeFromRight (74).reduced (2));
            }
            labels[i]->setBounds (row);
            b.removeFromTop (4);
        }
    }

private:
    // Edit a controller map's output range in place (lo>hi inverts). Reuses the AddControllerMap
    // upsert via onSetRange, so no new backend — just a small prompt.
    void promptRange (const juce::String& source, const juce::String& target, float lo, float hi)
    {
        auto* aw = new juce::AlertWindow ("Map range", source + " -> " + target + "\n(swap Low/High to invert)",
                                          juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("lo", juce::String (lo, 4), "Low (value at CC 0)");
        aw->addTextEditor ("hi", juce::String (hi, 4), "High (value at CC 127)");
        aw->addButton ("Apply",  1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, source, target] (int r)
        {
            if (r == 1 && onSetRange)
                onSetRange (source, target, aw->getTextEditorContents ("lo").getFloatValue(),
                                            aw->getTextEditorContents ("hi").getFloatValue());
            delete aw;
        }), false);
    }

    juce::Label title, empty;
    std::vector<Row> rows;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::TextButton> removeButtons, bypassButtons, editButtons;
};
