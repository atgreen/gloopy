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
    g.setColour (c.colour.getARGB() != 0 ? c.colour : t.colour);   // per-clip override, else the track colour
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

    // Named timeline markers — a cyan flag + label at each marker beat (distinct from the
    // accent-coloured tempo markers above).
    if (getMarkers)
    {
        for (auto& mk : getMarkers())
        {
            const float x = xForBeat (mk.second);
            if (x < headerWidth - 1.0f) continue;
            g.setColour (juce::Colours::aquamarine);
            juce::Path flag;
            flag.addTriangle (x, 0.0f, x + 8.0f, 0.0f, x, 8.0f);
            g.fillPath (flag);
            g.drawVerticalLine ((int) x, 0.0f, (float) rulerHeight);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (mk.first, (int) x + 3, 1, 90, 9, juce::Justification::centredLeft, false);
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

        // Is there a named marker near this bar (for Remove)?
        juce::String nearMarker;
        if (getMarkers)
            for (auto& mk : getMarkers())
                if (std::abs (mk.second - beat) < 1e-6) { nearMarker = mk.first; break; }

        juce::PopupMenu m;
        m.addSectionHeader ("Bar " + juce::String ((int) (beat / beatsPerBar) + 1));
        m.addItem (1, "Add tempo marker...");
        m.addItem (2, "Remove tempo marker", nearBeat >= 0.0);
        m.addSeparator();
        m.addItem (4, "Add marker...");                                   // named timeline marker
        m.addItem (5, "Remove marker" + (nearMarker.isNotEmpty() ? " (" + nearMarker + ")" : juce::String()), nearMarker.isNotEmpty());
        m.addSeparator();
        m.addItem (3, "Time signature... (" + juce::String (transport.getTimeSigNumerator())
                        + "/" + juce::String (transport.getTimeSigDenominator()) + ")");
        // Swing (groove): shift every other 1/8 note later. Presets, ticked at the current value.
        const double curSwing = getSwing ? getSwing() : 0.5;
        const std::pair<const char*, double> swingPresets[] = {
            { "Straight", 0.50 }, { "Light 56%", 0.56 }, { "Medium 62%", 0.62 },
            { "Heavy 68%", 0.68 }, { "Triplet 67%", 0.667 } };
        juce::PopupMenu sw;
        for (int i = 0; i < 5; ++i)
            sw.addItem (30 + i, swingPresets[i].first, true, std::abs (curSwing - swingPresets[i].second) < 0.005);
        m.addSubMenu ("Swing", sw);
        // Metronome click volume.
        const float curMetro = getMetronomeLevel ? getMetronomeLevel() : 1.0f;
        const std::pair<const char*, float> metroLvls[] = { {"25%", 0.25f}, {"50%", 0.5f}, {"75%", 0.75f}, {"100%", 1.0f} };
        juce::PopupMenu mm;
        for (int i = 0; i < 4; ++i)
            mm.addItem (60 + i, metroLvls[i].first, true, std::abs (curMetro - metroLvls[i].second) < 0.01f);
        m.addSubMenu ("Metronome level", mm);
        m.showMenuAsync (juce::PopupMenu::Options(), [this, beat, nearBeat, nearMarker] (int r)
        {
            if (r == 1) promptAddTempoMarker (beat);
            else if (r == 2 && nearBeat >= 0.0 && onRemoveTempoMarker) onRemoveTempoMarker (nearBeat);
            else if (r == 4) promptAddMarker (beat);
            else if (r == 5 && nearMarker.isNotEmpty() && onRemoveMarker) onRemoveMarker (nearMarker);
            else if (r == 3) promptTimeSignature();
            else if (r >= 30 && r <= 34 && onSetSwing)
            {
                const double vals[] = { 0.50, 0.56, 0.62, 0.68, 0.667 };
                onSetSwing (vals[r - 30]);
            }
            else if (r >= 60 && r <= 63 && onSetMetronomeLevel)
            {
                const float vals[] = { 0.25f, 0.5f, 0.75f, 1.0f };
                onSetMetronomeLevel (vals[r - 60]);
            }
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

        // Right-click a track header -> track menu (Rename; Sampler window for sampler tracks).
        if (e.mods.isPopupMenu())
        {
            SamplerCtl sc {};
            if (getSamplerControls) sc = getSamplerControls (track);
            juce::String curName;
            bool curPolarity = false;
            {
                const juce::ScopedLock sl (engineLock);
                if (juce::isPositiveAndBelow (track, (int) tracks.size()))
                {
                    curName = tracks[(size_t) track]->name;
                    curPolarity = tracks[(size_t) track]->polarity.load();
                }
            }
            const int tk = track;

            // Preset track colours (label -> 8-hex ARGB), menu ids 10..17.
            static const std::pair<const char*, const char*> kColours[] = {
                { "Red",    "ffef5350" }, { "Orange", "ffffa726" }, { "Yellow", "ffffee58" },
                { "Green",  "ff66bb6a" }, { "Teal",   "ff26a69a" }, { "Blue",   "ff42a5f5" },
                { "Purple", "ffab47bc" }, { "Grey",   "ff90a4ae" } };

            const int numTracks = (int) tracks.size();

            juce::PopupMenu m;
            m.addItem (1, "Rename track...");
            juce::PopupMenu cm;
            for (int i = 0; i < (int) numElementsInArray (kColours); ++i)
                cm.addItem (10 + i, kColours[i].first);
            m.addSubMenu ("Colour", cm);
            m.addSeparator();
            m.addItem (3, "Move up",   tk > 0);
            m.addItem (4, "Move down", tk < numTracks - 1);
            m.addItem (5, "Invert phase", true, curPolarity);   // checkable polarity flip
            m.addSeparator();
            m.addItem (6, "Export track (WAV)...");             // bounce this track to a stem
            if (sc.isSampler) { m.addSeparator(); m.addItem (2, "Sampler playback window..."); }
            m.showMenuAsync (juce::PopupMenu::Options(), [this, tk, curName, sc, curPolarity] (int r)
            {
                if (r >= 10 && r < 10 + (int) numElementsInArray (kColours))
                {
                    if (onSetTrackColour) onSetTrackColour (tk, kColours[r - 10].second);
                }
                else if (r == 3) { if (onMoveTrack) onMoveTrack (tk, -1); }   // up
                else if (r == 4) { if (onMoveTrack) onMoveTrack (tk, +1); }   // down
                else if (r == 5) { if (onSetTrackPolarity) onSetTrackPolarity (tk, ! curPolarity); }   // toggle phase
                else if (r == 6) { if (onExportTrack) onExportTrack (tk); }   // export stem
                else if (r == 1)
                {
                    auto* rw = new juce::AlertWindow ("Rename track", "New track name", juce::MessageBoxIconType::NoIcon);
                    rw->addTextEditor ("name", curName, "Name");
                    rw->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
                    rw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                    rw->enterModalState (true, juce::ModalCallbackFunction::create ([this, rw, tk] (int rr)
                    {
                        if (rr == 1 && onRenameTrack) onRenameTrack (tk, rw->getTextEditorContents ("name"));
                        delete rw;
                    }), false);
                }
                else if (r == 2 && sc.isSampler)
                {
                    auto* aw = new juce::AlertWindow ("Sampler", "One-shot playback window", juce::MessageBoxIconType::NoIcon);
                aw->addTextEditor ("start", juce::String (sc.start, 3), "Start (0..1)");
                aw->addTextEditor ("end",   juce::String (sc.end, 3),   "End (0..1)");
                aw->addTextEditor ("fadein",  juce::String (sc.fadeIn, 3),  "Fade in (s)");
                aw->addTextEditor ("fadeout", juce::String (sc.fadeOut, 3), "Fade out (s)");
                aw->addTextEditor ("root",  juce::String (sc.root),     "Root note");
                juce::StringArray dir { "Forward", "Reverse" };
                aw->addComboBox ("dir", dir, "Direction");
                aw->getComboBoxComponent ("dir")->setSelectedItemIndex (sc.reverse ? 1 : 0);
                juce::StringArray mode { "One-shot", "Loop" };
                aw->addComboBox ("mode", mode, "Mode");
                aw->getComboBoxComponent ("mode")->setSelectedItemIndex (sc.loop ? 1 : 0);
                juce::StringArray voices { "Poly", "Mono (choke)" };
                aw->addComboBox ("voices", voices, "Voices");
                aw->getComboBoxComponent ("voices")->setSelectedItemIndex (sc.mono ? 1 : 0);
                aw->addButton ("Apply",  1, juce::KeyPress (juce::KeyPress::returnKey));
                aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, tk] (int r)
                {
                    if (r == 1 && onSetSamplerControls)
                    {
                        const float s   = aw->getTextEditorContents ("start").getFloatValue();
                        const float en  = aw->getTextEditorContents ("end").getFloatValue();
                        const float fi  = aw->getTextEditorContents ("fadein").getFloatValue();
                        const float fo  = aw->getTextEditorContents ("fadeout").getFloatValue();
                        const int   rt  = aw->getTextEditorContents ("root").getIntValue();
                        const bool  rev = aw->getComboBoxComponent ("dir")->getSelectedItemIndex() == 1;
                        const bool  lp  = aw->getComboBoxComponent ("mode")->getSelectedItemIndex() == 1;
                        const bool  mn  = aw->getComboBoxComponent ("voices")->getSelectedItemIndex() == 1;
                        onSetSamplerControls (tk, s, en, rev, rt, fi, fo, lp, mn);
                    }
                    delete aw;
                }), false);
                }
            });
        }
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

        bool isMidi = false, isTake = false, isMutedTake = false, isLoopedMidi = false, isMuted = false;
        double clipStart = 0.0, clipEnd = 0.0;
        {
            const juce::ScopedLock sl (engineLock);
            if (juce::isPositiveAndBelow (hit, (int) tracks[(size_t) track]->clips.size()))
            {
                const auto& cl = tracks[(size_t) track]->clips[(size_t) hit];
                isMidi = ! cl.isAudio();
                isTake = cl.takeId.isNotEmpty();
                isMuted = cl.muted;
                isMutedTake = isTake && cl.muted;
                isLoopedMidi = isMidi && cl.looped && cl.contentLenBeats > 0.0
                               && cl.contentLenBeats < cl.lengthBeats - 1.0e-9;   // actually tiles
                clipStart = cl.startBeat;
                clipEnd   = cl.startBeat + cl.lengthBeats;
            }
        }

        // Markers that fall strictly inside this clip -> "Split at marker" submenu.
        std::vector<std::pair<juce::String, double>> clipMarkers;
        if (getMarkers)
            for (auto& mk : getMarkers())
                if (mk.second > clipStart + 1.0e-6 && mk.second < clipEnd - 1.0e-6)
                    clipMarkers.push_back (mk);

        juce::PopupMenu m;
        m.addItem (1, "Split at playhead");
        if (! clipMarkers.empty())
        {
            juce::PopupMenu markerMenu;
            for (int i = 0; i < (int) clipMarkers.size(); ++i)
                markerMenu.addItem (500 + i, clipMarkers[(size_t) i].first);
            m.addSubMenu ("Split at marker", markerMenu);
        }
        m.addItem (2, "Duplicate");
        {
            juce::PopupMenu rep;   // tile the clip N times total (adds N-1 butted copies)
            rep.addItem (602, "x2"); rep.addItem (604, "x4"); rep.addItem (608, "x8"); rep.addItem (616, "x16");
            m.addSubMenu ("Repeat", rep);
        }
        m.addItem (3, "Reverse");
        m.addItem (4, "Snap to scale", isMidi);
        m.addItem (13, "Crop to loop region", transport.isLoopEnabled());   // MIDI notes or audio buffer
        m.addItem (14, "Consolidate loops", isLoopedMidi);   // bake looped repetitions into notes
        m.addItem (15, "Bounce to audio");                   // freeze clip -> audio on a new track
        m.addItem (17, "Mute clip", ! isTake, isMuted);      // disable/enable in the arrangement (takes use Use/Promote)
        m.addItem (18, "Loop this clip");                    // set the transport loop to this clip's span
        m.addItem (19, "Copy notes (JSON)", isMidi);         // notes -> system clipboard as JSON
        m.addItem (20, "Rename clip...");                    // set the clip's label
        {
            juce::PopupMenu ccm;                             // per-clip colour override (else inherit the track)
            static const std::pair<const char*, const char*> kClipCols[] = {
                { "Red", "ffef5350" }, { "Orange", "ffffa726" }, { "Yellow", "ffffee58" },
                { "Green", "ff66bb6a" }, { "Teal", "ff26a69a" }, { "Blue", "ff42a5f5" },
                { "Purple", "ffab47bc" }, { "Grey", "ff90a4ae" } };
            for (int i = 0; i < (int) numElementsInArray (kClipCols); ++i)
                ccm.addItem (810 + i, kClipCols[i].first);
            ccm.addSeparator();
            ccm.addItem (818, "Inherit track");
            m.addSubMenu ("Colour", ccm);
        }
        if (isMidi)                                          // non-destructive playback transpose
        {
            juce::PopupMenu tr;
            const std::pair<const char*, int> opts[] = {
                { "-12 (octave)", -12 }, { "-7 (fifth)", -7 }, { "-5 (fourth)", -5 }, { "-2", -2 },
                { "Reset (0)", 0 }, { "+2", 2 }, { "+5 (fourth)", 5 }, { "+7 (fifth)", 7 }, { "+12 (octave)", 12 } };
            for (int i = 0; i < 9; ++i) tr.addItem (700 + i, opts[i].first);
            m.addSubMenu ("Transpose", tr);
            juce::PopupMenu vel;                             // non-destructive playback velocity scale
            const std::pair<const char*, int> vopts[] = {
                { "25%", 25 }, { "50%", 50 }, { "75%", 75 }, { "100% (reset)", 100 }, { "125%", 125 }, { "150%", 150 }, { "200%", 200 } };
            for (int i = 0; i < 7; ++i) vel.addItem (710 + i, vopts[i].first);
            m.addSubMenu ("Velocity", vel);
            juce::PopupMenu prob;                            // generative per-note fire probability
            const std::pair<const char*, int> popts[] = {
                { "100% (always)", 100 }, { "75%", 75 }, { "50%", 50 }, { "25%", 25 }, { "10%", 10 } };
            for (int i = 0; i < 5; ++i) prob.addItem (720 + i, popts[i].first);
            m.addSubMenu ("Probability", prob);
            juce::PopupMenu vr;                              // destructive velocity ramp across the clip
            vr.addItem (740, "Crescendo");                  // soft -> loud
            vr.addItem (741, "Decrescendo");                // loud -> soft
            m.addSubMenu ("Velocity ramp", vr);
            juce::PopupMenu fv;                              // flatten all velocities to one value
            fv.addItem (820, "Flat 100%");
            fv.addItem (821, "Flat 75%");
            fv.addItem (822, "Flat 50%");
            fv.addItem (823, "Flat 25%");
            m.addSubMenu ("Flatten velocity", fv);
            juce::PopupMenu ts;                             // time-scale the clip's rhythm
            ts.addItem (742, "Double-time (faster)");       // factor 0.5
            ts.addItem (743, "Half-time (slower)");         // factor 2
            m.addSubMenu ("Time", ts);
            juce::PopupMenu ec;                             // MIDI echo: decaying note repeats
            ec.addItem (744, "1/8 note x3");                // delay 0.5, 3 reps
            ec.addItem (745, "1/16 note x4");               // delay 0.25, 4 reps
            m.addSubMenu ("Echo", ec);
            m.addItem (746, "Invert (mirror pitches)");     // melodic inversion around the first note
            juce::PopupMenu rt;                             // ratchet: subdivide each note into rapid hits
            rt.addItem (750, "x2");
            rt.addItem (751, "x3");
            rt.addItem (752, "x4");
            m.addSubMenu ("Ratchet", rt);
            juce::PopupMenu hm;                             // harmonize: add a parallel interval voice
            hm.addItem (760, "Minor 3rd (+3)");
            hm.addItem (761, "Major 3rd (+4)");
            hm.addItem (762, "Perfect 5th (+7)");
            hm.addItem (763, "Octave (+12)");
            hm.addItem (764, "Octave down (-12)");
            m.addSubMenu ("Harmonize", hm);
            juce::PopupMenu sw;                            // swing: bake a groove (delay off-beats)
            sw.addItem (770, "1/8 light");
            sw.addItem (771, "1/8 medium");
            sw.addItem (772, "1/8 heavy");
            sw.addItem (773, "1/16 light");
            sw.addItem (774, "1/16 medium");
            sw.addItem (775, "1/16 heavy");
            m.addSubMenu ("Swing", sw);
            juce::PopupMenu cd;                            // chordify: turn each note into a named chord
            cd.addItem (780, "Major");
            cd.addItem (781, "Minor");
            cd.addItem (782, "Dominant 7th");
            cd.addItem (783, "Diminished");
            cd.addItem (784, "Sus4");
            m.addSubMenu ("Chord", cd);
            juce::PopupMenu qz;                            // quantize note starts (full or 50% soft)
            qz.addItem (790, "1/16");
            qz.addItem (791, "1/16 soft (50%)");
            qz.addItem (792, "1/8");
            qz.addItem (793, "1/8 soft (50%)");
            qz.addItem (794, "1/4");
            qz.addItem (795, "1/4 soft (50%)");
            m.addSubMenu ("Quantize", qz);
            juce::PopupMenu gt;                            // gate: scale note lengths (articulation)
            gt.addItem (800, "Staccato (50%)");
            gt.addItem (801, "Short (75%)");
            gt.addItem (802, "Tenuto (150%)");
            gt.addItem (803, "Double (200%)");
            m.addSubMenu ("Note length", gt);
        }
        if (! isMidi)                                   // audio-clip level ops
        {
            m.addItem (10, "Normalize");                // to -1 dBFS
            m.addItem (11, "Gain...");
            m.addItem (12, "Fades...");
            juce::PopupMenu fadeShapeM;                 // curve for the fade edges
            const char* fadeShapes[] = { "Linear", "Equal power", "Exponential" };
            for (int i = 0; i < 3; ++i) fadeShapeM.addItem (730 + i, fadeShapes[i]);
            m.addSubMenu ("Fade shape", fadeShapeM);
            m.addItem (16, "Slice at transients");      // detect onsets -> split into slices
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
        m.showMenuAsync (juce::PopupMenu::Options(), [this, t, c, clipMarkers, isMuted] (int r)
        {
            if (r == 0) return;
            if (r >= 500 && r - 500 < (int) clipMarkers.size())    // "Split at marker <name>"
            { if (onClipCommand) onClipCommand (t, c, "splitmarker:" + clipMarkers[(size_t) (r - 500)].first); return; }
            if (r == 17) { if (onClipCommand) onClipCommand (t, c, isMuted ? "unmute" : "mute"); return; }
            if (r >= 602 && r <= 616)    // Repeat xN -> add N-1 copies
            { if (onClipCommand) onClipCommand (t, c, "repeat:" + juce::String ((r - 600) - 1)); return; }
            if (r == 11) { promptClipGain (t, c); return; }        // "Gain..." -> dB prompt
            if (r == 12) { promptClipFades (t, c); return; }       // "Fades..." -> in/out prompt
            if (r == 19) { if (onClipCommand) onClipCommand (t, c, "copynotes"); return; }   // notes -> clipboard
            if (r == 20)   // Rename clip: prompt (prefilled with the clip's current name)
            {
                juce::String cur;
                { const juce::ScopedLock sl (engineLock);
                  if (juce::isPositiveAndBelow (t, (int) tracks.size())
                      && juce::isPositiveAndBelow (c, (int) tracks[(size_t) t]->clips.size()))
                      cur = tracks[(size_t) t]->clips[(size_t) c].name; }
                auto* rw = new juce::AlertWindow ("Rename clip", "New clip name (blank = track name)", juce::MessageBoxIconType::NoIcon);
                rw->addTextEditor ("name", cur, "Name");
                rw->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
                rw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                rw->enterModalState (true, juce::ModalCallbackFunction::create ([this, rw, t, c] (int rr)
                {
                    if (rr == 1 && onRenameClip) onRenameClip (t, c, rw->getTextEditorContents ("name"));
                    delete rw;
                }), false);
                return;
            }
            if (r >= 810 && r <= 818)   // Colour: per-clip override (818 = inherit the track)
            {
                static const char* cols[] = { "ffef5350", "ffffa726", "ffffee58", "ff66bb6a",
                                              "ff26a69a", "ff42a5f5", "ffab47bc", "ff90a4ae" };
                const juce::String hex = (r == 818) ? juce::String() : juce::String (cols[r - 810]);
                if (onClipCommand) onClipCommand (t, c, "clipcolour:" + hex);
                return;
            }
            if (r >= 700 && r <= 708)   // Transpose <semitones> (non-destructive)
            {
                const int vals[] = { -12, -7, -5, -2, 0, 2, 5, 7, 12 };
                if (onClipCommand) onClipCommand (t, c, "transpose:" + juce::String (vals[r - 700]));
                return;
            }
            if (r >= 710 && r <= 716)   // Velocity scale <percent> (non-destructive)
            {
                const int pcts[] = { 25, 50, 75, 100, 125, 150, 200 };
                if (onClipCommand) onClipCommand (t, c, "velscale:" + juce::String (pcts[r - 710]));
                return;
            }
            if (r >= 720 && r <= 724)   // Note fire probability <percent>
            {
                const int pcts[] = { 100, 75, 50, 25, 10 };
                if (onClipCommand) onClipCommand (t, c, "prob:" + juce::String (pcts[r - 720]));
                return;
            }
            if (r >= 730 && r <= 732)   // Audio-clip fade curve: 0 linear, 1 equal-power, 2 exp
            {
                if (onClipCommand) onClipCommand (t, c, "fadeshape:" + juce::String (r - 730));
                return;
            }
            if (r == 740 || r == 741)   // Velocity ramp: crescendo / decrescendo
            {
                if (onClipCommand) onClipCommand (t, c, r == 740 ? "velramp:up" : "velramp:down");
                return;
            }
            if (r >= 820 && r <= 823)   // Flatten velocity: 100/75/50/25%
            {
                const float v = r == 820 ? 1.0f : r == 821 ? 0.75f : r == 822 ? 0.5f : 0.25f;
                if (onClipCommand) onClipCommand (t, c, "flattenvel:" + juce::String (v));
                return;
            }
            if (r == 742 || r == 743)   // Time-scale: double-time (0.5) / half-time (2)
            {
                if (onClipCommand) onClipCommand (t, c, r == 742 ? "timescale:0.5" : "timescale:2");
                return;
            }
            if (r == 744 || r == 745)   // MIDI echo: 1/8 x3 or 1/16 x4
            {
                if (onClipCommand) onClipCommand (t, c, r == 744 ? "echo:0.5,3" : "echo:0.25,4");
                return;
            }
            if (r == 746) { if (onClipCommand) onClipCommand (t, c, "invert"); return; }   // melodic inversion
            if (r >= 750 && r <= 752)   // Ratchet: x2/x3/x4
            {
                if (onClipCommand) onClipCommand (t, c, "ratchet:" + juce::String (r - 748));
                return;
            }
            if (r >= 760 && r <= 764)   // Harmonize: +3 / +4 / +7 / +12 / -12
            {
                const int semis = r == 760 ? 3 : r == 761 ? 4 : r == 762 ? 7 : r == 763 ? 12 : -12;
                if (onClipCommand) onClipCommand (t, c, "harmonize:" + juce::String (semis));
                return;
            }
            if (r >= 770 && r <= 775)   // Swing: 1/8 (grid 0.5) or 1/16 (grid 0.25), light/medium/heavy
            {
                const double grid = r <= 772 ? 0.5 : 0.25;
                const float  amt  = (r == 770 || r == 773) ? 0.2f : (r == 771 || r == 774) ? 0.33f : 0.5f;
                if (onClipCommand) onClipCommand (t, c, "swing:" + juce::String (grid) + "," + juce::String (amt));
                return;
            }
            if (r >= 780 && r <= 784)   // Chord: major/minor/dom7/dim/sus4
            {
                if (onClipCommand) onClipCommand (t, c, "chordify:" + juce::String (r - 780));
                return;
            }
            if (r >= 790 && r <= 795)   // Quantize: 1/16, 1/8, 1/4 — full or 50% soft (odd ids)
            {
                const double grid = r <= 791 ? 0.25 : r <= 793 ? 0.5 : 1.0;
                const double str  = (r % 2 == 1) ? 0.5 : 1.0;   // 791/793/795 = soft
                if (onClipCommand) onClipCommand (t, c, "quantize:" + juce::String (grid) + "," + juce::String (str));
                return;
            }
            if (r >= 800 && r <= 803)   // Note length (gate): staccato/short/tenuto/double
            {
                const double factor = r == 800 ? 0.5 : r == 801 ? 0.75 : r == 802 ? 1.5 : 2.0;
                if (onClipCommand) onClipCommand (t, c, "gate:" + juce::String (factor));
                return;
            }
            const char* cmd = r == 1  ? "split"
                            : r == 2  ? "duplicate"
                            : r == 3  ? "reverse"
                            : r == 4  ? "snapscale"
                            : r == 13 ? "croploop"
                            : r == 14 ? "consolidate"
                            : r == 15 ? "bounce"
                            : r == 18 ? "loopclip"
                            : r == 16 ? "slicetransients"
                            : r == 10 ? "normalize"
                            : r == 5  ? "usetake"
                            : r == 6  ? "promotetake"
                            : r == 7  ? "cleanuptakes"
                            :           "delete";
            if (onClipCommand) onClipCommand (t, c, cmd);
        });
        return;
    }

    // Right-click empty track space -> "Paste notes here" (JSON clip notes from the clipboard).
    if (hit < 0 && e.mods.isPopupMenu() && p.x >= headerWidth && p.y >= rulerHeight)
    {
        const double beat = juce::jmax (0.0, beatForX (p.x));
        const bool haveClip = juce::SystemClipboard::getTextFromClipboard().trim().startsWithChar ('[');
        juce::PopupMenu m;
        m.addItem (1, "Paste notes here", haveClip);
        const int tk = track;
        m.showMenuAsync (juce::PopupMenu::Options(), [this, tk, beat] (int r)
        {
            if (r == 1 && onPasteNotes) onPasteNotes (tk, beat);
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

void ArrangeView::promptAddMarker (double beat)
{
    auto* aw = new juce::AlertWindow ("Marker",
                                      "Name for the marker at bar " + juce::String ((int) (beat / beatsPerBar) + 1),
                                      juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", "Marker");
    aw->addButton ("Add",    1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, beat] (int r)
    {
        if (r == 1)
        {
            const juce::String name = aw->getTextEditorContents ("name").trim();
            if (name.isNotEmpty() && onAddMarker) onAddMarker (name, beat);
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
