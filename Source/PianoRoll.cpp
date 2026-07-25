// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#include "PianoRoll.h"
#include "Palette.h"

PianoRoll::PianoRoll (Transport& transportToUse) : transport (transportToUse)
{
    setWantsKeyboardFocus (true);   // catch note-edit shortcuts (Q/H/arrows)
    startTimerHz (60); // playhead animation
}

void PianoRoll::setScale (int root, const std::vector<int>& intervals)
{
    scaleMask.fill (false);
    for (int i : intervals) scaleMask[(size_t) ((((root + i) % 12) + 12) % 12)] = true;
    int count = 0; for (bool b : scaleMask) if (b) ++count;
    scaleActive = (count > 0 && count < 12);   // a real (non-chromatic) scale
    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    if (! editable || notes.empty()) return false;
    const bool shift = key.getModifiers().isShiftDown();
    bool changed = false;

    if (key.getTextCharacter() == 'q' || key.getTextCharacter() == 'Q')
    { quantizeNotes (notes, shift ? 0.5 : 0.25); changed = true; }        // Q = 1/16, Shift+Q = 1/8
    else if (key.getTextCharacter() == 'h' || key.getTextCharacter() == 'H')
    { juce::Random rng; humanizeNotes (notes, 0.02, 0.1, rng); changed = true; }
    else if (key == juce::KeyPress::upKey)
    { transposeNotes (notes, shift ? 12 : 1); changed = true; }           // arrows transpose all
    else if (key == juce::KeyPress::downKey)
    { transposeNotes (notes, shift ? -12 : -1); changed = true; }

    if (changed) { if (onNotesChanged) onNotesChanged(); repaint(); }
    return changed;
}

PianoRoll::~PianoRoll()
{
    stopTimer();
}

void PianoRoll::setNotes (std::vector<Note> newNotes)
{
    notes = std::move (newNotes);
    if (onNotesChanged) onNotesChanged();
    repaint();
}

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------
bool PianoRoll::hasVelStrip() const
{
    // Only when there's room to keep the grid usable.
    return editable && getHeight() > velStripH + 120;
}

float PianoRoll::gridBottom() const
{
    return (float) getHeight() - (hasVelStrip() ? (float) velStripH : 0.0f);
}

double PianoRoll::rowHeight() const
{
    return (double) gridBottom() / (double) (pitchHigh - pitchLow + 1);
}

float PianoRoll::noteAreaWidth() const
{
    return (float) juce::jmax (1, getWidth() - keyGutter);
}

float PianoRoll::xForBeat (double b) const
{
    return (float) keyGutter + (float) (b / editLength) * noteAreaWidth();
}

double PianoRoll::beatForX (float x) const
{
    return (double) ((x - keyGutter) / noteAreaWidth()) * editLength;
}

float PianoRoll::yForPitch (int p) const
{
    return (float) ((pitchHigh - p) * rowHeight());
}

int PianoRoll::pitchForY (float y) const
{
    return pitchHigh - (int) std::floor (y / rowHeight());
}

double PianoRoll::snapBeat (double b) const
{
    return std::round (b / gridSnap) * gridSnap;
}

bool PianoRoll::isBlackKey (int pitch)
{
    switch (pitch % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default:                                 return false;
    }
}

int PianoRoll::velNoteAt (float x) const
{
    // Prefer a note whose horizontal span contains x; else the nearest by start.
    int best = -1;
    float bestDist = 1.0e9f;
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const auto& n = notes[(size_t) i];
        const float x0 = xForBeat (n.startBeat);
        const float x1 = xForBeat (n.startBeat + n.lengthBeats);
        if (x >= x0 && x <= x1) return i;              // inside a note's span → exact
        const float d = juce::jmin (std::abs (x - x0), std::abs (x - x1));
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return (bestDist <= 12.0f) ? best : -1;            // only snap if reasonably close
}

void PianoRoll::setVelFromY (int noteIdx, float y)
{
    if (! juce::isPositiveAndBelow (noteIdx, (int) notes.size())) return;
    const float top = gridBottom() + 4.0f;
    const float bot = (float) getHeight() - 4.0f;
    const float t = juce::jlimit (0.0f, 1.0f, (bot - y) / juce::jmax (1.0f, bot - top));
    notes[(size_t) noteIdx].velocity = juce::jlimit (0.05f, 1.0f, t);
}

int PianoRoll::noteIndexAt (juce::Point<float> p) const
{
    // Topmost first: iterate in reverse so later-drawn notes win.
    for (int i = (int) notes.size(); --i >= 0;)
    {
        const auto& n = notes[(size_t) i];
        juce::Rectangle<float> r (xForBeat (n.startBeat),
                                  yForPitch (n.pitch),
                                  xForBeat (n.startBeat + n.lengthBeats) - xForBeat (n.startBeat),
                                  (float) rowHeight());
        if (r.contains (p))
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
void PianoRoll::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth();
    const auto h = (float) getHeight();
    const auto rh = (float) rowHeight();

    g.fillAll (Palette::inset);
    const float gx = (float) keyGutter;

    // Pitch rows in the note area (shade black-key rows a touch darker).
    for (int pitch = pitchLow; pitch <= pitchHigh; ++pitch)
    {
        const float y = yForPitch (pitch);
        g.setColour (isBlackKey (pitch) ? Palette::inset : Palette::panel);
        g.fillRect (gx, y, w - gx, rh);

        if (scaleActive && scaleMask[(size_t) (((pitch % 12) + 12) % 12)])   // tint in-scale rows
        {
            g.setColour (Palette::accent.withAlpha (0.22f));
            g.fillRect (gx, y, w - gx, rh);
        }

        if (pitch % 12 == 0)   // C rows get a divider line
        {
            g.setColour (Palette::line);
            g.drawHorizontalLine ((int) y, gx, w);
        }
    }

    // Beat / bar grid (note area only).
    const int beats = (int) std::ceil (editLength);
    for (int beat = 0; beat <= beats; ++beat)
    {
        const float x = xForBeat ((double) beat);
        const bool bar = (beat % 4 == 0);
        g.setColour (bar ? Palette::line : Palette::lineSoft);
        g.drawVerticalLine ((int) x, 0.0f, h);
    }

    // Ghost notes (other tracks' notes in this time range) — dim, behind the real notes.
    for (const auto& n : ghostNotes)
    {
        auto r = juce::Rectangle<float> (xForBeat (n.startBeat), yForPitch (n.pitch),
                                         xForBeat (n.startBeat + n.lengthBeats) - xForBeat (n.startBeat), rh)
                     .reduced (0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.13f));
        g.fillRoundedRectangle (r, 2.0f);
    }

    // Notes.
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const auto& n = notes[(size_t) i];
        juce::Rectangle<float> r (xForBeat (n.startBeat),
                                  yForPitch (n.pitch),
                                  xForBeat (n.startBeat + n.lengthBeats) - xForBeat (n.startBeat),
                                  rh);
        r = r.reduced (0.5f);

        g.setColour (i == selectedNote ? Palette::accent.brighter (0.25f) : Palette::accent);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.20f));
        g.fillRoundedRectangle (r.withTrimmedBottom (r.getHeight() * 0.55f), 2.0f);
        g.setColour (Palette::inset);
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
    }

    // Velocity strip along the bottom: one bar per note, drag to shape dynamics.
    if (hasVelStrip())
    {
        const float top = gridBottom();
        g.setColour (Palette::inset);
        g.fillRect (gx, top, w - gx, (float) velStripH);
        g.setColour (Palette::line);
        g.drawHorizontalLine ((int) top, 0.0f, w);

        const float barTop = top + 4.0f;
        const float barBot = h - 4.0f;
        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const auto& n = notes[(size_t) i];
            const float x = xForBeat (n.startBeat);
            const float bh = (barBot - barTop) * juce::jlimit (0.0f, 1.0f, n.velocity);
            juce::Rectangle<float> bar (x + 1.0f, barBot - bh, 4.0f, bh);
            g.setColour (i == selectedNote ? Palette::accent.brighter (0.25f)
                                           : Palette::accent.withAlpha (0.75f));
            g.fillRoundedRectangle (bar, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.20f));
            g.fillEllipse (x + 0.5f, barBot - bh - 2.5f, 5.0f, 5.0f);   // handle cap
        }

        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (9.0f));
        g.drawText ("VEL", (int) gx + 3, (int) top + 2, 30, 12,
                    juce::Justification::topLeft, false);
    }

    // Playhead (optional — the arrange view owns the main one).
    if (showPlayhead)
    {
        const float px = xForBeat (std::fmod (transport.getPlayheadBeats(), editLength));
        g.setColour (Palette::playhead);
        g.drawVerticalLine ((int) px, gx, gridBottom());
    }

    // Piano-key gutter on the left.
    g.setColour (Palette::panel);
    g.fillRect (0.0f, 0.0f, gx, h);
    for (int pitch = pitchLow; pitch <= pitchHigh; ++pitch)
    {
        const float y = yForPitch (pitch);
        const bool black = isBlackKey (pitch);
        g.setColour (black ? juce::Colour (0xff17181b) : juce::Colour (0xffd8dae0));
        g.fillRect (0.0f, y, black ? gx * 0.62f : gx, rh);       // black keys shorter
        g.setColour (Palette::bg);
        g.drawHorizontalLine ((int) (y + rh), 0.0f, gx);         // key separator
        if (pitch % 12 == 0)                                     // label each C
        {
            g.setColour (juce::Colour (0xff55585f));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText ("C" + juce::String (pitch / 12 - 1),
                        2, (int) y, (int) gx - 3, (int) rh,
                        juce::Justification::centredRight, false);
        }
    }
    g.setColour (Palette::line);
    g.drawVerticalLine ((int) gx, 0.0f, h);

    // Placeholder hint when no channel is selected for editing.
    if (! editable)
    {
        g.setColour (Palette::bg.withAlpha (0.55f));
        g.fillAll();
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Click a channel's  PR  button to edit its notes here",
                    getLocalBounds(), juce::Justification::centred, false);
    }
}

// ---------------------------------------------------------------------------
// Mouse editing
// ---------------------------------------------------------------------------
void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    if (! editable || e.position.x < (float) keyGutter)   // ignore the key gutter
        return;

    const auto p = e.position;

    // Velocity strip along the bottom: drag a note's bar to set its velocity.
    if (hasVelStrip() && p.y >= gridBottom())
    {
        const int vn = velNoteAt (p.x);
        if (vn >= 0)
        {
            activeNote = selectedNote = vn;
            drag = Drag::velocity;
            setVelFromY (vn, p.y);
            if (onNotesChanged) onNotesChanged();
            repaint();
        }
        return;
    }

    const int hit = noteIndexAt (p);

    // Delete on right-click / double-click.
    if (hit >= 0 && (e.mods.isPopupMenu() || e.getNumberOfClicks() >= 2))
    {
        notes.erase (notes.begin() + hit);
        selectedNote = -1;
        activeNote = -1;
        drag = Drag::none;
        if (onNotesChanged) onNotesChanged();
        repaint();
        return;
    }

    if (hit >= 0)
    {
        activeNote = selectedNote = hit;
        const auto& n = notes[(size_t) hit];
        const float rightX = xForBeat (n.startBeat + n.lengthBeats);

        if (std::abs (p.x - rightX) <= 5.0f)
        {
            drag = Drag::resize;
        }
        else
        {
            drag = Drag::move;
            dragBeatOffset  = beatForX (p.x) - n.startBeat;
            dragPitchOffset = pitchForY (p.y) - n.pitch;
        }
    }
    else
    {
        const int    root  = juce::jlimit (pitchLow, pitchHigh, pitchForY (p.y));
        const double start = juce::jlimit (0.0, (double) transport.getLoopBeats() - gridSnap,
                                           snapBeat (beatForX (p.x)));
        if (chordType.isNotEmpty())
        {
            // Chord-stamp mode: lay down the whole voicing rooted at the clicked pitch.
            // Same makeChord() the AddChord API uses, so UI and scripts agree.
            auto chord = makeChord (root, chordType, 0, start, 1.0, 0.8f);
            for (auto& cn : chord) notes.push_back (cn);
            activeNote = selectedNote = (int) notes.size() - 1;  // top voice, for a length drag
            drag = Drag::resize; // dragging resizes the top voice; other voices keep 1 beat
        }
        else
        {
            // Create a single new note.
            Note n;
            n.pitch       = root;
            n.startBeat   = start;
            n.lengthBeats = 1.0;
            n.velocity    = 0.8f;
            notes.push_back (n);
            activeNote = selectedNote = (int) notes.size() - 1;
            drag = Drag::resize; // let an immediate drag set the length
        }
    }

    if (onNotesChanged) onNotesChanged();
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    if (! editable || activeNote < 0 || activeNote >= (int) notes.size())
        return;

    auto& n = notes[(size_t) activeNote];
    const auto p = e.position;
    const int loopBeats = transport.getLoopBeats();

    if (drag == Drag::velocity)
    {
        // Scrub horizontally to shape neighbouring notes, like a velocity pencil.
        const int vn = velNoteAt (p.x);
        const int target = vn >= 0 ? vn : activeNote;
        setVelFromY (target, p.y);
        selectedNote = target;
        if (onNotesChanged) onNotesChanged();
        repaint();
        return;
    }

    if (drag == Drag::move)
    {
        n.startBeat = juce::jlimit (0.0, (double) loopBeats - n.lengthBeats,
                                    snapBeat (beatForX (p.x) - dragBeatOffset));
        n.pitch     = juce::jlimit (pitchLow, pitchHigh,
                                    pitchForY (p.y) - dragPitchOffset);
    }
    else if (drag == Drag::resize)
    {
        const double end = juce::jlimit (n.startBeat + gridSnap, (double) loopBeats,
                                         snapBeat (beatForX (p.x)));
        n.lengthBeats = end - n.startBeat;
    }

    if (onNotesChanged) onNotesChanged();
    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::none;
    activeNote = -1;
}

void PianoRoll::timerCallback()
{
    if (showPlayhead && transport.isPlaying())
        repaint();
}
