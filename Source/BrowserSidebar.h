// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>
#include "Palette.h"

/** A collapsible left-hand browser panel. First slice: a "Templates" list — each
    built-in / user template is a clickable row that seeds a new project via
    onChooseTemplate. The category header is drawn as a tab bar so more categories
    (Demos / Samples / Plugins) can be added as follow-ups. */
class BrowserSidebar : public juce::Component
{
public:
    /** Populate the current category's rows (labels). */
    std::function<std::vector<juce::String>()> getTemplates;
    /** A row was clicked (the template/item name). */
    std::function<void (const juce::String&)>  onChooseTemplate;

    BrowserSidebar()
    {
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&itemHolder, false);
        viewport.setScrollBarsShown (true, false);
    }

    /** Rebuild the list from getTemplates(). Call after the model changes / on show. */
    void refresh()
    {
        rows.clear();
        itemHolder.removeAllChildren();
        const auto items = getTemplates ? getTemplates() : std::vector<juce::String>();
        for (auto& name : items)
        {
            auto b = std::make_unique<juce::TextButton> (name);
            b->setColour (juce::TextButton::buttonColourId, Palette::panelAlt);
            b->setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            b->setColour (juce::TextButton::textColourOffId, Palette::text);
            b->setTriggeredOnMouseDown (false);
            b->onClick = [this, name] { if (onChooseTemplate) onChooseTemplate (name); };
            itemHolder.addAndMakeVisible (*b);
            rows.push_back (std::move (b));
        }
        layoutRows();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::bg);
        // Header / (single) tab.
        auto hdr = getLocalBounds().removeFromTop (headerH);
        g.setColour (Palette::header);
        g.fillRect (hdr);
        g.setColour (Palette::accent);
        g.fillRect (hdr.removeFromBottom (2));                 // active-tab underline
        g.setColour (Palette::textBright);
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText ("TEMPLATES", getLocalBounds().removeFromTop (headerH).reduced (12, 0),
                    juce::Justification::centredLeft);
        // Right border.
        g.setColour (Palette::line);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
        if (rows.empty())
        {
            g.setColour (Palette::textDim);
            g.setFont (12.0f);
            g.drawText ("(no templates)", getLocalBounds().withTrimmedTop (headerH).reduced (12, 8),
                        juce::Justification::centredTop);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (headerH);
        r.removeFromRight (1);                                 // border
        viewport.setBounds (r);
        layoutRows();
    }

private:
    void layoutRows()
    {
        const int w = juce::jmax (10, viewport.getWidth() - 2);
        for (int i = 0; i < (int) rows.size(); ++i)
            rows[(size_t) i]->setBounds (3, i * rowH + 3, w - 6, rowH - 4);
        itemHolder.setSize (w, juce::jmax (viewport.getHeight(), (int) rows.size() * rowH + 6));
    }

    static constexpr int headerH = 30;
    static constexpr int rowH    = 30;

    juce::Viewport viewport;
    juce::Component itemHolder;
    std::vector<std::unique_ptr<juce::TextButton>> rows;
};
