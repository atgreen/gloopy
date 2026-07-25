// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

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
    beatsPerBar = transport.beatsPerBar();   // follow the project time signature
    muteButtons.clear();
    soloButtons.clear();
    editButtons.clear();
    armButtons.clear();
    arpButtons.clear();
    volSliders.clear();
    removeAllChildren();

    for (int ti = 0; ti < (int) tracks.size(); ++ti)
    {
        Track* t = tracks[(size_t) ti].get();

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

        // Plugin UI button (plugin-instrument tracks only).
        auto edit = std::make_unique<juce::TextButton> ("UI");
        edit->setColour (juce::TextButton::buttonColourId, Palette::accentDim);
        edit->onClick = [this, ti] { if (onOpenTrackEditor) onOpenTrackEditor (ti); };
        addChildComponent (*edit);
        edit->setVisible (t->generator != nullptr && t->generator->getPluginInstance() != nullptr);
        editButtons.push_back (std::move (edit));

        // Record-arm (audio tracks) — a red toggle; the transport Record captures armed tracks.
        auto arm = std::make_unique<juce::TextButton> (juce::String::fromUTF8 ("\xe2\x97\x8f"));  // ●
        arm->setClickingTogglesState (true);
        arm->setToggleState (t->recordArmed.load(), juce::dontSendNotification);
        arm->setColour (juce::TextButton::buttonOnColourId, juce::Colours::red);
        arm->setTooltip ("Arm for audio recording");
        arm->onClick = [t, a = arm.get()] { t->recordArmed.store (a->getToggleState()); };
        addChildComponent (*arm);
        arm->setVisible (t->type == TrackType::Audio);
        armButtons.push_back (std::move (arm));

        // Live arpeggiator config (instrument tracks) — lights up when the arp is on.
        auto arp = std::make_unique<juce::TextButton> ("ARP");
        arp->setColour (juce::TextButton::buttonOnColourId, Palette::accent);
        arp->setToggleState (t->arp.enabled, juce::dontSendNotification);
        arp->setTooltip ("Live arpeggiator");
        arp->onClick = [this, ti] { if (onArpMenu) onArpMenu (ti); };
        addChildComponent (*arp);
        arp->setVisible (t->generator != nullptr);
        arpButtons.push_back (std::move (arp));
    }

    setSize (getWidth(), preferredHeight());
    resized();
    repaint();
}

void ArrangeView::resized()
{
    beatsPerBar = transport.beatsPerBar();
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        const int y = rulerHeight + i * trackHeight;
        soloButtons[(size_t) i]->setBounds (headerWidth - 62, y + 6, 26, 20);
        muteButtons[(size_t) i]->setBounds (headerWidth - 32, y + 6, 26, 20);
        editButtons[(size_t) i]->setBounds (headerWidth - 58, y + 28, 52, 16);
        armButtons [(size_t) i]->setBounds (headerWidth - 90, y + 6, 26, 20);
        arpButtons [(size_t) i]->setBounds (headerWidth - 34, y + 28, 28, 16);
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
    beatsPerBar = transport.beatsPerBar();   // keep the bar grid current with the time signature
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

    // Tempo markers — a small flag + BPM at each marker beat on the ruler.
    if (getTempoMarkers)
    {
        for (auto& mk : getTempoMarkers())
        {
            const float x = xForBeat (mk.first);
            if (x < headerWidth - 1.0f) continue;
            g.setColour (Palette::accent);
            juce::Path flag;
            flag.addTriangle (x, 0.0f, x + 8.0f, 0.0f, x, 8.0f);   // corner flag
            g.fillPath (flag);
            g.drawVerticalLine ((int) x, 0.0f, (float) rulerHeight);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (juce::String (mk.second, (mk.second == (int) mk.second) ? 0 : 1),
                        (int) x + 3, rulerHeight - 11, 44, 10,
                        juce::Justification::centredLeft, false);
        }
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

    // Loop region.
    if (transport.isLoopEnabled())
    {
        const float lx0 = juce::jmax ((float) headerWidth, xForBeat (transport.getLoopStartBeats()));
        const float lx1 = juce::jmax ((float) headerWidth, xForBeat (transport.getLoopEndBeats()));
        if (lx1 > lx0)
        {
            g.setColour (Palette::accent.withAlpha (0.10f));
            g.fillRect (lx0, (float) rulerHeight, lx1 - lx0, h - rulerHeight);
            g.setColour (Palette::accent);
            g.fillRect (lx0, 0.0f, lx1 - lx0, 4.0f);       // brace in the ruler
        }
    }

    // Punch region (Alt-drag the ruler) — where recording punches in/out.
    if (getPunchRange)
    {
        double pin = 0.0, pout = 0.0;
        if (getPunchRange (pin, pout))
        {
            const double viewEnd = (double) bars * beatsPerBar;
            const float x0 = juce::jmax ((float) headerWidth, xForBeat (pin));
            const float x1 = juce::jmax ((float) headerWidth, xForBeat (juce::jmin (pout, viewEnd)));
            if (x1 > x0)
            {
                g.setColour (Palette::red.withAlpha (0.12f));
                g.fillRect (x0, (float) rulerHeight, x1 - x0, h - rulerHeight);
                g.setColour (Palette::red);
                g.fillRect (x0, 4.0f, x1 - x0, 4.0f);       // punch brace, below the loop brace
            }
        }
    }

    // Header divider + playhead (with a grab handle in the ruler).
    g.setColour (Palette::line);
    g.drawVerticalLine (headerWidth, 0.0f, h);
    const float px = xForBeat (transport.getPlayheadBeats());
    if (px >= headerWidth)
    {
        g.setColour (Palette::playhead);
        g.drawVerticalLine ((int) px, 0.0f, h);
        juce::Path tri;
        tri.addTriangle (px - 5.0f, 0.0f, px + 5.0f, 0.0f, px, 8.0f);
        g.fillPath (tri);
    }
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------
void ArrangeView::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;

    // --- ruler: right-click -> tempo-marker menu ---
    if (p.y < rulerHeight && p.x >= headerWidth && e.mods.isPopupMenu())
    {
        const double beat = juce::jmax (0.0, snapToBar (beatForX (p.x)));
        // Is there already a marker near this bar?
        double nearBeat = -1.0;
        if (getTempoMarkers)
            for (auto& mk : getTempoMarkers())
                if (std::abs (mk.first - beat) < 1e-6) { nearBeat = mk.first; break; }

        juce::PopupMenu m;
        m.addSectionHeader ("Bar " + juce::String ((int) (beat / beatsPerBar) + 1));
        m.addItem (1, "Add tempo marker...");
        m.addItem (2, "Remove tempo marker", nearBeat >= 0.0);
        m.addSeparator();
        m.addItem (3, "Time signature... (" + juce::String (transport.getTimeSigNumerator())
                        + "/" + juce::String (transport.getTimeSigDenominator()) + ")");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, beat, nearBeat] (int r)
        {
            if (r == 1) promptAddTempoMarker (beat);
            else if (r == 2 && nearBeat >= 0.0 && onRemoveTempoMarker) onRemoveTempoMarker (nearBeat);
            else if (r == 3) promptTimeSignature();
        });
        return;
    }

    // --- ruler: seek / loop region (Alt = punch region) ---
    if (p.y < rulerHeight && p.x >= headerWidth)
    {
        rulerAlt = e.mods.isAltDown();
        if (e.getNumberOfClicks() >= 2)   // double-click clears the loop (or punch, with Alt)
        {
            if (rulerAlt) { if (onSetPunchRange) onSetPunchRange (false, 0.0, 0.0); }
            else          { transport.setLoopEnabled (false); if (onLoopChanged) onLoopChanged(); }
            repaint();
            return;
        }
        rulerDrag = true;
        loopDragged = false;
        rulerStartBeat = juce::jmax (0.0, beatForX (p.x));
        if (! rulerAlt) transport.requestSeek (rulerStartBeat);   // Alt-drag sets punch, doesn't scrub
        repaint();
        return;
    }

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

    // Right-click a clip: context menu of the everyday edit ops (same operations the
    // control API exposes as SplitClip/DuplicateClip/ReverseClip/SnapClipToScale).
    if (hit >= 0 && e.mods.isPopupMenu())
    {
        selTrack = track; selClip = hit;
        if (onClipSelected) onClipSelected (track, hit);
        repaint();

        bool isMidi = false, isTake = false, isMutedTake = false, isLoopedMidi = false;
        {
            const juce::ScopedLock sl (engineLock);
            if (juce::isPositiveAndBelow (hit, (int) tracks[(size_t) track]->clips.size()))
            {
                const auto& cl = tracks[(size_t) track]->clips[(size_t) hit];
                isMidi = ! cl.isAudio();
                isTake = cl.takeId.isNotEmpty();
                isMutedTake = isTake && cl.muted;
                isLoopedMidi = isMidi && cl.looped && cl.contentLenBeats > 0.0
                               && cl.contentLenBeats < cl.lengthBeats - 1.0e-9;   // actually tiles
            }
        }

        juce::PopupMenu m;
        m.addItem (1, "Split at playhead");
        m.addItem (2, "Duplicate");
        m.addItem (3, "Reverse");
        m.addItem (4, "Snap to scale", isMidi);
        m.addItem (13, "Crop to loop region", transport.isLoopEnabled());   // MIDI notes or audio buffer
        m.addItem (14, "Consolidate loops", isLoopedMidi);   // bake looped repetitions into notes
        if (! isMidi)                                   // audio-clip level ops
        {
            m.addItem (10, "Normalize");                // to -1 dBFS
            m.addItem (11, "Gain...");
            m.addItem (12, "Fades...");
        }
        if (isTake)
        {
            m.addSeparator();
            m.addItem (5, "Use this take", isMutedTake);    // comp: make this the active take
            m.addItem (6, "Promote take (keep)");           // move raw scratch -> recordings
            m.addItem (7, "Clean up unused takes");
        }
        m.addSeparator();
        m.addItem (9, "Delete");
        const int t = track, c = hit;
        m.showMenuAsync (juce::PopupMenu::Options(), [this, t, c] (int r)
        {
            if (r == 0) return;
            if (r == 11) { promptClipGain (t, c); return; }        // "Gain..." -> dB prompt
            if (r == 12) { promptClipFades (t, c); return; }       // "Fades..." -> in/out prompt
            const char* cmd = r == 1  ? "split"
                            : r == 2  ? "duplicate"
                            : r == 3  ? "reverse"
                            : r == 4  ? "snapscale"
                            : r == 13 ? "croploop"
                            : r == 14 ? "consolidate"
                            : r == 10 ? "normalize"
                            : r == 5  ? "usetake"
                            : r == 6  ? "promotetake"
                            : r == 7  ? "cleanuptakes"
                            :           "delete";
            if (onClipCommand) onClipCommand (t, c, cmd);
        });
        return;
    }

    // Double-click a clip still deletes it directly (fast removal gesture).
    if (hit >= 0 && e.getNumberOfClicks() >= 2)
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

void ArrangeView::promptClipGain (int track, int clip)
{
    auto* aw = new juce::AlertWindow ("Clip gain", "Gain in dB:", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("db", "0.0");
    aw->addButton ("Set",    1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, track, clip] (int r)
    {
        if (r == 1 && onClipGain)
            onClipGain (track, clip, aw->getTextEditorContents ("db").getFloatValue());
        delete aw;
    }), false);
}

void ArrangeView::promptClipFades (int track, int clip)
{
    auto* aw = new juce::AlertWindow ("Clip fades", "Fade in / out (beats):", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("in",  "0.0", "Fade in");
    aw->addTextEditor ("out", "0.0", "Fade out");
    aw->addButton ("Set",    1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, track, clip] (int r)
    {
        if (r == 1 && onClipFades)
            onClipFades (track, clip,
                         aw->getTextEditorContents ("in").getDoubleValue(),
                         aw->getTextEditorContents ("out").getDoubleValue());
        delete aw;
    }), false);
}

void ArrangeView::promptTimeSignature()
{
    auto* aw = new juce::AlertWindow ("Time signature", "Beats per bar / note value:", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("num", juce::String (transport.getTimeSigNumerator()),   "Numerator");
    aw->addTextEditor ("den", juce::String (transport.getTimeSigDenominator()), "Denominator");
    aw->addButton ("Set",    1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int r)
    {
        if (r == 1 && onSetTimeSignature)
        {
            const int n = aw->getTextEditorContents ("num").getIntValue();
            const int d = aw->getTextEditorContents ("den").getIntValue();
            if (n >= 1 && d >= 1) onSetTimeSignature (n, d);
        }
        delete aw;
    }), false);
}

void ArrangeView::promptAddTempoMarker (double beat)
{
    auto* aw = new juce::AlertWindow ("Tempo marker",
                                      "BPM at bar " + juce::String ((int) (beat / beatsPerBar) + 1),
                                      juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("bpm", juce::String (transport.getBpm(), 1));
    aw->addButton ("Add",    1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, beat] (int r)
    {
        if (r == 1)
        {
            const double bpm = aw->getTextEditorContents ("bpm").getDoubleValue();
            if (bpm >= 20.0 && bpm <= 400.0 && onAddTempoMarker) onAddTempoMarker (beat, bpm);
        }
        delete aw;
    }), false);
}

void ArrangeView::mouseDrag (const juce::MouseEvent& e)
{
    if (rulerDrag)
    {
        const double b = juce::jmax (0.0, beatForX (e.position.x));
        if (std::abs (b - rulerStartBeat) > 0.5)   // became a range
        {
            double s = snapToBar (juce::jmin (rulerStartBeat, b));
            double f = snapToBar (juce::jmax (rulerStartBeat, b));
            if (f - s < beatsPerBar) f = s + beatsPerBar;
            if (rulerAlt)                          // Alt → punch region
            {
                if (onSetPunchRange) onSetPunchRange (true, s, f);
            }
            else                                   // plain → loop region
            {
                loopDragged = true;
                transport.setLoopRegion (s, f);
                transport.setLoopEnabled (true);
                if (onLoopChanged) onLoopChanged();
            }
        }
        else if (! rulerAlt)
        {
            transport.requestSeek (b);   // still just scrubbing
        }
        repaint();
        return;
    }

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
    rulerDrag = false;
    rulerAlt = false;
}

void ArrangeView::timerCallback()
{
    // Repaint so the playhead tracks both playback and manual seeks.
    repaint();
}
