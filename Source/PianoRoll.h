// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>
#include <functional>
#include "Note.h"
#include "NoteEdits.h"
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

    /** Highlight the rows of the project scale (root 0..11 + semitone intervals).
        A chromatic (all-12) scale disables the highlight. */
    void setScale (int root, const std::vector<int>& intervals);

    /** Reference notes from other tracks in this time range, drawn dim (read-only). */
    void setGhostNotes (std::vector<Note> g) { ghostNotes = std::move (g); repaint(); }

    /** Chord-stamp mode: when set to a chord type ("maj", "min7", ...), clicking an
        empty grid cell stamps that whole chord rooted at the clicked pitch instead of
        a single note. Empty string / "off" restores single-note drawing. Uses the same
        makeChord() transform as the AddChord API, so the UI and scripts agree. */
    void setChordType (const juce::String& t) { chordType = (t == "off" ? juce::String() : t); }
    juce::String getChordType() const { return chordType; }

    /** The visible/editable length in beats (a clip's content length). */
    void setLength (double beats) { editLength = juce::jmax (0.25, beats); repaint(); }
    void setShowPlayhead (bool s) { showPlayhead = s; }

    std::function<void()> onNotesChanged;

    /** Note auditioning: fired so the owner can play the pitch through the edited
        track's instrument (via its live-MIDI collector) as you click / drag / brush.
        onAuditionOn(pitch, velocity) = note-on (held); onAuditionOff(pitch) = note-off. */
    std::function<void (int, float)> onAuditionOn;
    std::function<void (int)>        onAuditionOff;
    void setAuditionEnabled (bool b) { auditionEnabled = b; if (! b) stopAudition(); }
    bool isAuditionEnabled() const { return auditionEnabled; }

    /** Fan out chord voices (notes sharing a start beat). Uses the same strumNotes
        transform as apiStrumClip, then fires onNotesChanged. */
    void strumRollNotes (double stepBeats, bool down);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;   // Q quantize, H humanize, arrows transpose

private:
    void timerCallback() override;

    // ---- coordinate helpers -------------------------------------------
    double rowHeight()          const;
    float  gridBottom()         const;   // y where the note grid ends (above vel strip)
    bool   hasVelStrip()        const;
    float  xForBeat (double b)  const;
    double beatForX (float x)   const;
    float  yForPitch (int p)    const;
    int    pitchForY (float y)  const;
    double snapBeat (double b)  const;
    int    noteIndexAt (juce::Point<float> p) const;
    void   startAudition (int pitch, float velocity);            // replace-held (scrub/move)
    void   auditionChord (const std::vector<int>& pitches, float velocity);
    void   stopAudition();
    int    velNoteAt (float x)  const;   // note whose span/start is nearest x
    void   setVelFromY (int noteIdx, float y);

    static bool isBlackKey (int pitch);

    Transport& transport;
    std::vector<Note> notes;
    std::vector<Note> ghostNotes;         // other tracks' notes (dim, read-only)
    juce::String chordType;               // chord-stamp mode ("" = single notes)
    bool auditionEnabled { true };        // play notes through the instrument on click/brush
    bool gutterAuditioning { false };     // a held audition started on the key gutter
    std::vector<int> auditionPitches;     // currently-sounding audition notes
    std::array<bool, 12> scaleMask { };   // pitch classes in the project scale
    bool  scaleActive { false };          // a non-chromatic scale is highlighted
    bool editable { true };
    double editLength { 4.0 };     // visible length in beats (clip content length)
    bool   showPlayhead { false };

    // pitch range shown (inclusive)
    static constexpr int   pitchLow  = 36;   // C2
    static constexpr int   pitchHigh = 84;   // C6
    static constexpr double gridSnap = 0.25; // sixteenth-note grid
    static constexpr int   keyGutter = 34;   // left piano-key strip (px)
    static constexpr int   velStripH = 46;   // bottom velocity-editing strip (px)

    float noteAreaWidth() const;

    enum class Drag { none, create, move, resize, velocity };
    Drag  drag        { Drag::none };
    int   activeNote  { -1 };
    int   selectedNote { -1 };
    double dragBeatOffset  { 0.0 };
    int    dragPitchOffset { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};
