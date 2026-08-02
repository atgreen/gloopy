// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>
#include "Palette.h"

/** A thin, always-visible vertical "activity rail" (à la VS Code's activity bar):
    one icon per browser category. Clicking an item selects that category and shows
    the browser; clicking the item that's already active collapses the browser. Icons
    are drawn as vector shapes, so there's no dependency on an icon font. */
class ActivityRail : public juce::Component
{
public:
    static constexpr int kWidth = 44;

    std::function<void (int)> onItemClicked;   // an item (category index) was clicked

    /** Build one rail item per category. `tooltipFor` supplies each item's hover text. */
    void setItemCount (int n, const std::function<juce::String (int)>& tooltipFor)
    {
        items.clear();
        for (int i = 0; i < n; ++i)
        {
            auto b = std::make_unique<Item> (i);
            b->setTooltip (tooltipFor ? tooltipFor (i) : juce::String());
            b->onClick = [this, i] { if (onItemClicked) onItemClicked (i); };
            addAndMakeVisible (*b);
            items.push_back (std::move (b));
        }
        resized();
        repaint();
    }

    /** `i` = the selected category (-1 = none); `open` = whether the browser is shown.
        The active highlight only appears while the browser is open. */
    void setActive (int i, bool open)
    {
        active = i; browserOpen = open;
        for (auto& it : items) it->repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::panel);
        g.setColour (Palette::lineSoft);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
    }

    void resized() override
    {
        int y = 4;
        for (auto& it : items) { it->setBounds (0, y, getWidth() - 1, kWidth); y += kWidth; }
    }

private:
    struct Item : juce::Button
    {
        explicit Item (int idx) : juce::Button ({}), index (idx) {}
        int index;

        void paintButton (juce::Graphics& g, bool over, bool /*down*/) override
        {
            auto* rail = dynamic_cast<ActivityRail*> (getParentComponent());
            const bool isActive = rail != nullptr && rail->active == index && rail->browserOpen;
            if (isActive)         g.fillAll (Palette::panelAlt);
            else if (over)        g.fillAll (Palette::panelAlt.withAlpha (0.4f));
            if (isActive) { g.setColour (Palette::accent); g.fillRect (0, 0, 2, getHeight()); }
            const auto col = isActive ? Palette::textBright : (over ? Palette::text : Palette::textDim);
            drawCategoryIcon (g, index, getLocalBounds().toFloat().reduced (13.0f), col);
        }
    };

    // Simple, recognisable vector glyphs per category. Order matches setCategories():
    // 0 Templates, 1 Demos, 2 Plugins, 3 Samples, 4 Presets. Unknown -> a dot.
    static void drawCategoryIcon (juce::Graphics& g, int cat, juce::Rectangle<float> r, juce::Colour c)
    {
        g.setColour (c);
        switch (cat)
        {
            case 0:  // Templates — a page with lines
            {
                auto p = r.reduced (1.0f);
                g.drawRoundedRectangle (p, 2.0f, 1.3f);
                for (float f : { 0.38f, 0.58f, 0.78f })
                    g.drawLine (p.getX() + 3, p.getY() + p.getHeight() * f,
                                p.getRight() - 3, p.getY() + p.getHeight() * f, 1.0f);
                break;
            }
            case 1:  // Demos — a play triangle
            {
                juce::Path t;
                t.addTriangle (r.getX() + 2, r.getY(), r.getX() + 2, r.getBottom(), r.getRight() - 1, r.getCentreY());
                g.fillPath (t);
                break;
            }
            case 2:  // Plugins — two overlapping rounded squares
            {
                const float s = r.getWidth() * 0.6f;
                g.drawRoundedRectangle (r.getX(), r.getY(), s, s, 1.5f, 1.3f);
                g.drawRoundedRectangle (r.getRight() - s, r.getBottom() - s, s, s, 1.5f, 1.3f);
                break;
            }
            case 3:  // Samples — a little waveform
            {
                const int n = 5;
                const float bw = r.getWidth() / (n * 2.0f - 1.0f);
                const float hs[] = { 0.35f, 0.9f, 0.55f, 1.0f, 0.45f };
                for (int i = 0; i < n; ++i)
                {
                    const float h = r.getHeight() * hs[i];
                    g.fillRect (juce::Rectangle<float> (r.getX() + i * 2 * bw, r.getCentreY() - h / 2, bw, h));
                }
                break;
            }
            case 4:  // Presets — three sliders
            {
                for (int i = 0; i < 3; ++i)
                {
                    const float y = r.getY() + r.getHeight() * (0.2f + 0.3f * i);
                    g.drawLine (r.getX(), y, r.getRight(), y, 1.2f);
                    g.fillEllipse (r.getX() + r.getWidth() * (0.25f + 0.25f * i) - 2.0f, y - 2.0f, 4.0f, 4.0f);
                }
                break;
            }
            default:
                g.fillEllipse (r.getCentreX() - 2.0f, r.getCentreY() - 2.0f, 4.0f, 4.0f);
                break;
        }
    }

    std::vector<std::unique_ptr<Item>> items;
    int  active { -1 };
    bool browserOpen { false };
};
