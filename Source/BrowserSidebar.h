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
    (seed a new project), Demos (open a bundled example composition), Plugins (add
    an instrument plugin track), and Samples (import an audio file as a track).
    More (Presets / Favorites) plug in the same way. */
class BrowserSidebar : public juce::Component
{
public:
    struct Category
    {
        juce::String                                title;
        std::function<std::vector<juce::String>()>  getItems;   // labels for this category
        std::function<void (const juce::String&)>   onChoose;   // a row (label) was left-clicked
        std::function<void (const juce::String&)>   onFavorite; // optional: right-click action (add/remove favorite)
        juce::String                                favLabel;   // menu text for the right-click action
        std::function<juce::String (const juce::String&)> dragDescription; // optional: label -> drag payload ("kind\tref\tlabel")
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
        g.fillAll (Palette::panel);                           // own surface shade, so the seam can be soft
        g.setColour (Palette::lineSoft);                      // subtle right seam
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
        g.setColour (Palette::accent);                        // active-tab underline (per its own row)
        if (active >= 0 && active < (int) tabButtons.size())
        {
            auto tb = tabButtons[(size_t) active]->getBounds();
            g.fillRect (tb.getX(), tb.getBottom() - 2, tb.getWidth(), 2);
        }
        if (rows.empty())
        {
            g.setColour (Palette::textDim);
            g.setFont (12.0f);
            g.drawText ("(empty)", getLocalBounds().withTrimmedTop (tabBarHeight()).reduced (12, 8),
                        juce::Justification::centredTop);
        }
    }

    void resized() override
    {
        // Grid the tabs: one row up to 4, two rows beyond, so labels stay readable
        // instead of truncating ("Templates" -> "Te...") as categories are added.
        const int n     = juce::jmax (1, (int) tabButtons.size());
        const int nrows = tabRowCount();
        const int cols  = (n + nrows - 1) / nrows;
        const int tw    = (getWidth() - 1) / cols;
        for (int i = 0; i < (int) tabButtons.size(); ++i)
        {
            const int row = i / cols, col = i % cols;
            tabButtons[(size_t) i]->setBounds (col * tw, row * headerH, tw, headerH);
        }

        auto r = getLocalBounds();
        r.removeFromTop (tabBarHeight());
        r.removeFromRight (1);                                // border
        viewport.setBounds (r);
        layoutRows();
    }

    int tabRowCount()  const { return (int) tabButtons.size() > 4 ? 2 : 1; }
    int tabBarHeight() const { return headerH * tabRowCount(); }

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
            auto b = std::make_unique<RowButton> (name);
            b->setColour (juce::TextButton::buttonColourId, Palette::panelAlt);
            b->setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            b->setColour (juce::TextButton::textColourOffId, Palette::text);
            const auto onChoose = cat.onChoose;
            b->onClick = [onChoose, name] { if (onChoose) onChoose (name); };
            if (cat.dragDescription)                         // draggable into the arrange view
                b->dragDesc = cat.dragDescription (name);
            if (cat.onFavorite)                              // right-click -> add/remove favorite
            {
                const auto onFav = cat.onFavorite;
                const auto favLabel = cat.favLabel.isNotEmpty() ? cat.favLabel : juce::String ("Favorite");
                b->onRightClick = [onFav, favLabel, name]
                {
                    juce::PopupMenu m;
                    m.addItem (1, favLabel);
                    m.showMenuAsync (juce::PopupMenu::Options(),
                                     [onFav, name] (int r) { if (r == 1 && onFav) onFav (name); });
                };
            }
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

    // A browser row: left-click runs onClick (TextButton), right-click runs onRightClick,
    // and a left-drag past the threshold starts a drag with dragDesc (into the arrange view).
    struct RowButton : juce::TextButton
    {
        using juce::TextButton::TextButton;
        std::function<void()> onRightClick;
        juce::String dragDesc;                            // payload; empty = not draggable
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (onRightClick && (e.mods.isPopupMenu()))   // right-click / ctrl-click
            { onRightClick(); return; }
            juce::TextButton::mouseDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragDesc.isNotEmpty() && ! dragging
                && e.getDistanceFromDragStart() > 6 && ! e.mods.isPopupMenu())
                if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
                { dragging = true; c->startDragging (dragDesc, this); return; }
            juce::TextButton::mouseDrag (e);
        }
        void mouseUp (const juce::MouseEvent& e) override { dragging = false; juce::TextButton::mouseUp (e); }
        bool dragging { false };
    };

    static constexpr int headerH = 30;
    static constexpr int rowH    = 30;

    std::vector<Category> categories;
    int active { 0 };
    std::vector<std::unique_ptr<juce::TextButton>> tabButtons;

    juce::Viewport viewport;
    juce::Component itemHolder;
    std::vector<std::unique_ptr<juce::TextButton>> rows;
};
