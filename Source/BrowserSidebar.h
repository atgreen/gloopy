// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <set>
#include <vector>
#include "Palette.h"

/** A collapsible left-hand browser panel. Categories (Templates, Demos, Plugins,
    Samples, Presets) are selected by an external activity rail; this panel shows the
    active category's items. A filter box narrows the list live, and items whose labels
    are "Group/Name" (e.g. Surge presets: "Basses/Stone") are shown under collapsible
    group headers so a few hundred patches stay navigable. */
class BrowserSidebar : public juce::Component
{
public:
    struct Category
    {
        juce::String                                title;
        std::function<std::vector<juce::String>()>  getItems;   // labels for this category
        std::function<void (const juce::String&)>   onChoose;   // a row was left-clicked (full label)
        std::function<void (const juce::String&)>   onFavorite; // optional: right-click action
        juce::String                                favLabel;   // menu text for the right-click action
        std::function<juce::String (const juce::String&)> dragDescription; // label -> drag payload
    };

    BrowserSidebar()
    {
        filterBox.setTextToShowWhenEmpty ("Filter…", Palette::textDim);
        filterBox.setColour (juce::TextEditor::backgroundColourId, Palette::panelAlt);
        filterBox.setColour (juce::TextEditor::textColourId,       Palette::textBright);
        filterBox.setColour (juce::TextEditor::outlineColourId,    juce::Colours::transparentBlack);
        filterBox.setColour (juce::TextEditor::focusedOutlineColourId, Palette::accentDim);
        filterBox.onTextChange = [this] { populate(); };
        filterBox.setJustification (juce::Justification::centredLeft);
        addAndMakeVisible (filterBox);

        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&itemHolder, false);
        viewport.setScrollBarsShown (true, false);
    }

    void setCategories (std::vector<Category> cats)
    {
        categories = std::move (cats);
        tabButtons.clear();
        for (int i = 0; showTabBar && i < (int) categories.size(); ++i)
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

    void refresh() { populate(); }

    void selectCategory (int i) { setActive (i); }
    int          numCategories()      const { return (int) categories.size(); }
    juce::String categoryTitle (int i) const
    { return (i >= 0 && i < (int) categories.size()) ? categories[(size_t) i].title : juce::String(); }

    void setTabBarVisible (bool v) { showTabBar = v; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::panel);
        g.setColour (Palette::lineSoft);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
        g.setColour (Palette::accent);
        if (active >= 0 && active < (int) tabButtons.size())
        {
            auto tb = tabButtons[(size_t) active]->getBounds();
            g.fillRect (tb.getX(), tb.getBottom() - 2, tb.getWidth(), 2);
        }
        if (entries.empty())
        {
            g.setColour (Palette::textDim);
            g.setFont (12.0f);
            g.drawText (filterBox.isEmpty() ? "(empty)" : "no matches",
                        viewport.getBounds().reduced (12, 8), juce::Justification::centredTop);
        }
    }

    void resized() override
    {
        const int n     = juce::jmax (1, (int) tabButtons.size());
        const int nrows = tabRowCount();
        const int cols  = (n + nrows - 1) / nrows;
        const int tw    = (getWidth() - 1) / cols;
        for (int i = 0; i < (int) tabButtons.size(); ++i)
            tabButtons[(size_t) i]->setBounds ((i % cols) * tw, (i / cols) * headerH, tw, headerH);

        auto r = getLocalBounds();
        r.removeFromTop (tabBarHeight());
        r.removeFromRight (1);
        filterBox.setBounds (r.removeFromTop (filterH).reduced (4, 3));
        viewport.setBounds (r);
        layoutRows();
    }

    int tabRowCount()  const { return (int) tabButtons.size() > 4 ? 2 : 1; }
    int tabBarHeight() const { return showTabBar ? headerH * tabRowCount() : 0; }

private:
    void setActive (int i)
    {
        active = i;
        if (i >= 0 && i < (int) tabButtons.size())
            tabButtons[(size_t) i]->setToggleState (true, juce::dontSendNotification);
        filterBox.setText ({}, juce::dontSendNotification);   // fresh filter per category
        populate();
        repaint();
    }

    bool groupCollapsed (const juce::String& g) const { return collapsed.count (g) != 0; }

    void populate()
    {
        entries.clear();
        itemHolder.removeAllChildren();
        if (active < 0 || active >= (int) categories.size()) { layoutRows(); return; }

        const auto& cat  = categories[(size_t) active];
        const auto  items = cat.getItems ? cat.getItems() : std::vector<juce::String>();
        const auto  filt = filterBox.getText().trim().toLowerCase();
        const bool  grouped = std::any_of (items.begin(), items.end(),
                                           [] (const juce::String& s) { return s.containsChar ('/'); });

        juce::String curGroup;
        for (const auto& label : items)
        {
            if (filt.isNotEmpty() && ! label.toLowerCase().contains (filt)) continue;

            if (grouped)
            {
                const int slash  = label.indexOfChar ('/');
                const auto group = slash >= 0 ? label.substring (0, slash) : juce::String ("Other");
                const auto name  = slash >= 0 ? label.substring (slash + 1) : label;
                if (group != curGroup) { curGroup = group; addHeader (group); }
                if (! (groupCollapsed (group) && filt.isEmpty()))    // an active filter forces groups open
                    addItem (cat, label, name);
            }
            else
                addItem (cat, label, label);
        }
        layoutRows();
        repaint();
    }

    void addHeader (const juce::String& group)
    {
        auto h = std::make_unique<GroupHeader> (group);
        h->collapsed = groupCollapsed (group);
        h->onToggle  = [this, group]
        {
            if (! collapsed.erase (group)) collapsed.insert (group);
            populate();
        };
        itemHolder.addAndMakeVisible (*h);
        entries.push_back ({ std::move (h), true });
    }

    void addItem (const Category& cat, const juce::String& label, const juce::String& display)
    {
        auto b = std::make_unique<RowButton> (display);
        b->value = label;
        b->setColour (juce::TextButton::buttonColourId, Palette::panelAlt);
        b->setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
        b->setColour (juce::TextButton::textColourOffId, Palette::text);
        const auto onChoose = cat.onChoose;
        b->onClick = [onChoose, label] { if (onChoose) onChoose (label); };
        if (cat.dragDescription) b->dragDesc = cat.dragDescription (label);
        if (cat.onFavorite)
        {
            const auto onFav = cat.onFavorite;
            const auto favLabel = cat.favLabel.isNotEmpty() ? cat.favLabel : juce::String ("Favorite");
            b->onRightClick = [onFav, favLabel, label]
            {
                juce::PopupMenu m;
                m.addItem (1, favLabel);
                m.showMenuAsync (juce::PopupMenu::Options(),
                                 [onFav, label] (int r) { if (r == 1 && onFav) onFav (label); });
            };
        }
        itemHolder.addAndMakeVisible (*b);
        entries.push_back ({ std::move (b), false });
    }

    void layoutRows()
    {
        const int w = juce::jmax (10, viewport.getWidth() - 2);
        int y = 3;
        for (auto& e : entries)
        {
            const int h = e.isHeader ? headerRowH : rowH;
            const int indent = e.isHeader ? 0 : 12;
            e.comp->setBounds (3 + indent, y, w - 6 - indent, h - 4);
            y += h;
        }
        itemHolder.setSize (w, juce::jmax (viewport.getHeight(), y + 3));
    }

    // A section header for a group of items; click toggles collapse. Draws a disclosure
    // triangle + the group name (uppercase), styled like the app's section headers.
    struct GroupHeader : juce::Component
    {
        explicit GroupHeader (juce::String g) : group (std::move (g)) {}
        juce::String group;
        bool collapsed { false };
        std::function<void()> onToggle;
        void mouseUp (const juce::MouseEvent&) override { if (onToggle) onToggle(); }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (Palette::textDim);
            juce::Path tri;
            const float cx = 6.0f, cy = r.getCentreY(), s = 3.0f;
            if (collapsed) tri.addTriangle (cx - s, cy - s, cx + s, cy, cx - s, cy + s);       // ▸
            else           tri.addTriangle (cx - s, cy - s, cx + s, cy - s, cx, cy + s);       // ▾
            g.fillPath (tri);
            g.setColour (Palette::textBright);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (group.toUpperCase(), r.withTrimmedLeft (16.0f), juce::Justification::centredLeft);
        }
    };

    // A browser item row: left-click chooses, right-click favourites, left-drag drags out.
    struct RowButton : juce::TextButton
    {
        using juce::TextButton::TextButton;
        std::function<void()> onRightClick;
        juce::String value;      // full label (callbacks key on this, not the display text)
        juce::String dragDesc;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (onRightClick && e.mods.isPopupMenu()) { onRightClick(); return; }
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

    struct Entry { std::unique_ptr<juce::Component> comp; bool isHeader; };

    static constexpr int headerH    = 30;   // tab-bar row height
    static constexpr int rowH        = 30;   // item row height
    static constexpr int headerRowH  = 24;   // group-header row height
    static constexpr int filterH     = 30;   // filter box strip height

    std::vector<Category> categories;
    bool showTabBar { true };
    int active { 0 };
    std::vector<std::unique_ptr<juce::TextButton>> tabButtons;

    juce::TextEditor filterBox;
    juce::Viewport   viewport;
    juce::Component  itemHolder;
    std::vector<Entry> entries;
    std::set<juce::String> collapsed;   // groups the user has collapsed
};
