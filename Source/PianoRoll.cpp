#include "PianoRoll.h"
#include "Palette.h"

PianoRoll::PianoRoll (Transport& transportToUse) : transport (transportToUse)
{
    setWantsKeyboardFocus (false);
    startTimerHz (60); // playhead animation
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
double PianoRoll::rowHeight() const
{
    return (double) getHeight() / (double) (pitchHigh - pitchLow + 1);
}

float PianoRoll::xForBeat (double b) const
{
    return (float) (b / editLength * getWidth());
}

double PianoRoll::beatForX (float x) const
{
    return (double) x / (double) getWidth() * editLength;
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

    // Pitch rows (shade black-key rows a touch darker).
    for (int pitch = pitchLow; pitch <= pitchHigh; ++pitch)
    {
        const float y = yForPitch (pitch);
        g.setColour (isBlackKey (pitch) ? Palette::inset : Palette::panel);
        g.fillRect (0.0f, y, w, rh);

        // C rows get a subtle label + line.
        if (pitch % 12 == 0)
        {
            g.setColour (Palette::line);
            g.drawHorizontalLine ((int) y, 0.0f, w);
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("C" + juce::String (pitch / 12 - 1),
                        3, (int) y, 24, (int) rh,
                        juce::Justification::centredLeft, false);
        }
    }

    // Beat / bar grid.
    const int beats = (int) std::ceil (editLength);
    for (int beat = 0; beat <= beats; ++beat)
    {
        const float x = xForBeat ((double) beat);
        const bool bar = (beat % 4 == 0);
        g.setColour (bar ? Palette::line : Palette::lineSoft);
        g.drawVerticalLine ((int) x, 0.0f, h);
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

    // Playhead (optional — the arrange view owns the main one).
    if (showPlayhead)
    {
        const float px = xForBeat (std::fmod (transport.getPlayheadBeats(), editLength));
        g.setColour (Palette::playhead);
        g.drawVerticalLine ((int) px, 0.0f, h);
    }

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
    if (! editable)
        return;

    const auto p = e.position;
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
        // Create a new note.
        Note n;
        n.pitch       = juce::jlimit (pitchLow, pitchHigh, pitchForY (p.y));
        n.startBeat   = juce::jlimit (0.0, (double) transport.getLoopBeats() - gridSnap,
                                      snapBeat (beatForX (p.x)));
        n.lengthBeats = 1.0;
        n.velocity    = 0.8f;
        notes.push_back (n);
        activeNote = selectedNote = (int) notes.size() - 1;
        drag = Drag::resize; // let an immediate drag set the length
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
