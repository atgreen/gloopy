#pragma once

#include <JuceHeader.h>
#include <vector>
#include <functional>
#include <algorithm>
#include <cmath>
#include "Note.h"
#include "Transport.h"
#include "Palette.h"

/** A single-lane step grid that edits a clip's notes (the "tracker" view).
    Toggling a cell adds/removes a note at that step at the track's default
    pitch — the same note list the piano roll edits, so the two stay in sync. */
class StepEditor : public juce::Component,
                   private juce::Timer
{
public:
    explicit StepEditor (Transport& t) : transport (t) { startTimerHz (30); }
    ~StepEditor() override { stopTimer(); }

    std::function<void()> onNotesChanged;

    void setContent (double contentLenBeats, int pitch)
    {
        contentLen   = juce::jmax (0.25, contentLenBeats);
        defaultPitch = pitch;
        repaint();
    }
    void loadNotes (std::vector<Note> n) { notes = std::move (n); repaint(); }
    const std::vector<Note>& getNotes() const noexcept { return notes; }
    void setEnabledEditing (bool e) { editable = e; repaint(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::inset);
        const int N = numSteps();
        const float cw = (float) getWidth() / (float) N;

        int cur = -1;
        if (transport.isPlaying())
            cur = (int) std::floor (std::fmod (transport.getPlayheadBeats(), contentLen) / stepDur()) % N;

        for (int s = 0; s < N; ++s)
        {
            juce::Rectangle<float> cell (s * cw + 3.0f, 8.0f, cw - 6.0f, (float) getHeight() - 16.0f);
            const bool on   = isStepOn (s);
            const bool beat = (s % 4 == 0);

            if (on)
            {
                g.setColour (Palette::accent);
                g.fillRoundedRectangle (cell, 4.0f);
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRoundedRectangle (cell.withHeight (cell.getHeight() * 0.45f), 4.0f);
            }
            else
            {
                g.setColour (beat ? Palette::header : Palette::panelAlt);
                g.fillRoundedRectangle (cell, 4.0f);
            }

            if (s == cur)
            {
                g.setColour (Palette::playhead);
                g.drawRoundedRectangle (cell, 4.0f, 1.6f);
            }
        }

        if (! editable)
        {
            g.setColour (Palette::bg.withAlpha (0.55f));
            g.fillAll();
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (15.0f));
            g.drawText ("Select an instrument clip to edit its steps",
                        getLocalBounds(), juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! editable)
            return;
        const int N = numSteps();
        const float cw = (float) getWidth() / (float) N;
        const int s = (int) (e.position.x / cw);
        if (! juce::isPositiveAndBelow (s, N))
            return;

        const auto before = notes.size();
        notes.erase (std::remove_if (notes.begin(), notes.end(),
                        [this, s] (const Note& n) { return stepOf (n.startBeat) == s; }),
                     notes.end());
        if (notes.size() == before)   // nothing removed → add one
            notes.push_back ({ defaultPitch, s * stepDur(), stepDur(), 0.85f });

        if (onNotesChanged) onNotesChanged();
        repaint();
    }

private:
    void timerCallback() override { if (transport.isPlaying()) repaint(); }

    int    numSteps() const { return juce::jlimit (1, 64, (int) std::round (contentLen * 4.0)); }
    double stepDur()  const { return contentLen / (double) numSteps(); }
    int    stepOf (double beat) const { return (int) std::llround (beat / stepDur()); }
    bool   isStepOn (int s) const
    {
        for (const auto& n : notes)
            if (stepOf (n.startBeat) == s)
                return true;
        return false;
    }

    Transport& transport;
    std::vector<Note> notes;
    double contentLen   { 4.0 };
    int    defaultPitch { 60 };
    bool   editable     { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepEditor)
};
