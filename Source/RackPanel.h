// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <functional>
#include <cmath>
#include "Palette.h"

/** Rack panel — the selected track's *macros* (the "rack" layer), shown in the bottom area as a
    row of labelled encoders. A macro is one perceptual knob (0..1) that drives its mapped
    synth/effect params within their authored safe ranges, so one turn moves a whole sound. The
    owner wires the callbacks to the api* macro calls for the selected track; call refresh() when
    the selection or macro set changes. A light timer re-pulls the values so the knobs also track
    changes made elsewhere (a script `randomize-macros`, another window, project load). Add/turn
    macros, map/rename/remove them (the per-knob ⋯ menu, incl. MIDI-learn), Randomize the rack,
    and store/recall snapshots (variations) of the whole knob set. The owner does the actual work
    via the api* calls; this panel is just the surface. */
class RackPanel : public juce::Component, private juce::Timer
{
public:
    struct MacroInfo { juce::String name; float value; int mappingCount; };

    RackPanel()
    {
        title.setJustificationType (juce::Justification::centredLeft);
        title.setColour (juce::Label::textColourId, Palette::textDim);
        title.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (title);

        clipBtn.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x80")) + " Clip");
        clipBtn.onClick = [this] { if (onShowClip) onShowClip(); };
        addAndMakeVisible (clipBtn);

        addBtn.setButtonText ("+ Macro");
        addBtn.setTooltip ("Add a macro (a perceptual encoder) to this track");
        addBtn.onClick = [this] { if (onAddMacro) { onAddMacro(); refresh(); } };
        addAndMakeVisible (addBtn);

        randomBtn.setButtonText ("Randomize");
        randomBtn.setTooltip ("Roll every macro to a fresh random value — musical, since each stays in its safe range");
        randomBtn.onClick = [this] { if (onRandomize) { onRandomize(); refresh(); } };
        addAndMakeVisible (randomBtn);

        storeBtn.setButtonText ("+ Snapshot");
        storeBtn.setTooltip ("Save the current knob positions as a snapshot you can recall later");
        storeBtn.onClick = [this] { if (onStoreSnapshot) { onStoreSnapshot(); refresh(); } };
        addAndMakeVisible (storeBtn);

        startTimerHz (12);   // live feedback: track values changed elsewhere
    }

    // Owner callbacks — routed to the selected track's macros.
    std::function<juce::String()>            getTitle;    // "MACROS  •  TRACK"
    std::function<std::vector<MacroInfo>()>  getMacros;   // one entry per macro
    std::function<void()>                    onAddMacro;  // add a macro to the track
    std::function<void()>                    onRandomize; // roll all macros to fresh random values
    std::function<void (int, float)>         onSetValue;  // (macro index, value 0..1)
    std::function<void (int, juce::Component*)> onMacroMenu; // (macro index, anchor) map/rename/remove
    std::function<void()>                    onShowClip;  // switch back to the clip editor
    // Snapshots (rack variations).
    std::function<std::vector<juce::String>()> getSnapshots;   // slot names, in order
    std::function<void()>                    onStoreSnapshot;  // capture current knobs as a new snapshot
    std::function<void (int)>                onRecallSnapshot; // recall snapshot i
    std::function<void (int, juce::Component*)> onSnapshotMenu; // (snapshot i, anchor) overwrite/rename/delete

    // Standalone mode (a detached rack window): no "Clip" button, since there's no clip editor to
    // return to. (Reserved for a later slice; the bottom-area view never sets it.)
    void setStandalone (bool s) { standalone = s; clipBtn.setVisible (! s); resized(); }

    void refresh()
    {
        title.setText (getTitle ? getTitle() : "MACROS", juce::dontSendNotification);
        macros = getMacros ? getMacros() : std::vector<MacroInfo>{};
        randomBtn.setEnabled (! macros.empty());   // nothing to roll on an empty rack
        storeBtn.setEnabled  (! macros.empty());
        storeBtn.setVisible  (! macros.empty());
        knobs.clear();
        menuBtns.clear();
        for (int i = 0; i < (int) macros.size(); ++i)
        {
            auto s = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            s->setRange (0.0, 1.0, 0.0);
            s->setValue (macros[(size_t) i].value, juce::dontSendNotification);
            s->setTooltip (macros[(size_t) i].name + juce::String::fromUTF8 ("  (\xe2\x8b\xaf to map / rename / remove)"));
            auto* raw = s.get();
            s->onValueChange = [this, i, raw] { if (onSetValue) onSetValue (i, (float) raw->getValue()); setActiveSnap (-1); };
            addAndMakeVisible (*s);
            knobs.push_back (std::move (s));

            // A small "⋯" affordance in the cell's top-right opens the map/rename/remove menu.
            auto b = std::make_unique<juce::TextButton> (juce::String (juce::CharPointer_UTF8 ("\xe2\x8b\xaf")));
            b->setTooltip ("Map this macro to a parameter, rename, or remove it");
            b->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            b->setColour (juce::TextButton::textColourOffId, Palette::textDim);
            auto* rawB = b.get();
            b->onClick = [this, i, rawB] { if (onMacroMenu) onMacroMenu (i, rawB); };
            addAndMakeVisible (*b);
            menuBtns.push_back (std::move (b));
        }

        // Snapshot slots: one recall button per saved state (left-click recalls, right-click manages).
        snapNames = (getSnapshots && ! macros.empty()) ? getSnapshots() : std::vector<juce::String>{};
        if (activeSnap >= (int) snapNames.size()) activeSnap = -1;
        snapBtns.clear();
        for (int i = 0; i < (int) snapNames.size(); ++i)
        {
            auto b = std::make_unique<juce::TextButton> (snapNames[(size_t) i]);
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            b->setToggleState (i == activeSnap, juce::dontSendNotification);
            b->setTooltip (snapNames[(size_t) i] + juce::String::fromUTF8 (" — click to recall, right-click to manage"));
            auto* rawB = b.get();
            b->onClick = [this, i] { if (onRecallSnapshot) { onRecallSnapshot (i); setActiveSnap (i); } };
            b->addMouseListener (this, false);   // catch right-click for the manage menu
            addAndMakeVisible (*b);
            snapBtns.push_back (std::move (b));
        }
        resized();
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        for (int i = 0; i < (int) snapBtns.size(); ++i)
            if (e.eventComponent == snapBtns[(size_t) i].get())
            { if (onSnapshotMenu) onSnapshotMenu (i, snapBtns[(size_t) i].get()); return; }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::panel);
        g.setColour (Palette::header);  g.fillRect (getLocalBounds().removeFromTop (26));
        g.setColour (Palette::line);    g.fillRect (0, 25, getWidth(), 1);
        if (knobs.empty())
        {
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("No macros on this track \xe2\x80\x94 click \"+ Macro\" to add one")),
                        getLocalBounds().withTrimmedTop (60), juce::Justification::centredTop, false);
            return;
        }
        // Under each knob: the macro name, and a small hint of how many params it drives.
        for (int i = 0; i < (int) knobs.size() && i < (int) macros.size(); ++i)
        {
            auto r = knobs[(size_t) i]->getBounds();
            g.setColour (Palette::text);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (macros[(size_t) i].name, r.getX() - 8, r.getBottom() - 1, r.getWidth() + 16, 13,
                        juce::Justification::centred, false);
            const int mc = macros[(size_t) i].mappingCount;
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (mc == 0 ? juce::String ("unmapped")
                                : juce::String (mc) + (mc == 1 ? " param" : " params"),
                        r.getX() - 8, r.getBottom() + 11, r.getWidth() + 16, 12,
                        juce::Justification::centred, false);
        }
        // Snapshot-strip hint until the first snapshot exists.
        if (! knobs.empty() && snapNames.empty())
        {
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("no snapshots yet \xe2\x80\x94 \"+ Snapshot\" saves these knob positions")),
                        112, 30, getWidth() - 122, 22, juce::Justification::centredLeft, false);
        }
    }

    void resized() override
    {
        auto a = getLocalBounds();
        auto h = a.removeFromTop (26).reduced (4, 3);
        if (! standalone) { clipBtn.setBounds (h.removeFromLeft (54)); h.removeFromLeft (8); }
        title.setBounds (h.removeFromLeft (juce::jmin (200, juce::jmax (0, h.getWidth() - 180))));
        addBtn.setBounds (h.removeFromRight (78));
        h.removeFromRight (6);
        randomBtn.setBounds (h.removeFromRight (84));

        // Snapshot strip (rack variations): + Snapshot, then a row of recall slots.
        if (! macros.empty())
        {
            auto sr = a.removeFromTop (30).reduced (8, 4);
            storeBtn.setBounds (sr.removeFromLeft (92));
            sr.removeFromLeft (10);
            for (auto& b : snapBtns)
            {
                if (sr.getWidth() < 56) { b->setBounds (0, 0, 0, 0); continue; }   // overflow: hide
                b->setBounds (sr.removeFromLeft (juce::jmin (112, sr.getWidth())).reduced (2, 0));
                sr.removeFromLeft (2);
            }
        }

        // Encoder grid: a ⋯ menu button (top-right), knob, name + "N params" hint per cell.
        auto grid = a.reduced (12, 10);
        const int kw = 76, kh = 96, knobH = kh - 34;
        int x = grid.getX(), y = grid.getY();
        for (int i = 0; i < (int) knobs.size(); ++i)
        {
            if (x + kw > grid.getRight()) { x = grid.getX(); y += kh; }
            knobs[(size_t) i]->setBounds (x, y, kw, knobH);
            if (i < (int) menuBtns.size()) menuBtns[(size_t) i]->setBounds (x + kw - 20, y - 2, 20, 16);
            x += kw + 8;
        }
    }

private:
    void setActiveSnap (int i)
    {
        if (activeSnap == i) return;
        activeSnap = i;
        for (int k = 0; k < (int) snapBtns.size(); ++k)
            snapBtns[(size_t) k]->setToggleState (k == activeSnap, juce::dontSendNotification);
    }

    void timerCallback() override
    {
        if (! isShowing() || ! getMacros) return;
        auto now = getMacros();
        if ((int) now.size() != (int) knobs.size()) { refresh(); return; }   // added/removed elsewhere
        if (getSnapshots && (int) getSnapshots().size() != (int) snapBtns.size()) { refresh(); return; }  // snapshot set changed
        bool changed = false;
        for (int i = 0; i < (int) now.size(); ++i)
        {
            if (std::abs (now[(size_t) i].value - (float) knobs[(size_t) i]->getValue()) > 1.0e-4f)
            { knobs[(size_t) i]->setValue (now[(size_t) i].value, juce::dontSendNotification); changed = true; }
            if (i < (int) macros.size()
                && (now[(size_t) i].name != macros[(size_t) i].name
                    || now[(size_t) i].mappingCount != macros[(size_t) i].mappingCount)) changed = true;
        }
        if (changed) { macros = std::move (now); repaint(); }
    }

    juce::Label      title;
    juce::TextButton clipBtn, addBtn, randomBtn, storeBtn;
    std::vector<std::unique_ptr<juce::Slider>>     knobs;
    std::vector<std::unique_ptr<juce::TextButton>> menuBtns;   // per-macro ⋯ map/rename/remove
    std::vector<std::unique_ptr<juce::TextButton>> snapBtns;   // snapshot recall slots
    std::vector<MacroInfo>    macros;
    std::vector<juce::String> snapNames;
    int  activeSnap { -1 };    // last recalled slot (highlighted; cleared when a knob moves)
    bool standalone { false };
};
