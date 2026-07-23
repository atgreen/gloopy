#include "ArrangeView.h"
#include "Palette.h"
#include <cmath>

ArrangeView::ArrangeView (std::vector<std::unique_ptr<Track>>& tracksRef,
                          Transport& transportRef, juce::CriticalSection& engineLockRef)
    : tracks (tracksRef), transport (transportRef), engineLock (engineLockRef)
{
    startTimerHz (30);
}

ArrangeView::~ArrangeView() { stopTimer(); }

int ArrangeView::preferredHeight() const
{
    return rulerHeight + juce::jmax (1, (int) tracks.size()) * trackHeight;
}

void ArrangeView::rebuild()
{
    muteButtons.clear();
    soloButtons.clear();
    volSliders.clear();
    removeAllChildren();

    for (auto& tPtr : tracks)
    {
        Track* t = tPtr.get();

        auto solo = std::make_unique<juce::TextButton> ("S");
        solo->setClickingTogglesState (true);
        solo->setToggleState (t->solo.load(), juce::dontSendNotification);
        solo->setColour (juce::TextButton::buttonOnColourId, Palette::solo);
        solo->setColour (juce::TextButton::textColourOnId, Palette::bg);
        solo->onClick = [t, s = solo.get()] { t->solo.store (s->getToggleState()); };
        addAndMakeVisible (*solo);
        soloButtons.push_back (std::move (solo));

        auto mute = std::make_unique<juce::TextButton> ("M");
        mute->setClickingTogglesState (true);
        mute->setToggleState (t->mute.load(), juce::dontSendNotification);
        mute->setColour (juce::TextButton::buttonOnColourId, Palette::mute);
        mute->onClick = [t, m = mute.get()] { t->mute.store (m->getToggleState()); };
        addAndMakeVisible (*mute);
        muteButtons.push_back (std::move (mute));

        auto vol = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                   juce::Slider::NoTextBox);
        vol->setRange (0.0, 1.0, 0.01);
        vol->setValue (t->volume.load(), juce::dontSendNotification);
        vol->onValueChange = [t, s = vol.get()] { t->volume.store ((float) s->getValue()); };
        addAndMakeVisible (*vol);
        volSliders.push_back (std::move (vol));
    }

    setSize (getWidth(), preferredHeight());
    resized();
    repaint();
}

void ArrangeView::resized()
{
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        const int y = rulerHeight + i * trackHeight;
        soloButtons[(size_t) i]->setBounds (headerWidth - 62, y + 6, 26, 20);
        muteButtons[(size_t) i]->setBounds (headerWidth - 32, y + 6, 26, 20);
        volSliders [(size_t) i]->setBounds (12, y + trackHeight - 18, headerWidth - 24, 12);
    }
}

// ---------------------------------------------------------------------------
// Coordinates
// ---------------------------------------------------------------------------
int ArrangeView::numBars() const
{
    double maxEnd = 0.0;
    for (auto& t : tracks)
        for (auto& c : t->clips)
            maxEnd = juce::jmax (maxEnd, c.endBeat());
    return juce::jmax (8, (int) std::ceil (maxEnd / beatsPerBar) + 1);
}

float  ArrangeView::barWidth() const { return (float) (getWidth() - headerWidth) / (float) numBars(); }
float  ArrangeView::xForBeat (double beat) const { return headerWidth + (float) (beat / beatsPerBar) * barWidth(); }
double ArrangeView::beatForX (float x) const { return (double) ((x - headerWidth) / barWidth()) * beatsPerBar; }
int    ArrangeView::trackAtY (float y) const
{
    const int t = (int) ((y - rulerHeight) / trackHeight);
    return juce::isPositiveAndBelow (t, (int) tracks.size()) ? t : -1;
}
double ArrangeView::snapToBar (double beat) const { return std::round (beat / beatsPerBar) * beatsPerBar; }

int ArrangeView::clipAt (int track, juce::Point<float> p) const
{
    if (! juce::isPositiveAndBelow (track, (int) tracks.size()))
        return -1;
    const auto& clips = tracks[(size_t) track]->clips;
    const float y = rulerHeight + track * trackHeight;
    for (int i = (int) clips.size(); --i >= 0;)
    {
        juce::Rectangle<float> r (xForBeat (clips[(size_t) i].startBeat), y + 2.0f,
                                  xForBeat (clips[(size_t) i].endBeat()) - xForBeat (clips[(size_t) i].startBeat),
                                  (float) trackHeight - 4.0f);
        if (r.contains (p))
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
void ArrangeView::drawClip (juce::Graphics& g, const Track& t, const Clip& c,
                            juce::Rectangle<float> r, bool selected) const
{
    g.setColour (t.colour);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.fillRoundedRectangle (r.withHeight (14.0f), 3.0f);

    // Audio clip: waveform overview.
    if (c.isAudio() && c.peaks && ! c.peaks->empty())
    {
        auto wf = r.withTrimmedTop (15.0f).reduced (2.0f, 2.0f);
        const float mid = wf.getCentreY();
        const auto& pk = *c.peaks;
        const int N = (int) pk.size();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        const int x0 = (int) r.getX() + 2, x1 = (int) r.getRight() - 2;
        for (int x = x0; x < x1; ++x)
        {
            const float frac = (x - r.getX()) / juce::jmax (1.0f, r.getWidth());
            const int idx = juce::jlimit (0, N - 1, (int) (frac * N));
            const float a = pk[(size_t) idx] * wf.getHeight() * 0.5f;
            g.drawVerticalLine (x, mid - a, mid + a);
        }
    }
    // MIDI clip: mini note preview (tiled per content repetition).
    else if (! c.notes.empty() && c.contentLenBeats > 0.0)
    {
        auto notesArea = r.withTrimmedTop (15.0f).reduced (2.0f, 2.0f);
        int lo = 127, hi = 0;
        for (auto& n : c.notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
        const float span = (float) juce::jmax (2, hi - lo);
        const double repW = c.contentLenBeats / beatsPerBar * barWidth();

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        for (double repX = r.getX(); repX < r.getRight() - 0.5; repX += repW)
        {
            for (auto& n : c.notes)
            {
                const float nx = (float) (repX + (n.startBeat / c.contentLenBeats) * repW);
                const float nw = juce::jmax (2.0f, (float) ((n.lengthBeats / c.contentLenBeats) * repW));
                const float ny = notesArea.getBottom() - ((n.pitch - lo) / span) * notesArea.getHeight();
                g.fillRect (juce::jlimit (r.getX(), r.getRight() - 1.0f, nx), ny - 1.5f,
                            juce::jmin (nw, r.getRight() - nx), 2.2f);
            }
        }
    }

    g.setColour (selected ? juce::Colours::white : juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 3.0f, selected ? 2.0f : 1.0f);

    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (c.name.isNotEmpty() ? c.name : t.name, r.reduced (5.0f, 1.0f).withHeight (13.0f),
                juce::Justification::centredLeft, true);
}

void ArrangeView::paint (juce::Graphics& g)
{
    g.fillAll (Palette::inset);
    const int bars = numBars();
    const float bw = barWidth();
    const auto h = (float) getHeight();

    // Header column background.
    g.setColour (Palette::panel);
    g.fillRect (0, 0, headerWidth, getHeight());

    // Ruler.
    g.setColour (Palette::header);
    g.fillRect (0, 0, getWidth(), rulerHeight);
    for (int b = 0; b <= bars; ++b)
    {
        const float x = xForBeat ((double) b * beatsPerBar);
        g.setColour (Palette::line);
        g.drawVerticalLine ((int) x, 0.0f, h);
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1), (int) x + 4, 2, 30, rulerHeight - 3,
                    juce::Justification::centredLeft, false);
    }

    // Tracks.
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        const Track* t = tracks[(size_t) i].get();
        const int y = rulerHeight + i * trackHeight;

        // Lane background.
        g.setColour ((i % 2 == 0) ? Palette::inset : Palette::inset.brighter (0.10f));
        g.fillRect (headerWidth, y, getWidth() - headerWidth, trackHeight);

        // Header.
        g.setColour ((i % 2 == 0) ? Palette::panel : Palette::panelAlt);
        g.fillRect (0, y, headerWidth, trackHeight);
        if (i == selTrack)
        {
            g.setColour (Palette::accent.withAlpha (0.12f));
            g.fillRect (0, y, headerWidth, trackHeight);
        }
        g.setColour (t->colour);
        g.fillRect (0, y + 3, 4, trackHeight - 6);

        g.setColour (i == selTrack ? Palette::textBright : Palette::text);
        g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        g.drawText (t->name, 12, y + 4, headerWidth - 70, 18, juce::Justification::centredLeft, true);

        const char* typeName = t->type == TrackType::Instrument ? "INSTRUMENT"
                             : t->type == TrackType::Audio ? "AUDIO" : "MIDI OUT";
        g.setColour (Palette::textDim);
        g.setFont (Palette::sectionFont());
        g.drawText (typeName, 12, y + 22, headerWidth - 70, 12, juce::Justification::centredLeft, false);

        // Clips.
        for (int ci = 0; ci < (int) t->clips.size(); ++ci)
        {
            const auto& c = t->clips[(size_t) ci];
            juce::Rectangle<float> r (xForBeat (c.startBeat), (float) y + 2.0f,
                                      xForBeat (c.endBeat()) - xForBeat (c.startBeat),
                                      (float) trackHeight - 4.0f);
            drawClip (g, *t, c, r, i == selTrack && ci == selClip);
        }

        g.setColour (Palette::lineSoft);
        g.drawHorizontalLine (y + trackHeight, 0.0f, (float) getWidth());
    }

    // Header divider + playhead.
    g.setColour (Palette::line);
    g.drawVerticalLine (headerWidth, 0.0f, h);
    const float px = xForBeat (transport.getPlayheadBeats());
    if (px >= headerWidth)
    {
        g.setColour (Palette::playhead);
        g.drawVerticalLine ((int) px, 0.0f, h);
    }
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------
void ArrangeView::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    const int track = trackAtY (p.y);
    if (track < 0 || p.y < rulerHeight)
        return;

    if (p.x < headerWidth)   // header click selects the track
    {
        selTrack = track; selClip = -1;
        if (onClipSelected) onClipSelected (track, -1);
        repaint();
        return;
    }

    const int hit = clipAt (track, p);

    if (hit >= 0 && (e.mods.isPopupMenu() || e.getNumberOfClicks() >= 2))
    {
        {
            const juce::ScopedLock sl (engineLock);
            auto& clips = tracks[(size_t) track]->clips;
            clips.erase (clips.begin() + hit);
        }
        selTrack = selClip = -1;
        drag = Drag::none;
        if (onClipSelected) onClipSelected (-1, -1);
        if (onChanged) onChanged();
        repaint();
        return;
    }

    if (hit >= 0)
    {
        selTrack = dragTrack = track;
        selClip  = dragClip  = hit;
        const auto& c = tracks[(size_t) track]->clips[(size_t) hit];
        const float rightX = xForBeat (c.endBeat());
        if (std::abs (p.x - rightX) <= 6.0f)
            drag = Drag::resize;
        else { drag = Drag::move; dragBeatOffset = beatForX (p.x) - c.startBeat; }
    }
    else if (tracks[(size_t) track]->type != TrackType::Instrument)
    {
        // Empty click on a non-instrument track just selects it.
        selTrack = track; selClip = -1;
        if (onClipSelected) onClipSelected (track, -1);
        repaint();
        return;
    }
    else   // create a new 1-bar MIDI clip
    {
        Clip c;
        c.startBeat      = juce::jmax (0.0, snapToBar (beatForX (p.x)));
        c.lengthBeats    = beatsPerBar;
        c.contentLenBeats = beatsPerBar;
        c.looped         = true;
        {
            const juce::ScopedLock sl (engineLock);
            tracks[(size_t) track]->clips.push_back (c);
            selClip = (int) tracks[(size_t) track]->clips.size() - 1;
        }
        selTrack = dragTrack = track;
        dragClip = selClip;
        drag = Drag::resize;
    }

    if (onClipSelected) onClipSelected (selTrack, selClip);
    if (onChanged) onChanged();
    repaint();
}

void ArrangeView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragTrack < 0 || dragClip < 0)
        return;
    {
        const juce::ScopedLock sl (engineLock);
        if (! juce::isPositiveAndBelow (dragTrack, (int) tracks.size())) return;
        auto& clips = tracks[(size_t) dragTrack]->clips;
        if (! juce::isPositiveAndBelow (dragClip, (int) clips.size())) return;
        auto& c = clips[(size_t) dragClip];

        if (drag == Drag::move)
            c.startBeat = juce::jmax (0.0, snapToBar (beatForX (e.position.x) - dragBeatOffset));
        else if (drag == Drag::resize)
            c.lengthBeats = juce::jmax (beatsPerBar, snapToBar (beatForX (e.position.x)) - c.startBeat);
    }
    if (onChanged) onChanged();
    repaint();
}

void ArrangeView::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::none;
    dragTrack = dragClip = -1;
}

void ArrangeView::timerCallback()
{
    if (transport.isPlaying())
        repaint();
}
