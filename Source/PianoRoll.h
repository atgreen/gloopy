#pragma once

#include <JuceHeader.h>
#include <vector>
#include <functional>
#include "Note.h"
#include "Transport.h"

/** An editable piano-roll grid.

    Interaction:
      - click empty space        → create a 1-beat note (snapped to the grid)
      - drag a note's body        → move it in time and pitch
      - drag a note's right edge  → resize its length
      - right-click / double-click a note → delete it

    The PianoRoll holds the authoritative note list; after every edit it calls
    @c onNotesChanged so the owner can push a copy into the (audio-thread) Sequencer. */
class PianoRoll : public juce::Component,
                  private juce::Timer
{
public:
    explicit PianoRoll (Transport& transportToUse);
    ~PianoRoll() override;

    void setNotes (std::vector<Note> newNotes);

    /** Load notes for display/editing WITHOUT firing onNotesChanged (used when
        switching which channel/pattern the roll is showing). */
    void loadNotes (std::vector<Note> newNotes) { notes = std::move (newNotes); repaint(); }

    const std::vector<Note>& getNotes() const noexcept { return notes; }

    void setEnabledEditing (bool shouldEdit) { editable = shouldEdit; repaint(); }

    std::function<void()> onNotesChanged;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    // ---- coordinate helpers -------------------------------------------
    double rowHeight()          const;
    float  xForBeat (double b)  const;
    double beatForX (float x)   const;
    float  yForPitch (int p)    const;
    int    pitchForY (float y)  const;
    double snapBeat (double b)  const;
    int    noteIndexAt (juce::Point<float> p) const;

    static bool isBlackKey (int pitch);

    Transport& transport;
    std::vector<Note> notes;
    bool editable { true };

    // pitch range shown (inclusive)
    static constexpr int   pitchLow  = 36;   // C2
    static constexpr int   pitchHigh = 84;   // C6
    static constexpr double gridSnap = 0.25; // sixteenth-note grid

    enum class Drag { none, create, move, resize };
    Drag  drag        { Drag::none };
    int   activeNote  { -1 };
    int   selectedNote { -1 };
    double dragBeatOffset  { 0.0 };
    int    dragPitchOffset { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};
