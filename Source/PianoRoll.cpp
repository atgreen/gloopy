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

    // Note transforms act on the region selection if there is one, else all notes.
    if (key.getTextCharacter() == 'q' || key.getTextCharacter() == 'Q')
    { transformSelectionOrAll ([&] (std::vector<Note>& v) { quantizeNotes (v, shift ? 0.5 : 0.25); }); changed = true; }
    else if (key.getTextCharacter() == 'h' || key.getTextCharacter() == 'H')
    { transformSelectionOrAll ([&] (std::vector<Note>& v) { juce::Random rng; humanizeNotes (v, 0.02, 0.1, rng); }); changed = true; }
    else if (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S')
    { transformSelectionOrAll ([&] (std::vector<Note>& v) { strumNotes (v, 0.05, ! shift); }); changed = true; }
    else if (key.getTextCharacter() == 'l' || key.getTextCharacter() == 'L')
    { legatoNotes (notes, shift ? 0.5f : 1.0f); changed = true; }   // full clip (needs all onsets); Shift = half
    else if (key == juce::KeyPress::upKey)
    { transformSelectionOrAll ([&] (std::vector<Note>& v) { transposeNotes (v, shift ? 12 : 1); }); changed = true; }
    else if (key == juce::KeyPress::downKey)
    { transformSelectionOrAll ([&] (std::vector<Note>& v) { transposeNotes (v, shift ? -12 : -1); }); changed = true; }
    else if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (! selection.empty())
        {
            std::vector<int> idxs (selection.begin(), selection.end());
            std::sort (idxs.rbegin(), idxs.rend());                       // erase high indices first
            for (int i : idxs) if (i >= 0 && i < (int) notes.size()) notes.erase (notes.begin() + i);
            selection.clear(); selectedNote = -1; changed = true;
        }
    }

    if (changed) { if (onNotesChanged) onNotesChanged(); repaint(); }
    return changed;
}

// Apply a NoteEdits transform to just the selected notes (if any), else to all.
void PianoRoll::transformSelectionOrAll (const std::function<void (std::vector<Note>&)>& fn)
{
    if (selection.empty()) { fn (notes); return; }
    std::vector<int> idxs (selection.begin(), selection.end());
    std::vector<Note> sub;
    for (int i : idxs) if (i >= 0 && i < (int) notes.size()) sub.push_back (notes[(size_t) i]);
    fn (sub);   // NoteEdits transforms preserve size and order, so write back by position
    for (size_t k = 0; k < idxs.size() && k < sub.size(); ++k)
        if (idxs[k] >= 0 && idxs[k] < (int) notes.size()) notes[(size_t) idxs[k]] = sub[k];
}

void PianoRoll::strumRollNotes (double stepBeats, bool down)
{
    if (! editable || notes.empty()) return;
    strumNotes (notes, stepBeats, down);
    if (onNotesChanged) onNotesChanged();
    repaint();
}

void PianoRoll::legatoRollNotes (float amount)
{
    if (! editable || notes.empty()) return;
    legatoNotes (notes, amount);            // stretch each note to the next onset (full clip)
    if (onNotesChanged) onNotesChanged();
    repaint();
}

void PianoRoll::splitRollNotesAt (double beat)
{
    if (! editable) return;
    const size_t before = notes.size();
    splitNotesAtBeat (notes, beat);
    if (notes.size() != before) { selection.clear(); if (onNotesChanged) onNotesChanged(); repaint(); }
}

void PianoRoll::arpeggiateRollNotes (double stepBeats, int mode)
{
    if (! editable || notes.empty()) return;
    arpeggiateNotes (notes, stepBeats, mode);   // count can change (up-down) → drop stale selection
    selection.clear();
    if (onNotesChanged) onNotesChanged();
    repaint();
}

PianoRoll::~PianoRoll()
{
    stopTimer();
}

void PianoRoll::setNotes (std::vector<Note> newNotes)
{
    notes = std::move (newNotes);
    selection.clear();
    centerViewOnNotes();
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
    return (double) gridBottom() / (double) juce::jmax (1, viewRows);
}

void PianoRoll::clampView()
{
    viewRows = juce::jlimit (8, pitchHigh - pitchLow + 1, viewRows);
    viewTop  = juce::jlimit (pitchLow + viewRows - 1, pitchHigh, viewTop);
}

void PianoRoll::centerViewOnNotes()
{
    if (notes.empty()) { clampView(); return; }
    int lo = 127, hi = 0;
    for (const auto& n : notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
    const int mid = (lo + hi) / 2;
    viewTop = mid + viewRows / 2;      // frame the notes' centre in the window
    clampView();
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
    return (float) ((viewTop - p) * rowHeight());
}

int PianoRoll::pitchForY (float y) const
{
    return viewTop - (int) std::floor (y / rowHeight());
}

double PianoRoll::snapBeat (double b) const
{
    return std::round (b / gridSnap) * gridSnap;
}

int PianoRoll::snapPitchToScaleRoll (int pitch) const
{
    if (! snapToScale || ! scaleActive) return pitch;    // off / chromatic → no change
    const int pc = ((pitch % 12) + 12) % 12;
    for (int d = 0; d <= 6; ++d)                          // search outward, +d before -d
    {
        if (scaleMask[(size_t) ((pc + d) % 12)])                    return juce::jlimit (pitchLow, pitchHigh, pitch + d);
        if (d > 0 && scaleMask[(size_t) (((pc - d) % 12 + 12) % 12)]) return juce::jlimit (pitchLow, pitchHigh, pitch - d);
    }
    return pitch;
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
        const float x0 = xForBeat (n.startBeat.toBeats());
        const float x1 = xForBeat ((n.startBeat + n.lengthBeats).toBeats());
        if (x >= x0 && x <= x1) return i;              // inside a note's span → exact
        const float d = juce::jmin (std::abs (x - x0), std::abs (x - x1));
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return (bestDist <= 12.0f) ? best : -1;            // only snap if reasonably close
}

float PianoRoll::velForY (float y) const
{
    const float top = gridBottom() + 4.0f;
    const float bot = (float) getHeight() - 4.0f;
    const float t = juce::jlimit (0.0f, 1.0f, (bot - y) / juce::jmax (1.0f, bot - top));
    return juce::jlimit (0.05f, 1.0f, t);
}

void PianoRoll::setVelFromY (int noteIdx, float y)
{
    if (! juce::isPositiveAndBelow (noteIdx, (int) notes.size())) return;
    notes[(size_t) noteIdx].velocity = velForY (y);
}

// Linear velocity ramp: every note whose start-x falls between a and b gets a velocity
// interpolated between velForY(a.y) and velForY(b.y) — a crescendo/decrescendo tool.
void PianoRoll::applyVelRamp (juce::Point<float> a, juce::Point<float> b)
{
    const float x0 = juce::jmin (a.x, b.x), x1 = juce::jmax (a.x, b.x);
    const float v0 = velForY (a.x <= b.x ? a.y : b.y);     // velocity at the left end
    const float v1 = velForY (a.x <= b.x ? b.y : a.y);     // velocity at the right end
    const float span = juce::jmax (1.0f, x1 - x0);
    for (auto& n : notes)
    {
        const float nx = xForBeat (n.startBeat.toBeats());
        if (nx < x0 - 1.0f || nx > x1 + 1.0f) continue;
        const float f = juce::jlimit (0.0f, 1.0f, (nx - x0) / span);
        n.velocity = juce::jlimit (0.05f, 1.0f, v0 + (v1 - v0) * f);
    }
}

int PianoRoll::noteIndexAt (juce::Point<float> p) const
{
    // Topmost first: iterate in reverse so later-drawn notes win.
    for (int i = (int) notes.size(); --i >= 0;)
    {
        const auto& n = notes[(size_t) i];
        juce::Rectangle<float> r (xForBeat (n.startBeat.toBeats()),
                                  yForPitch (n.pitch),
                                  xForBeat ((n.startBeat + n.lengthBeats).toBeats()) - xForBeat (n.startBeat.toBeats()),
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

    // Pitch rows in the note area (only the visible zoom window).
    for (int pitch = viewTop - viewRows + 1; pitch <= viewTop; ++pitch)
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

    // Beat / bar grid (note area), three brightness tiers so the metre reads at a glance:
    // faint 16th subdivisions, medium beats, bright bar boundaries.
    const int beats = (int) std::ceil (editLength);
    const float beatW = xForBeat (1.0) - xForBeat (0.0);
    if (beatW > 44.0f)                                   // 16th sub-lines only when there's room
    {
        g.setColour (Palette::lineSoft.withAlpha (0.5f));
        for (int beat = 0; beat < beats; ++beat)
            for (int sub = 1; sub < 4; ++sub)
                g.drawVerticalLine ((int) xForBeat (beat + sub * 0.25), 0.0f, h);
    }
    for (int beat = 0; beat <= beats; ++beat)
    {
        const bool bar = (beat % 4 == 0);
        g.setColour (bar ? Palette::line.brighter (0.35f) : Palette::line.withAlpha (0.6f));
        g.drawVerticalLine ((int) xForBeat ((double) beat), 0.0f, h);
    }

    // Ghost notes (other tracks' notes in this time range) — dim, behind the real notes.
    for (const auto& n : ghostNotes)
    {
        auto r = juce::Rectangle<float> (xForBeat (n.startBeat.toBeats()), yForPitch (n.pitch),
                                         xForBeat ((n.startBeat + n.lengthBeats).toBeats()) - xForBeat (n.startBeat.toBeats()), rh)
                     .reduced (0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.13f));
        g.fillRoundedRectangle (r, 2.0f);
    }

    // Notes.
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const auto& n = notes[(size_t) i];
        juce::Rectangle<float> r (xForBeat (n.startBeat.toBeats()),
                                  yForPitch (n.pitch),
                                  xForBeat ((n.startBeat + n.lengthBeats).toBeats()) - xForBeat (n.startBeat.toBeats()),
                                  rh);
        r = r.reduced (0.5f);

        const bool sel = selection.count (i) > 0;
        g.setColour (sel ? juce::Colour (0xfff2c14e)       // gold — clearly distinct from the cyan accent
                         : (i == selectedNote ? Palette::accent.brighter (0.25f) : Palette::accent));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.20f));
        g.fillRoundedRectangle (r.withTrimmedBottom (r.getHeight() * 0.55f), 2.0f);
        g.setColour (sel ? juce::Colours::white : Palette::inset);        // selected notes get a bright outline
        g.drawRoundedRectangle (r, 2.0f, sel ? 1.5f : 1.0f);
    }

    // Region marquee rectangle (while shift-dragging).
    if (marqueeing)
    {
        g.setColour (Palette::accent.withAlpha (0.15f));
        g.fillRect (marqueeRect);
        g.setColour (Palette::accent);
        g.drawRect (marqueeRect, 1.0f);
    }

    // Velocity strip along the bottom: one bar per note, drag to shape dynamics.
    if (hasVelStrip())
    {
        const float top = gridBottom();
        g.setColour (Palette::inset);
        g.fillRect (gx, top, w - gx, (float) velStripH);
        g.setColour (Palette::accent.withAlpha (0.05f));    // faint tint marks the velocity zone
        g.fillRect (gx, top, w - gx, (float) velStripH);
        g.setColour (Palette::line);
        g.drawHorizontalLine ((int) top, 0.0f, w);

        const float barTop = top + 4.0f;
        const float barBot = h - 4.0f;
        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const auto& n = notes[(size_t) i];
            const float x = xForBeat (n.startBeat.toBeats());
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
    for (int pitch = viewTop - viewRows + 1; pitch <= viewTop; ++pitch)
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
// Note auditioning — play pitches through the edited track's instrument.
// ---------------------------------------------------------------------------
void PianoRoll::startAudition (int pitch, float velocity)
{
    if (! auditionEnabled) return;
    if (auditionPitches.size() == 1 && auditionPitches[0] == pitch) return;   // unchanged
    stopAudition();
    if (onAuditionOn) onAuditionOn (pitch, velocity);
    auditionPitches.push_back (pitch);
}

void PianoRoll::auditionChord (const std::vector<int>& pitches, float velocity)
{
    if (! auditionEnabled) return;
    stopAudition();
    for (int pitch : pitches)
    {
        if (onAuditionOn) onAuditionOn (pitch, velocity);
        auditionPitches.push_back (pitch);
    }
}

void PianoRoll::stopAudition()
{
    for (int pitch : auditionPitches)
        if (onAuditionOff) onAuditionOff (pitch);
    auditionPitches.clear();
}

// ---------------------------------------------------------------------------
// Mouse editing
// ---------------------------------------------------------------------------
void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    if (! editable) return;
    grabKeyboardFocus();   // so Q/H/S/arrows/Delete reach keyPressed after interacting

    const auto p = e.position;

    // Key gutter: audition the pitch under the cursor, held while dragging (brush).
    if (p.x < (float) keyGutter)
    {
        gutterAuditioning = true;
        startAudition (pitchForY (p.y), 0.85f);
        return;
    }

    // Velocity strip along the bottom: drag a note's bar to set its velocity, or
    // shift-drag to draw a linear velocity ramp (crescendo/decrescendo) across notes.
    if (hasVelStrip() && p.y >= gridBottom())
    {
        if (e.mods.isShiftDown())
        {
            velRamp = true;
            velRampStart = p;
            drag = Drag::velocity;
            applyVelRamp (p, p);
            if (onNotesChanged) onNotesChanged();
            repaint();
            return;
        }
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

    // Knife mode: a plain click cuts every note crossing the clicked beat in two.
    if (knifeMode && ! e.mods.isPopupMenu())
    {
        splitRollNotesAt (snapBeat (beatForX (p.x)));
        drag = Drag::none;
        return;
    }

    const int hit = noteIndexAt (p);

    // Shift: extend/region-select rather than create or replace.
    if (e.mods.isShiftDown() && ! e.mods.isPopupMenu())
    {
        if (hit >= 0)                                   // shift-click a note toggles it
        {
            if (selection.count (hit)) selection.erase (hit); else selection.insert (hit);
            selectedNote = hit;
        }
        else                                            // shift-drag on empty space = marquee
        {
            marqueeing = true;
            marqueeStart = p;
            marqueeRect = juce::Rectangle<float> (p, p);
        }
        repaint();
        return;
    }

    // Delete on right-click / double-click.
    if (hit >= 0 && (e.mods.isPopupMenu() || e.getNumberOfClicks() >= 2))
    {
        notes.erase (notes.begin() + hit);
        selection.clear();
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
        const float rightX = xForBeat ((n.startBeat + n.lengthBeats).toBeats());

        if (std::abs (p.x - rightX) <= 5.0f)
        {
            selection.clear();                          // resize acts on one note
            drag = Drag::resize;
        }
        else
        {
            drag = Drag::move;
            dragBeatOffset  = beatForX (p.x) - n.startBeat.toBeats();
            dragPitchOffset = pitchForY (p.y) - n.pitch;
            if (! selection.count (hit)) { selection.clear(); selection.insert (hit); }  // clicked outside selection → select just this
            dragOrigins.clear();                        // snapshot the group for a rigid move
            for (int idx : selection)
                if (idx >= 0 && idx < (int) notes.size())
                    dragOrigins.emplace_back (idx, notes[(size_t) idx].startBeat.toBeats(), notes[(size_t) idx].pitch);
            startAudition (n.pitch, n.velocity);   // hear the note you grabbed
        }
    }
    else
    {
        selection.clear();                              // drawing a note drops any region selection
        const int    root  = juce::jlimit (pitchLow, pitchHigh, pitchForY (p.y));
        const double start = juce::jlimit (0.0, (double) transport.getLoopBeats() - gridSnap,
                                           snapBeat (beatForX (p.x)));
        if (chordType.isNotEmpty())
        {
            // Chord-stamp mode: lay down the whole voicing rooted at the clicked pitch.
            // Same makeChord() the AddChord API uses, so UI and scripts agree.
            auto chord = makeChord (root, chordType, 0, start, 1.0, 0.8f);
            std::vector<int> pitches;
            for (auto& cn : chord) { notes.push_back (cn); pitches.push_back (cn.pitch); }
            activeNote = selectedNote = (int) notes.size() - 1;  // top voice, for a length drag
            drag = Drag::resize; // dragging resizes the top voice; other voices keep 1 beat
            auditionChord (pitches, 0.8f);       // hear the whole voicing
        }
        else
        {
            // Create a single new note (snapped to the scale if that's on).
            Note n;
            n.pitch       = snapPitchToScaleRoll (root);
            n.startBeat   = start;
            n.lengthBeats = 1.0;
            n.velocity    = 0.8f;
            notes.push_back (n);
            activeNote = selectedNote = (int) notes.size() - 1;
            drag = Drag::resize; // let an immediate drag set the length
            startAudition (n.pitch, 0.8f);       // hear the note you drew
        }
    }

    if (onNotesChanged) onNotesChanged();
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    // Brushing the key gutter: re-trigger as the pitch under the cursor changes.
    if (gutterAuditioning)
    {
        startAudition (juce::jlimit (0, 127, pitchForY (e.position.y)), 0.85f);
        return;
    }

    // Region marquee: grow the selection rectangle.
    if (marqueeing)
    {
        marqueeRect = juce::Rectangle<float> (marqueeStart, e.position);
        repaint();
        return;
    }

    // Velocity ramp: redraw the crescendo/decrescendo from the drag start to here.
    if (velRamp)
    {
        applyVelRamp (velRampStart, e.position);
        if (onNotesChanged) onNotesChanged();
        repaint();
        return;
    }

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
        // Compute the active note's snapped delta, then move the whole selected group rigidly.
        double origActiveStart = n.startBeat.toBeats(); int origActivePitch = n.pitch;
        for (auto& o : dragOrigins) if (std::get<0> (o) == activeNote)
            { origActiveStart = std::get<1> (o); origActivePitch = std::get<2> (o); }

        const double newStart = juce::jlimit (0.0, (double) loopBeats - n.lengthBeats.toBeats(),
                                              snapBeat (beatForX (p.x) - dragBeatOffset));
        const int    newPitch = snapPitchToScaleRoll (juce::jlimit (pitchLow, pitchHigh,
                                                                    pitchForY (p.y) - dragPitchOffset));
        const double dBeat = newStart - origActiveStart;
        const int    dPitch = newPitch - origActivePitch;

        if (dragOrigins.empty())                        // single note (no snapshot)
        {
            n.startBeat = newStart; n.pitch = newPitch;
        }
        else for (auto& o : dragOrigins)                // rigid group move
        {
            const int idx = std::get<0> (o);
            if (idx < 0 || idx >= (int) notes.size()) continue;
            auto& m = notes[(size_t) idx];
            m.startBeat = juce::jlimit (0.0, (double) loopBeats - m.lengthBeats.toBeats(), std::get<1> (o) + dBeat);
            m.pitch     = juce::jlimit (pitchLow, pitchHigh, std::get<2> (o) + dPitch);
        }
        startAudition (n.pitch, n.velocity);   // re-trigger as the note is dragged in pitch
    }
    else if (drag == Drag::resize)
    {
        const double end = juce::jlimit (n.startBeat.toBeats() + gridSnap, (double) loopBeats,
                                         snapBeat (beatForX (p.x)));
        n.lengthBeats = end - n.startBeat.toBeats();
    }

    if (onNotesChanged) onNotesChanged();
    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    if (marqueeing)
    {
        // Select every note whose rectangle intersects the marquee.
        const float rh = (float) rowHeight();
        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const auto& n = notes[(size_t) i];
            juce::Rectangle<float> r (xForBeat (n.startBeat.toBeats()), yForPitch (n.pitch),
                                      xForBeat ((n.startBeat + n.lengthBeats).toBeats()) - xForBeat (n.startBeat.toBeats()), rh);
            if (marqueeRect.intersects (r)) selection.insert (i);
        }
        marqueeing = false;
        selectedNote = selection.empty() ? -1 : *selection.begin();
        repaint();
    }
    drag = Drag::none;
    activeNote = -1;
    dragOrigins.clear();
    velRamp = false;
    gutterAuditioning = false;
    stopAudition();
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! editable) return;
    const float dy = wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);
    if (std::abs (dy) < 1.0e-4f) return;

    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        // Vertical zoom, keeping the pitch under the cursor stable.
        const int anchorPitch = pitchForY (e.position.y);
        viewRows += (dy > 0 ? -2 : 2);                 // wheel up = zoom in (fewer rows)
        clampView();
        // re-anchor: put anchorPitch back under the cursor
        const int rowUnderCursor = (int) std::floor (e.position.y / juce::jmax (1.0, rowHeight()));
        viewTop = anchorPitch + rowUnderCursor;
        clampView();
    }
    else
    {
        viewTop += (dy > 0 ? 2 : -2);                  // wheel up = scroll toward higher pitches
        clampView();
    }
    repaint();
}

void PianoRoll::timerCallback()
{
    if (showPlayhead && transport.isPlaying())
        repaint();
}
