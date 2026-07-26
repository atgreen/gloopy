// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>
#include "Palette.h"

/** A collapsible left-hand browser panel with tabbed categories. Each category
    lists items (labels) and opens the clicked one. Categories so far: Templates
    (seed a new project) and Demos (open a bundled example composition). More
    (Samples / Plugins / Presets / Favorites) plug in the same way. */
class BrowserSidebar : public juce::Component
{
public:
    struct Category
    {
        juce::String                                title;
        std::function<std::vector<juce::String>()>  getItems;   // labels for this category
        std::function<void (const juce::String&)>   onChoose;   // a row (label) was clicked
    };

    BrowserSidebar()
    {
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&itemHolder, false);
        viewport.setScrollBarsShown (true, false);
    }

    /** Install the categories and build their tabs. Activates the first one. */
    void setCategories (std::vector<Category> cats)
    {
        categories = std::move (cats);
        tabButtons.clear();
        for (int i = 0; i < (int) categories.size(); ++i)
        {
            auto b = std::make_unique<juce::TextButton> (categories[(size_t) i].title);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7001);
            b->setColour (juce::TextButton::buttonOnColourId, Palette::header);
            b->setColour (juce::TextButton::buttonColourId,   Palette::bg);
            b->setColour (juce::TextButton::textColourOffId,  Palette::textDim);
            b->setColour (juce::TextButton::textColourOnId,   Palette::textBright);
            b->onClick = [this, i] { setActive (i); };
            addAndMakeVisible (*b);
            tabButtons.push_back (std::move (b));
        }
        setActive (0);
    }

    /** Repopulate the current tab (call on show / after the model changes). */
    void refresh() { populate(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::bg);
        g.setColour (Palette::line);                          // right border + tab-bar underline
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
        g.setColour (Palette::accent);                        // active-tab underline
        if (active >= 0 && active < (int) tabButtons.size())
        {
            auto tb = tabButtons[(size_t) active]->getBounds();
            g.fillRect (tb.getX(), headerH - 2, tb.getWidth(), 2);
        }
        if (rows.empty())
        {
            g.setColour (Palette::textDim);
            g.setFont (12.0f);
            g.drawText ("(empty)", getLocalBounds().withTrimmedTop (headerH).reduced (12, 8),
                        juce::Justification::centredTop);
        }
    }

    void resized() override
    {
        auto hdr = getLocalBounds().removeFromTop (headerH);
        const int n = juce::jmax (1, (int) tabButtons.size());
        const int tw = (hdr.getWidth() - 1) / n;
        for (int i = 0; i < (int) tabButtons.size(); ++i)
            tabButtons[(size_t) i]->setBounds (i * tw, 0, tw, headerH);

        auto r = getLocalBounds();
        r.removeFromTop (headerH);
        r.removeFromRight (1);                                // border
        viewport.setBounds (r);
        layoutRows();
    }

private:
    void setActive (int i)
    {
        active = i;
        if (i >= 0 && i < (int) tabButtons.size())
            tabButtons[(size_t) i]->setToggleState (true, juce::dontSendNotification);
        populate();
        repaint();
    }

    void populate()
    {
        rows.clear();
        itemHolder.removeAllChildren();
        if (active < 0 || active >= (int) categories.size()) { layoutRows(); return; }
        const auto& cat = categories[(size_t) active];
        const auto items = cat.getItems ? cat.getItems() : std::vector<juce::String>();
        for (auto& name : items)
        {
            auto b = std::make_unique<juce::TextButton> (name);
            b->setColour (juce::TextButton::buttonColourId, Palette::panelAlt);
            b->setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            b->setColour (juce::TextButton::textColourOffId, Palette::text);
            const auto onChoose = cat.onChoose;
            b->onClick = [onChoose, name] { if (onChoose) onChoose (name); };
            itemHolder.addAndMakeVisible (*b);
            rows.push_back (std::move (b));
        }
        layoutRows();
        repaint();
    }

    void layoutRows()
    {
        const int w = juce::jmax (10, viewport.getWidth() - 2);
        for (int i = 0; i < (int) rows.size(); ++i)
            rows[(size_t) i]->setBounds (3, i * rowH + 3, w - 6, rowH - 4);
        itemHolder.setSize (w, juce::jmax (viewport.getHeight(), (int) rows.size() * rowH + 6));
    }

    static constexpr int headerH = 30;
    static constexpr int rowH    = 30;

    std::vector<Category> categories;
    int active { 0 };
    std::vector<std::unique_ptr<juce::TextButton>> tabButtons;

    juce::Viewport viewport;
    juce::Component itemHolder;
    std::vector<std::unique_ptr<juce::TextButton>> rows;
};
