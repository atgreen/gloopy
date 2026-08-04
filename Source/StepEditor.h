// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <functional>
#include <algorithm>
#include <cmath>
#include "Note.h"
#include "Transport.h"
#include "Palette.h"

/** A multi-lane step grid — the Hydrogen-style drum view, and a pure VIEW over one
    clip's note list.

    Each row is one voice of the selected track's instrument (a drum-kit pad): a name
    gutter on the left, a line of step cells on the right, a cell lit where that voice
    has a note at that step. A kit track supplies its pads via Generator::voices(), so
    the whole groove reads at once; a single-voice (melodic) track gets one row at its
    default pitch. Every row edits the SAME note list — the clip's — just at its own
    pitch, so toggling a cell adds/removes one note there and the piano roll (which
    edits the same list) stays in sync. The owner writes getNotes() back to the clip. */
class StepEditor : public juce::Component,
                   private juce::Timer
{
public:
    struct Voice
    {
        juce::String label;
        juce::Colour colour { Palette::accent };
        int          pitch  { 60 };   // cells on this row read/write notes at this pitch
    };

    explicit StepEditor (Transport& t) : transport (t) { startTimerHz (30); }
    ~StepEditor() override { stopTimer(); }

    std::function<void()> onNotesChanged;

    /** Set the voice rows, the clip's note list to edit, and the step window (beats). */
    void setVoices (std::vector<Voice> v, std::vector<Note> clipNotes, double contentLenBeats)
    {
        voices     = std::move (v);
        notes      = std::move (clipNotes);
        contentLen = juce::jmax (0.25, contentLenBeats);
        repaint();
    }

    /** The clip's full note list — what the owner writes back. */
    const std::vector<Note>& getNotes() const noexcept { return notes; }

    void setEnabledEditing (bool e) { editable = e; repaint(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::inset);

        if (! editable || voices.empty())
        {
            drawEmptyState (g);
            return;
        }

        const int   N   = numSteps();
        const int   R   = (int) voices.size();
        const float top = 4.0f, bot = 4.0f;
        const float rh  = ((float) getHeight() - top - bot) / (float) R;
        const float cw  = gridWidth() / (float) N;

        int cur = -1;
        if (transport.isPlaying())
            cur = (int) std::floor (std::fmod (transport.getPlayheadBeats(), contentLen) / stepDur()) % N;

        for (int r = 0; r < R; ++r)
        {
            const auto& voice = voices[(size_t) r];
            const float ry    = top + (float) r * rh;

            // Row band: alternating shade so lanes read apart.
            g.setColour ((r % 2 == 0) ? Palette::panel : Palette::panelAlt);
            g.fillRect (juce::Rectangle<float> (0.0f, ry, (float) getWidth(), rh));

            // Name gutter: colour tab + voice label.
            g.setColour (voice.colour.withAlpha (0.95f));
            g.fillRect (juce::Rectangle<float> (0.0f, ry + 2.0f, 3.0f, rh - 4.0f));
            g.setColour (Palette::text);
            g.setFont (juce::FontOptions (12.0f, juce::Font::plain));
            g.drawText (voice.label, juce::Rectangle<int> (9, (int) ry, gutter - 13, (int) rh),
                        juce::Justification::centredLeft, true);

            // Step cells.
            for (int s = 0; s < N; ++s)
            {
                juce::Rectangle<float> cell ((float) gutter + (float) s * cw + 2.0f, ry + 3.0f,
                                             cw - 4.0f, rh - 6.0f);
                const bool on   = stepOn (voice.pitch, s);
                const bool beat = (s % 4 == 0);

                if (on)
                {
                    g.setColour (voice.colour);
                    g.fillRoundedRectangle (cell, 3.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.18f));
                    g.fillRoundedRectangle (cell.withHeight (cell.getHeight() * 0.4f), 3.0f);
                }
                else
                {
                    g.setColour (beat ? Palette::header : Palette::inset);
                    g.fillRoundedRectangle (cell, 3.0f);
                }

                if (s == cur)
                {
                    g.setColour (Palette::playhead);
                    g.drawRoundedRectangle (cell, 3.0f, 1.4f);
                }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! editable || voices.empty())
            return;

        const int   R   = (int) voices.size();
        const float top = 4.0f, bot = 4.0f;
        const float rh  = ((float) getHeight() - top - bot) / (float) R;
        const int   r   = (int) ((e.position.y - top) / rh);
        if (! juce::isPositiveAndBelow (r, R))
            return;

        const int   N  = numSteps();
        const float cw = gridWidth() / (float) N;
        const int   s  = (int) ((e.position.x - (float) gutter) / cw);
        if (! juce::isPositiveAndBelow (s, N))
            return;

        const int  pitch  = voices[(size_t) r].pitch;
        const auto before = notes.size();
        notes.erase (std::remove_if (notes.begin(), notes.end(),
                        [this, s, pitch] (const Note& n)
                        { return n.pitch == pitch && stepOf (n.startBeat.toBeats()) == s; }),
                     notes.end());
        if (notes.size() == before)                                    // nothing removed → add one
            notes.push_back ({ pitch, (double) s * stepDur(), stepDur(), 0.85f });

        if (onNotesChanged) onNotesChanged();
        repaint();
    }

private:
    void timerCallback() override { if (transport.isPlaying()) repaint(); }

    float  gridWidth() const { return (float) getWidth() - (float) gutter - 4.0f; }
    int    numSteps()  const { return juce::jlimit (1, 64, (int) std::round (contentLen * 4.0)); }
    double stepDur()   const { return contentLen / (double) numSteps(); }
    int    stepOf (double beat) const { return (int) std::llround (beat / stepDur()); }

    bool stepOn (int pitch, int s) const
    {
        for (const auto& n : notes)
            if (n.pitch == pitch && stepOf (n.startBeat.toBeats()) == s)
                return true;
        return false;
    }

    void drawEmptyState (juce::Graphics& g)
    {
        const auto ctr = getLocalBounds().toFloat().getCentre();
        const float cw = 22.0f, ch = 26.0f, gap = 9.0f, total = 8 * cw + 7 * gap;
        float x = ctr.x - total * 0.5f;
        for (int i = 0; i < 8; ++i)
        {
            juce::Rectangle<float> cell (x, ctr.y - ch - 16.0f, cw, ch);
            g.setColour (Palette::textDim.withAlpha (0.16f));
            g.drawRoundedRectangle (cell, 4.0f, 1.4f);
            if (i % 4 == 0) { g.setColour (Palette::accent.withAlpha (0.18f)); g.fillRoundedRectangle (cell.reduced (4.5f), 3.0f); }
            x += cw + gap;
        }
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Select an instrument clip to edit its steps",
                    getLocalBounds().withTrimmedTop ((int) ctr.y - 2), juce::Justification::centredTop, false);
    }

    static constexpr int gutter = 66;   // left name-gutter width (px)

    Transport& transport;
    std::vector<Voice> voices;
    std::vector<Note>  notes;           // the clip's notes (shared across rows, one pitch each)
    double contentLen  { 4.0 };
    bool   editable    { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepEditor)
};
