#include "PlaylistView.h"
#include <cmath>

PlaylistView::PlaylistView (std::vector<PlaylistClip>& clipsRef,
                            std::vector<std::unique_ptr<Pattern>>& patternsRef,
                            Transport& transportRef,
                            juce::CriticalSection& engineLockRef,
                            std::function<int()> selectedPatternProvider)
    : clips (clipsRef), patterns (patternsRef), transport (transportRef),
      engineLock (engineLockRef), selectedPattern (std::move (selectedPatternProvider))
{
    startTimerHz (30);
}

PlaylistView::~PlaylistView()
{
    stopTimer();
}

// ---------------------------------------------------------------------------
// Coordinates
// ---------------------------------------------------------------------------
int PlaylistView::numBars() const
{
    double maxEnd = 0.0;
    for (const auto& c : clips)
        maxEnd = juce::jmax (maxEnd, c.startBeat + c.lengthBeats);
    const int songBars = (int) std::ceil (maxEnd / beatsPerBar);
    return juce::jmax (8, songBars + 1);
}

float  PlaylistView::barWidth() const { return (float) (getWidth() - gutter) / (float) numBars(); }
float  PlaylistView::xForBeat (double beat) const { return gutter + (float) (beat / beatsPerBar) * barWidth(); }
double PlaylistView::beatForX (float x) const { return (double) ((x - gutter) / barWidth()) * beatsPerBar; }
float  PlaylistView::yForTrack (int t) const { return (float) (rulerHeight + t * trackHeight); }
int    PlaylistView::trackForY (float y) const { return (int) ((y - rulerHeight) / trackHeight); }
double PlaylistView::snapToBar (double beat) const { return std::round (beat / beatsPerBar) * beatsPerBar; }

int PlaylistView::clipIndexAt (juce::Point<float> p) const
{
    for (int i = (int) clips.size(); --i >= 0;)
    {
        const auto& c = clips[(size_t) i];
        juce::Rectangle<float> r (xForBeat (c.startBeat), yForTrack (c.track),
                                  xForBeat (c.startBeat + c.lengthBeats) - xForBeat (c.startBeat),
                                  (float) trackHeight);
        if (r.contains (p))
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
void PlaylistView::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth();
    g.fillAll (juce::Colour (0xff1a1a1e));

    const int bars = numBars();
    const float bw = barWidth();

    // Ruler + bar lines.
    g.setColour (juce::Colour (0xff242429));
    g.fillRect (0.0f, 0.0f, w, (float) rulerHeight);
    for (int b = 0; b <= bars; ++b)
    {
        const float x = gutter + b * bw;
        g.setColour (juce::Colour (0xff33333c));
        g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
        if (b < bars)
        {
            g.setColour (juce::Colour (0xff70707c));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::String (b + 1), (int) x + 3, 2, 30, rulerHeight - 4,
                        juce::Justification::centredLeft, false);
        }
    }

    // Track lanes + gutter labels.
    for (int t = 0; t < kNumTracks; ++t)
    {
        const float y = yForTrack (t);
        g.setColour ((t % 2 == 0) ? juce::Colour (0xff1d1d22) : juce::Colour (0xff202026));
        g.fillRect (0.0f, y, w, (float) trackHeight);
        g.setColour (juce::Colour (0xff55555f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (t + 1), 6, (int) y, gutter - 10, trackHeight,
                    juce::Justification::centred, false);
        g.setColour (juce::Colour (0xff141417));
        g.drawHorizontalLine ((int) (y + trackHeight), 0.0f, w);
    }

    // Clips.
    for (int i = 0; i < (int) clips.size(); ++i)
    {
        const auto& c = clips[(size_t) i];
        if (! juce::isPositiveAndBelow (c.patternIndex, (int) patterns.size()))
            continue;
        const Pattern& p = *patterns[(size_t) c.patternIndex];

        juce::Rectangle<float> r (xForBeat (c.startBeat), yForTrack (c.track) + 2.0f,
                                  xForBeat (c.startBeat + c.lengthBeats) - xForBeat (c.startBeat),
                                  (float) trackHeight - 4.0f);
        g.setColour (p.colour);
        g.fillRoundedRectangle (r, 3.0f);

        // Repetition dividers (one per pattern length).
        const double repBeats = juce::jmax (1, p.getLengthBeats());
        for (double b = c.startBeat + repBeats; b < c.startBeat + c.lengthBeats - 0.001; b += repBeats)
        {
            const float x = xForBeat (b);
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        }

        g.setColour ((i == selectedClip) ? juce::Colours::white
                                         : juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (r, 3.0f, (i == selectedClip) ? 2.0f : 1.0f);

        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (p.name, r.reduced (5.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    // Song playhead.
    if (transport.isSongMode())
    {
        const float px = xForBeat (transport.getPlayheadBeats());
        g.setColour (juce::Colour (0xffff5d5d));
        g.drawVerticalLine ((int) px, 0.0f, (float) getHeight());
    }
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------
void PlaylistView::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    const int hit = clipIndexAt (p);

    if (hit >= 0 && (e.mods.isPopupMenu() || e.getNumberOfClicks() >= 2))
    {
        {
            const juce::ScopedLock sl (engineLock);
            clips.erase (clips.begin() + hit);
        }
        selectedClip = activeClip = -1;
        drag = Drag::none;
        if (onChanged) onChanged();
        repaint();
        return;
    }

    if (hit >= 0)
    {
        activeClip = selectedClip = hit;
        const auto& c = clips[(size_t) hit];
        const float rightX = xForBeat (c.startBeat + c.lengthBeats);
        if (std::abs (p.x - rightX) <= 6.0f)
        {
            drag = Drag::resize;
        }
        else
        {
            drag = Drag::move;
            dragBeatOffset = beatForX (p.x) - c.startBeat;
        }
    }
    else
    {
        const int track = trackForY (p.y);
        const int pat   = selectedPattern ? selectedPattern() : -1;
        if (! juce::isPositiveAndBelow (track, kNumTracks)
              || ! juce::isPositiveAndBelow (pat, (int) patterns.size()))
            return;

        PlaylistClip c;
        c.patternIndex = pat;
        c.track        = track;
        c.startBeat    = juce::jmax (0.0, snapToBar (beatForX (p.x)));
        c.lengthBeats  = juce::jmax (1, patterns[(size_t) pat]->getLengthBeats());
        {
            const juce::ScopedLock sl (engineLock);
            clips.push_back (c);
        }
        activeClip = selectedClip = (int) clips.size() - 1;
        drag = Drag::resize;
    }

    if (onChanged) onChanged();
    repaint();
}

void PlaylistView::mouseDrag (const juce::MouseEvent& e)
{
    if (activeClip < 0 || activeClip >= (int) clips.size())
        return;

    const auto p = e.position;
    {
        const juce::ScopedLock sl (engineLock);
        auto& c = clips[(size_t) activeClip];
        const double barLen = juce::jmax (1, patterns[(size_t) c.patternIndex]->getLengthBeats());

        if (drag == Drag::move)
        {
            c.startBeat = juce::jmax (0.0, snapToBar (beatForX (p.x) - dragBeatOffset));
            c.track     = juce::jlimit (0, kNumTracks - 1, trackForY (p.y));
        }
        else if (drag == Drag::resize)
        {
            const double end = snapToBar (beatForX (p.x));
            c.lengthBeats = juce::jmax (barLen, end - c.startBeat);
        }
    }

    if (onChanged) onChanged();
    repaint();
}

void PlaylistView::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::none;
    activeClip = -1;
}

void PlaylistView::timerCallback()
{
    if (transport.isPlaying() && transport.isSongMode())
        repaint();
}
