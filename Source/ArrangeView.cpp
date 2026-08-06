// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#include "ArrangeView.h"
#include "EngineLock.h"
#include "Palette.h"
#include <cmath>

ArrangeView::ArrangeView (std::vector<std::unique_ptr<Track>>& tracksRef,
                          Transport& transportRef, juce::CriticalSection& engineLockRef)
    : tracks (tracksRef), transport (transportRef), engineLock (engineLockRef)
{
    startTimerHz (30);
}

ArrangeView::~ArrangeView() { stopTimer(); }

bool ArrangeView::isExpanded (int i) const
{
    return juce::isPositiveAndBelow (i, (int) tracks.size())
             && expandedTracks.count (tracks[(size_t) i]->id) > 0;
}
int ArrangeView::laneCountFor (int track) const
{
    if (! juce::isPositiveAndBelow (track, (int) tracks.size())) return 0;
    const int tid = tracks[(size_t) track]->id;
    int n = 0;
    for (const auto& l : autoLanes) if (l.trackId == tid) ++n;
    return n;
}
int ArrangeView::rowHeight (int i) const
{
    return th() + (isExpanded (i) ? pickerRowH + laneCountFor (i) * laneRowH : 0);
}
int ArrangeView::rowTop (int i) const
{
    int y = rulerHeight;
    for (int k = 0; k < i && k < (int) tracks.size(); ++k) y += rowHeight (k);
    return y;
}

int ArrangeView::preferredHeight() const
{
    int h = rulerHeight;
    for (int i = 0; i < (int) tracks.size(); ++i) h += rowHeight (i);
    h += (int) busRows.size() * busRowH;                // content-less bus/master rows below
    return juce::jmax (rulerHeight + th(), h);   // always at least one row tall
}

void ArrangeView::refreshAutomation()
{
    autoLanes = getAutomation ? getAutomation() : std::vector<AutoLaneView>{};
    busRows   = getBusRows    ? getBusRows()    : std::vector<BusRowView>{};
    setSize (getWidth(), preferredHeight());   // lane count / bus rows affect total height
    resized();
    repaint();
}

void ArrangeView::rebuild()
{
    refreshMeter();
    muteButtons.clear();
    soloButtons.clear();
    editButtons.clear();
    armButtons.clear();
    arpButtons.clear();
    volSliders.clear();
    expandButtons.clear();
    paramButtons.clear();
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

        // Automation-lane disclosure: break the track's automation out into a sub-lane below.
        const int tid = t->id;
        auto exp = std::make_unique<juce::TextButton> (juce::String (juce::CharPointer_UTF8 (
                        expandedTracks.count (tid) ? "\xe2\x96\xbe" : "\xe2\x96\xb8")));   // ▾ / ▸
        exp->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        exp->setColour (juce::TextButton::textColourOffId, Palette::textDim);
        exp->setTooltip ("Show/hide this track's automation lane");
        exp->onClick = [this, tid, b = exp.get()]
        {
            if (expandedTracks.count (tid)) expandedTracks.erase (tid); else expandedTracks.insert (tid);
            b->setButtonText (juce::String (juce::CharPointer_UTF8 (expandedTracks.count (tid) ? "\xe2\x96\xbe" : "\xe2\x96\xb8")));
            setSize (getWidth(), preferredHeight());   // heights changed — re-layout WITHOUT rebuild (would free this button)
            resized();
            repaint();
        };
        addAndMakeVisible (*exp);
        expandButtons.push_back (std::move (exp));

        // Sub-lane parameter picker (visible only when the track is expanded — see resized()).
        auto pk = std::make_unique<juce::TextButton>();
        pk->setColour (juce::TextButton::buttonColourId, Palette::panelAlt);
        pk->setColour (juce::TextButton::textColourOffId, Palette::text);
        pk->setTooltip ("Choose which parameter this lane automates");
        pk->onClick = [this, tid, b = pk.get()]
        {
            if (! getTrackParams) return;
            auto params = getTrackParams (tid);
            juce::PopupMenu m;
            for (int k = 0; k < (int) params.size(); ++k) m.addItem (k + 1, params[(size_t) k].first);
            if (params.empty()) m.addItem (1, "No automatable parameters", false, false);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (b),
                [this, tid, params] (int r)
                {
                    if (r <= 0 || r > (int) params.size()) return;
                    const auto target = params[(size_t) (r - 1)].second;
                    focusedTarget[tid] = target;
                    if (onPickAutomationParam) onPickAutomationParam (tid, target);
                    resized(); repaint();
                });
        };
        addChildComponent (*pk);
        paramButtons.push_back (std::move (pk));
    }

    setSize (getWidth(), preferredHeight());
    resized();
    refreshAutomation();   // pull automation lanes after a track-set change / project load
    repaint();
}

void ArrangeView::resized()
{
    refreshMeter();
    // rebuild() sizes the per-track header widgets to tracks.size(). A resize can arrive before
    // that sync — e.g. mid project-load, when setViewState reflows for the restored zoom — with
    // the widget vectors still holding the previous project's count. Skip the header layout then
    // rather than index stale vectors out of bounds (rebuild() lays them out once it has synced).
    const size_t n = tracks.size();
    if (soloButtons.size() < n || muteButtons.size() < n || editButtons.size() < n
        || armButtons.size() < n || arpButtons.size() < n || volSliders.size() < n)
        return;
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        const int y = rowTop (i);
        soloButtons[(size_t) i]->setBounds (headerWidth - 62, y + 6, 26, 20);
        muteButtons[(size_t) i]->setBounds (headerWidth - 32, y + 6, 26, 20);
        editButtons[(size_t) i]->setBounds (headerWidth - 58, y + 28, 52, 16);
        armButtons [(size_t) i]->setBounds (headerWidth - 90, y + 6, 26, 20);
        arpButtons [(size_t) i]->setBounds (headerWidth - 34, y + 28, 28, 16);
        if (i < (int) expandButtons.size()) expandButtons[(size_t) i]->setBounds (10, y + th() - 18, 16, 14);
        volSliders [(size_t) i]->setBounds (30, y + th() - 18, headerWidth - 42, 12);

        if (i < (int) paramButtons.size())
        {
            auto* pk = paramButtons[(size_t) i].get();
            if (isExpanded (i))
            {
                pk->setButtonText ("+ Lane");
                pk->setBounds (10, y + th() + 2, 72, pickerRowH - 4);
                pk->setVisible (true);
            }
            else pk->setVisible (false);
        }
    }
}

// ---------------------------------------------------------------------------
// Coordinates
// ---------------------------------------------------------------------------
// The beat span shown across the width: at least ~8 bars, always extended to a whole bar past
// the last clip. The horizontal scale is beats-per-pixel over this span, so a quarter note is
// always the same width regardless of meter — only bar *lines* move with the time signature.
double ArrangeView::spanBeats() const
{
    double maxEnd = 0.0;
    for (auto& t : tracks)
        for (auto& c : t->clips)
            maxEnd = juce::jmax (maxEnd, c.endBeat());
    const double span = juce::jmax (meter.barBeatToBeats (9, 1.0), maxEnd);
    int bar; double bib; meter.beatToBarBeat (span, bar, bib);
    return juce::jmax (1.0, meter.barBeatToBeats (bar + 1, 1.0));   // round up to the next whole bar
}

int ArrangeView::numBars() const { int bar; double bib; meter.beatToBarBeat (spanBeats() - 1.0e-6, bar, bib); return bar; }

float  ArrangeView::fitPixelsPerBeat() const { return (float) (getWidth() - headerWidth) / (float) juce::jmax (1.0, spanBeats()); }
float  ArrangeView::pixelsPerBeat() const { return pxPerBeatStore > 0.0 ? (float) pxPerBeatStore : fitPixelsPerBeat(); }
float  ArrangeView::barWidth() const { return pixelsPerBeat() * (float) juce::jmax (0.001, beatsPerBar); }
float  ArrangeView::xForBeat (double beat) const { return (float) headerWidth + (float) ((beat - viewStartBeat) * (double) pixelsPerBeat()); }
double ArrangeView::beatForX (float x) const { return viewStartBeat + (double) (x - (float) headerWidth) / (double) juce::jmax (1.0e-6f, pixelsPerBeat()); }

// ── Timeline zoom / scroll (Phase 1: horizontal) ─────────────────────────────
// Windowed horizontal view: every beat↔pixel conversion funnels through the two functions
// above, so honouring viewStartBeat + a stored pxPerBeat here makes clips, automation, the
// ruler, hit-testing and drops all zoom/scroll consistently. Zero pxPerBeatStore = fit-to-width.

void ArrangeView::clampView()
{
    const double vis = (double) juce::jmax (1, getWidth() - headerWidth) / (double) juce::jmax (1.0e-6f, pixelsPerBeat());
    viewStartBeat = juce::jlimit (0.0, juce::jmax (0.0, spanBeats() - vis), viewStartBeat);
    if (onViewChanged) onViewChanged();
}

double ArrangeView::getSpanBeats() const { return spanBeats(); }
double ArrangeView::getVisibleBeats() const { return (double) juce::jmax (1, getWidth() - headerWidth) / (double) juce::jmax (1.0e-6f, pixelsPerBeat()); }
void   ArrangeView::setViewStartBeat (double b) { viewStartBeat = juce::jmax (0.0, b); clampView(); repaint(); }

void ArrangeView::zoomHAround (float anchorX, double factor)
{
    const double cur  = (double) pixelsPerBeat();
    const double next = juce::jlimit (0.25, 400.0, cur * factor);          // multiplicative, clamped
    if (std::abs (next - cur) < 1.0e-9) return;
    const double anchorBeat = beatForX (anchorX);                          // beat under the anchor (before)
    pxPerBeatStore = next;
    viewStartBeat  = anchorBeat - (double) (anchorX - (float) headerWidth) / next;   // keep that beat fixed
    zoomToggled = false;
    clampView();
    repaint();
}

void ArrangeView::scrollBeats (double dBeats) { viewStartBeat += dBeats; clampView(); repaint(); }

void ArrangeView::zoomHCentered (double factor)
{
    zoomHAround ((float) headerWidth + (float) (getWidth() - headerWidth) * 0.5f, factor);
}

void ArrangeView::fitWidth()
{
    pxPerBeatStore = 0.0;   // -> derive fit-to-width
    viewStartBeat  = 0.0;
    zoomToggled = false;
    if (onViewChanged) onViewChanged();
    repaint();
}

void ArrangeView::zoomToSelection()
{
    double s = -1.0, e = -1.0;
    {
        GLOOPY_ELOCK(sl);
        if (juce::isPositiveAndBelow (selTrack, (int) tracks.size())
              && juce::isPositiveAndBelow (selClip, (int) tracks[(size_t) selTrack]->clips.size()))
        {
            const auto& c = tracks[(size_t) selTrack]->clips[(size_t) selClip];
            s = c.startBeat.toBeats(); e = c.endBeat();
        }
    }
    if (e <= s) return;                                    // nothing selected -> no-op
    const double pad = juce::jmax (0.5, (e - s) * 0.05);
    const double lenBeats = juce::jmax (0.25, (e - s) + 2.0 * pad);
    pxPerBeatStore = juce::jlimit (0.25, 400.0, (double) juce::jmax (1, getWidth() - headerWidth) / lenBeats);
    viewStartBeat  = juce::jmax (0.0, s - pad);
    clampView();
    repaint();
}

void ArrangeView::zoomToggle()
{
    if (! zoomToggled)
    {
        togglePrevPxPerBeat = pxPerBeatStore; togglePrevStart = viewStartBeat;
        zoomToSelection();
        zoomToggled = true;      // set AFTER zoomToSelection (it clears the flag) so a 2nd press restores
    }
    else
    {
        pxPerBeatStore = togglePrevPxPerBeat; viewStartBeat = togglePrevStart; zoomToggled = false;
        clampView(); repaint();
    }
}

void ArrangeView::fitHeight()
{
    const int n = (int) tracks.size();
    if (n <= 0) return;
    int availH = getHeight();
    if (auto* vp = findParentComponentOfClass<juce::Viewport>()) availH = vp->getMaximumVisibleHeight();
    const double target = (double) juce::jmax (1, availH - rulerHeight) / (double) n;   // per-row target height
    trackHeightScale = juce::jlimit (0.35, 4.0, target / (double) trackHeight);
    setSize (getWidth(), preferredHeight());
    repaint();
}

void ArrangeView::zoomVCentered (double factor)
{
    trackHeightScale = juce::jlimit (0.35, 4.0, trackHeightScale * factor);
    setSize (getWidth(), preferredHeight());
    repaint();
}

void ArrangeView::nudgeWaveAmp (double factor)
{
    waveAmpScale = (float) juce::jlimit (1.0, 16.0, (double) waveAmpScale * factor);
    repaint();
}

void ArrangeView::setViewState (double pxPerBeat, double startBeat, double trackHScale, double waveAmp)
{
    pxPerBeatStore   = juce::jmax (0.0, pxPerBeat);
    viewStartBeat    = juce::jmax (0.0, startBeat);
    trackHeightScale = juce::jlimit (0.35, 4.0, trackHScale > 0.0 ? trackHScale : 1.0);
    waveAmpScale     = (float) juce::jlimit (1.0, 16.0, waveAmp > 0.0 ? waveAmp : 1.0);
    zoomToggled = false;
    // Only reflow when we have real geometry — in headless (width 0, no window) setSize/resized
    // would touch un-laid-out child widgets and crash; the members above are enough to persist.
    if (getWidth() > headerWidth) { clampView(); setSize (getWidth(), preferredHeight()); }
    repaint();
}

void ArrangeView::zoomToMarquee()
{
    const float x0 = juce::jmin (marqueeA.x, marqueeB.x), x1 = juce::jmax (marqueeA.x, marqueeB.x);
    const float y0 = juce::jmin (marqueeA.y, marqueeB.y), y1 = juce::jmax (marqueeA.y, marqueeB.y);
    if (x1 - x0 < 6.0f && y1 - y0 < 6.0f) { repaint(); return; }   // too small: treat as a click, no zoom

    if (x1 - x0 >= 6.0f)                                            // horizontal: frame the beat range
    {
        const double bs = juce::jmax (0.0, beatForX (x0)), be = beatForX (x1);
        if (be > bs + 1.0e-6)
        {
            pxPerBeatStore = juce::jlimit (0.25, 400.0, (double) juce::jmax (1, getWidth() - headerWidth) / (be - bs));
            viewStartBeat  = bs;
        }
    }
    if (y1 - y0 >= 6.0f)                                            // vertical: fill the covered tracks, scroll to them
    {
        const int t0 = trackAtY (y0), t1 = trackAtY (y1);
        if (t0 >= 0 && t1 >= t0)
        {
            const double spanPx = (double) (rowTop (t1) + rowHeight (t1) - rowTop (t0));
            int availH = getHeight();
            if (auto* vp = findParentComponentOfClass<juce::Viewport>()) availH = vp->getMaximumVisibleHeight();
            if (spanPx > 1.0)
                trackHeightScale = juce::jlimit (0.35, 4.0, trackHeightScale * (double) juce::jmax (1, availH - rulerHeight) / spanPx);
            setSize (getWidth(), preferredHeight());
            if (auto* vp = findParentComponentOfClass<juce::Viewport>())
                vp->setViewPosition (vp->getViewPositionX(), juce::jmax (0, rowTop (t0) - rulerHeight));
        }
    }
    zoomToggled = false;
    clampView();
    repaint();
}

void ArrangeView::storeZoomPreset (int i)
{
    if (i >= 1 && i <= 5)
        zoomPresets[(size_t) (i - 1)] = { pxPerBeatStore, viewStartBeat, trackHeightScale, (double) waveAmpScale, true };
}

void ArrangeView::recallZoomPreset (int i)
{
    if (i < 1 || i > 5) return;
    const auto& p = zoomPresets[(size_t) (i - 1)];
    if (p.set) setViewState (p.px, p.start, p.thScale, p.waveAmp);
}

void ArrangeView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const bool cmd = e.mods.isCommandDown(), alt = e.mods.isAltDown(), shift = e.mods.isShiftDown();
    const bool up = w.deltaY >= 0.0f;
    if (cmd && alt)                             // Ctrl/Cmd + Alt: waveform amplitude zoom
        nudgeWaveAmp (up ? 1.2 : 1.0 / 1.2);
    else if (cmd)                               // Ctrl/Cmd: horizontal zoom around the pointer
        zoomHAround (e.position.x, up ? 1.15 : 1.0 / 1.15);
    else if (alt)                               // Alt: vertical (row height) zoom
        zoomVCentered (up ? 1.1 : 1.0 / 1.1);
    else if (shift)                             // Shift: horizontal scroll
    {
        const double vis = (double) juce::jmax (1, getWidth() - headerWidth) / (double) juce::jmax (1.0e-6f, pixelsPerBeat());
        const float  d   = w.deltaY != 0.0f ? w.deltaY : w.deltaX;
        scrollBeats (-(double) d * vis * 0.2);
    }
    else
        Component::mouseWheelMove (e, w);        // plain wheel: let the outer viewport scroll vertically
}

void ArrangeView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (scaleFactor > 0.0f) zoomHAround (e.position.x, (double) scaleFactor);
}

void ArrangeView::refreshMeter()
{
    meter = getMeterMap ? getMeterMap()
                        : gloopy::time::MeterMap (transport.getTimeSigNumerator(), transport.getTimeSigDenominator());
    beatsPerBar = meter.beatsPerBarAt (0.0);
}
int    ArrangeView::trackAtY (float y) const
{
    int top = rulerHeight;
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        const int h = rowHeight (i);
        if (y >= top && y < top + h) return i;
        top += h;
    }
    return -1;
}
double ArrangeView::snapToBar (double beat) const { return meter.snapToBar (beat); }
double ArrangeView::snapToGrid (double beat) const { constexpr double g = 0.25; return std::round (beat / g) * g; }

// Collapsed overlay band: automation drawn/hit-tested inside the clip area.
void ArrangeView::trackBand (int track, float& top, float& bot) const
{
    const float y = (float) rowTop (track);
    top = y + 4.0f; bot = y + (float) th() - 4.0f;
}

// Global autoLanes indices belonging to a track, in draw order (top to bottom of the stack).
void ArrangeView::trackLaneIndices (int track, std::vector<int>& out) const
{
    out.clear();
    if (! juce::isPositiveAndBelow (track, (int) tracks.size())) return;
    const int tid = tracks[(size_t) track]->id;
    for (int i = 0; i < (int) autoLanes.size(); ++i) if (autoLanes[(size_t) i].trackId == tid) out.push_back (i);
}

// Padded band of the k-th stacked sub-lane (below the clip area + the picker strip).
void ArrangeView::laneBand (int track, int k, float& top, float& bot) const
{
    const float base = (float) (rowTop (track) + th() + pickerRowH);
    top = base + (float) (k * laneRowH) + 4.0f;
    bot = base + (float) ((k + 1) * laneRowH) - 4.0f;
}

int ArrangeView::laneAtY (int track, float y) const
{
    if (! isExpanded (track)) return -1;
    const int base = rowTop (track) + th() + pickerRowH;
    if (y < base) return -1;                       // in the clip area or picker strip
    const int k = (int) ((y - base) / laneRowH);
    return juce::isPositiveAndBelow (k, laneCountFor (track)) ? k : -1;
}

int ArrangeView::firstAutoLane (int track) const
{
    if (! juce::isPositiveAndBelow (track, (int) tracks.size())) return -1;
    const int tid = tracks[(size_t) track]->id;
    for (int i = 0; i < (int) autoLanes.size(); ++i) if (autoLanes[(size_t) i].trackId == tid) return i;
    return -1;
}


// Is p within grab range of a breakpoint on this track? Fills laneOut (index into autoLanes) +
// pointOut. Expanded: search each stacked sub-lane in its own band; collapsed: the overlay band.
bool ArrangeView::hitAutoPoint (int track, juce::Point<float> p, int& laneOut, int& pointOut) const
{
    if (! juce::isPositiveAndBelow (track, (int) tracks.size())) return false;
    auto tryLane = [&] (int globalIdx, float top, float bot) -> bool
    {
        const auto& lane = autoLanes[(size_t) globalIdx];
        const float range = juce::jmax (1.0e-6f, lane.hi - lane.lo);
        for (int pi = 0; pi < (int) lane.points.size(); ++pi)
        {
            const float x = xForBeat (lane.points[(size_t) pi].first);
            const float n = juce::jlimit (0.0f, 1.0f, (lane.points[(size_t) pi].second - lane.lo) / range);
            const float y = bot - n * (bot - top);
            if (std::abs (p.x - x) <= 6.0f && std::abs (p.y - y) <= 6.0f) { laneOut = globalIdx; pointOut = pi; return true; }
        }
        return false;
    };

    std::vector<int> idx; trackLaneIndices (track, idx);
    if (isExpanded (track))
    {
        for (int k = 0; k < (int) idx.size(); ++k)
        { float top, bot; laneBand (track, k, top, bot); if (tryLane (idx[(size_t) k], top, bot)) return true; }
        return false;
    }
    float top, bot; trackBand (track, top, bot);
    for (int gi : idx) if (tryLane (gi, top, bot)) return true;
    return false;
}

int ArrangeView::clipAt (int track, juce::Point<float> p) const
{
    if (! juce::isPositiveAndBelow (track, (int) tracks.size()))
        return -1;
    const auto& clips = tracks[(size_t) track]->clips;
    const float y = (float) rowTop (track);
    for (int i = (int) clips.size(); --i >= 0;)
    {
        juce::Rectangle<float> r (xForBeat (clips[(size_t) i].startBeat.toBeats()), y + 2.0f,
                                  xForBeat (clips[(size_t) i].endBeat()) - xForBeat (clips[(size_t) i].startBeat.toBeats()),
                                  (float) th() - 4.0f);
        if (r.contains (p))
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

// Read-only overlay of a track's automation lanes: each lane's breakpoints normalised into the
// row's height (lo at the bottom, hi at the top) and drawn as a warm polyline with point dots.
// Stepped lanes hold-then-jump; ramped lanes interpolate linearly (curve easing comes later).
// One lane's curve normalised into [top,bot] (lo bottom, hi top): warm polyline + breakpoint dots.
void ArrangeView::drawOneLane (juce::Graphics& g, const AutoLaneView& lane, float top, float bot) const
{
    if (lane.points.empty()) return;
    const float xL = (float) headerWidth, xR = (float) getWidth();
    const float range = juce::jmax (1.0e-6f, lane.hi - lane.lo);
    auto yForVal = [&] (float v)
    {
        const float n = juce::jlimit (0.0f, 1.0f, (v - lane.lo) / range);
        return bot - n * (bot - top);
    };

    const float expo = std::pow (2.0f, 2.0f * juce::jlimit (-1.0f, 1.0f, lane.curve));   // matches interpAuto
    juce::Path p;
    p.startNewSubPath (xL, yForVal (lane.points[0].second));   // flat lead-in
    p.lineTo (xForBeat (lane.points[0].first), yForVal (lane.points[0].second));
    for (size_t i = 1; i < lane.points.size(); ++i)
    {
        const float x0 = xForBeat (lane.points[i - 1].first), v0 = lane.points[i - 1].second;
        const float x1 = xForBeat (lane.points[i].first),     v1 = lane.points[i].second;
        if (lane.step) { p.lineTo (x1, yForVal (v0)); p.lineTo (x1, yForVal (v1)); }        // hold, then jump
        else if (lane.curve != 0.0f)
            for (int s = 1; s <= 12; ++s)   // sample the eased ramp
            {
                const float tt = (float) s / 12.0f;
                p.lineTo (x0 + tt * (x1 - x0), yForVal (v0 + std::pow (tt, expo) * (v1 - v0)));
            }
        else p.lineTo (x1, yForVal (v1));   // linear
    }
    p.lineTo (xR, yForVal (lane.points.back().second));    // hold out to the edge

    g.setColour (Palette::warm.withAlpha (0.75f));
    g.strokePath (p, juce::PathStrokeType (1.4f));
    g.setColour (Palette::warm);
    for (const auto& pt : lane.points)
        g.fillEllipse (xForBeat (pt.first) - 2.0f, yForVal (pt.second) - 2.0f, 4.0f, 4.0f);
}

// Collapsed overlay: all of a track's lanes drawn in the same clip-area band.
void ArrangeView::drawAutomation (juce::Graphics& g, int trackId, float top, float bot) const
{
    for (const auto& lane : autoLanes)
        if (lane.trackId == trackId) drawOneLane (g, lane, top, bot);
}

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
            const float a = juce::jmin (wf.getHeight() * 0.5f,
                                        pk[(size_t) idx] * wf.getHeight() * 0.5f * waveAmpScale);   // amplitude zoom, clamped to lane
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
        const double repW = c.contentLenBeats.toBeats() * pixelsPerBeat();

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        for (double repX = r.getX(); repX < r.getRight() - 0.5; repX += repW)
        {
            for (auto& n : c.notes)
            {
                const float nx = (float) (repX + (n.startBeat.toBeats() / c.contentLenBeats.toBeats()) * repW);
                const float nw = juce::jmax (2.0f, (float) ((n.lengthBeats.toBeats() / c.contentLenBeats.toBeats()) * repW));
                const float ny = notesArea.getBottom() - ((n.pitch - lo) / span) * notesArea.getHeight();
                g.fillRect (juce::jlimit (r.getX(), r.getRight() - 1.0f, nx), ny - 1.5f,
                            juce::jmin (nw, r.getRight() - nx), 2.2f);
            }
        }
    }

    // Linked-clip hover: when the pointer is over any clip in this link group, tie the
    // related clips together with a blue tint + outline so they read at a glance.
    const bool linkHover = c.isLinked() && ! hoveredLinkId.isEmpty() && c.linkId == hoveredLinkId;
    if (linkHover)
    {
        g.setColour (juce::Colour (0xff6ab0ff).withAlpha (0.13f));
        g.fillRoundedRectangle (r, 3.0f);
    }
    g.setColour (selected     ? juce::Colours::white
                 : linkHover  ? juce::Colour (0xff6ab0ff)
                              : juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 3.0f, (selected || linkHover) ? 2.0f : 1.0f);

    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    auto titleArea = r.reduced (5.0f, 1.0f).withHeight (13.0f);

    // "Live" script clip: a small badge in the title strip — it re-generates from the kernel
    // a bar ahead of playback, so it's visibly distinct from a frozen/materialised script clip.
    if (c.scriptLive && titleArea.getWidth() > 34.0f)
    {
        auto badge = titleArea.removeFromRight (30.0f);
        g.setColour (juce::Colour (0xff2ec96b));
        g.fillEllipse (badge.getX(), badge.getCentreY() - 2.5f, 5.0f, 5.0f);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText ("LIVE", badge.withTrimmedLeft (7.0f), juce::Justification::centredLeft, false);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (juce::Colours::black.withAlpha (0.85f));
    }
    // Linked/pooled clip: two interlocking rings (a chain) — this clip's pattern is shared
    // with the other clips carrying the same link id, so editing any one updates them all.
    if (c.isLinked() && titleArea.getWidth() > 20.0f)
    {
        auto badge = titleArea.removeFromRight (15.0f);
        const float cy = badge.getCentreY(), x0 = badge.getX() + 1.0f;
        g.setColour (juce::Colour (0xff6ab0ff));
        g.drawEllipse (x0,        cy - 3.0f, 6.0f, 6.0f, 1.3f);
        g.drawEllipse (x0 + 4.0f, cy - 3.0f, 6.0f, 6.0f, 1.3f);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
    }
    g.drawText (c.name.isNotEmpty() ? c.name : t.name, titleArea, juce::Justification::centredLeft, true);
}

void ArrangeView::paint (juce::Graphics& g)
{
    refreshMeter();
    g.fillAll (Palette::inset);
    const double span = spanBeats();
    const auto h = (float) getHeight();

    // Header column background.
    g.setColour (Palette::panel);
    g.fillRect (0, 0, headerWidth, getHeight());

    // Ruler + bar grid. Bar lines come from the meter map, so their spacing changes at each
    // mid-song time-signature change; bars are numbered continuously across the changes.
    g.setColour (Palette::header);
    g.fillRect (0, 0, getWidth(), rulerHeight);
    meter.forEachBarLine (span, [&] (int barNum, double beatAtStart)
    {
        const float x = xForBeat (beatAtStart);
        if (x < (float) headerWidth) return;   // scrolled off the left — don't paint into the header column
        g.setColour (Palette::line);
        g.drawVerticalLine ((int) x, 0.0f, h);
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (barNum), (int) x + 4, 2, 30, rulerHeight - 3,
                    juce::Justification::centredLeft, false);
    });

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

    // Time-signature changes — a label ("6/8") at each change beat on the ruler, in a warm
    // amber to read distinctly from tempo (accent) and named (cyan) flags.
    for (auto& c : meter.changes)
    {
        const float x = xForBeat (c.beat);
        if (x < headerWidth - 1.0f) continue;
        g.setColour (juce::Colour (0xffe6a23c));
        g.drawVerticalLine ((int) x, 0.0f, (float) rulerHeight);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (juce::String (c.num) + "/" + juce::String (c.den),
                    (int) x + 3, 1, 34, 10, juce::Justification::centredLeft, false);
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
        const int y = rowTop (i);
        const int rh = rowHeight (i);

        // Lane background (whole row, incl. any expanded automation sub-lane).
        g.setColour ((i % 2 == 0) ? Palette::inset : Palette::inset.brighter (0.10f));
        g.fillRect (headerWidth, y, getWidth() - headerWidth, rh);

        // Header (whole row).
        g.setColour ((i % 2 == 0) ? Palette::panel : Palette::panelAlt);
        g.fillRect (0, y, headerWidth, rh);
        if (i == selTrack)
        {
            g.setColour (Palette::accent.withAlpha (0.12f));
            g.fillRect (0, y, headerWidth, rh);
        }
        g.setColour (t->colour);
        g.fillRect (0, y + 3, 4, th() - 6);

        // Expanded automation area: a "+ Lane" picker strip, then one stacked sub-lane per param.
        if (isExpanded (i))
        {
            const int exTop = y + th();
            g.setColour (Palette::lineSoft);
            g.drawHorizontalLine (exTop, (float) headerWidth, (float) getWidth());
            g.setColour (Palette::bg.withAlpha (0.22f));
            g.fillRect (headerWidth, exTop, getWidth() - headerWidth, pickerRowH);   // picker strip bg

            std::vector<int> idx; trackLaneIndices (i, idx);
            for (int k = 0; k < (int) idx.size(); ++k)
            {
                const auto& lane = autoLanes[(size_t) idx[(size_t) k]];
                const int bTop = exTop + pickerRowH + k * laneRowH;
                g.setColour (Palette::bg.withAlpha (k % 2 == 0 ? 0.35f : 0.28f));
                g.fillRect (headerWidth, bTop, getWidth() - headerWidth, laneRowH);
                g.setColour (Palette::lineSoft);
                g.drawHorizontalLine (bTop, (float) headerWidth, (float) getWidth());
                // left header: param label + a × remove glyph (hit-tested in mouseDown).
                const juce::String lbl = lane.target.fromFirstOccurrenceOf ("/", false, false)
                                                     .fromFirstOccurrenceOf ("/", false, false);
                g.setColour (Palette::textDim);
                g.setFont (juce::FontOptions (10.5f));
                g.drawText (lbl, 12, bTop + 4, headerWidth - 40, 14, juce::Justification::centredLeft, true);
                g.setFont (juce::FontOptions (13.0f));
                g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")), headerWidth - 22, bTop + 4, 16, 16,
                            juce::Justification::centred, false);
                drawOneLane (g, lane, (float) bTop + 4.0f, (float) (bTop + laneRowH) - 4.0f);
            }
            if (idx.empty())
            {
                g.setColour (Palette::textDim);
                g.setFont (juce::FontOptions (11.0f));
                g.drawText ("use + Lane to automate a parameter", headerWidth + 10, exTop + 3,
                            getWidth() - headerWidth - 20, 16, juce::Justification::centredLeft, false);
            }
        }

        // Live MIDI-input LED: pulses green in the left gutter on the track receiving notes, so
        // you can see the keyboard is connected and which track will sound. Fades after each note.
        if (getMidiActivity)
            if (float act = getMidiActivity (t->id); act > 0.0f)
            {
                g.setColour (juce::Colour (0xff33dd66).withAlpha (juce::jlimit (0.0f, 1.0f, act)));
                g.fillEllipse (5.5f, (float) y + 10.0f, 6.0f, 6.0f);
            }

        // Default: track name over the type label. A hosted instrument with a known patch
        // (e.g. Surge XT) instead shows the patch name over the plugin name — "Distorted Bass"
        // / "Surge XT".
        juce::String title = t->name;
        juce::String subtitle = t->type == TrackType::Instrument ? "INSTRUMENT"
                              : t->type == TrackType::Audio ? "AUDIO" : "MIDI OUT";
        if (t->generator != nullptr && t->generator->uiPatchName.isNotEmpty())
            if (auto* pi = t->generator->getPluginInstance())
            {
                title    = t->generator->uiPatchName;
                subtitle = pi->getName();
            }

        g.setColour (i == selTrack ? Palette::textBright : Palette::text);
        g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        g.drawText (title, 12, y + 4, headerWidth - 70, 18, juce::Justification::centredLeft, true);

        g.setColour (Palette::textDim);
        g.setFont (Palette::sectionFont());
        g.drawText (subtitle, 12, y + 22, headerWidth - 70, 12, juce::Justification::centredLeft, false);

        // Clips + collapsed automation — clipped to the lane's timeline area so a scrolled or
        // partly-offscreen clip never paints over the pinned header column on the left.
        {
            juce::Graphics::ScopedSaveState clipSS (g);
            g.reduceClipRegion (headerWidth, y, getWidth() - headerWidth, rh);
            for (int ci = 0; ci < (int) t->clips.size(); ++ci)
            {
                const auto& c = t->clips[(size_t) ci];
                juce::Rectangle<float> r (xForBeat (c.startBeat.toBeats()), (float) y + 2.0f,
                                          xForBeat (c.endBeat()) - xForBeat (c.startBeat.toBeats()),
                                          (float) th() - 4.0f);
                drawClip (g, *t, c, r, i == selTrack && ci == selClip);
            }
            if (! isExpanded (i)) { float bt, bb; trackBand (i, bt, bb); drawAutomation (g, t->id, bt, bb); }
        }

        g.setColour (Palette::lineSoft);
        g.drawHorizontalLine (y + rh, 0.0f, (float) getWidth());   // row-bottom separator (incl. sub-lane)
    }

    // Content-less bus / group / master rows below the tracks — each carries only automation.
    {
        int by = rowTop ((int) tracks.size());
        for (const auto& br : busRows)
        {
            g.setColour (Palette::inset.darker (0.12f));
            g.fillRect (headerWidth, by, getWidth() - headerWidth, busRowH);
            g.setColour (Palette::panelAlt);
            g.fillRect (0, by, headerWidth, busRowH);
            const juce::Colour c = br.colour.getAlpha() == 0 ? Palette::accentDim : br.colour;
            g.setColour (c); g.fillRect (0, by + 3, 4, busRowH - 6);
            g.setColour (Palette::text);
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText (br.name, 12, by + 4, headerWidth - 20, 16, juce::Justification::centredLeft, true);
            g.setColour (Palette::textDim);
            g.setFont (Palette::sectionFont());
            g.drawText (br.mixerIndex == 0 ? "MASTER" : br.mixerIndex < 0 ? "VCA" : "BUS",
                        12, by + 22, headerWidth - 20, 12, juce::Justification::centredLeft, false);
            for (const auto& lane : br.lanes)
                drawOneLane (g, lane, (float) by + 4.0f, (float) (by + busRowH) - 4.0f);
            g.setColour (Palette::lineSoft);
            g.drawHorizontalLine (by + busRowH, 0.0f, (float) getWidth());
            by += busRowH;
        }
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
            const double viewEnd = spanBeats();
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
    g.setColour (Palette::lineSoft);
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

    // Ctrl-drag marquee zoom box.
    if (marquee)
    {
        const juce::Rectangle<float> box = juce::Rectangle<float>::leftTopRightBottom (
            juce::jmin (marqueeA.x, marqueeB.x), juce::jmin (marqueeA.y, marqueeB.y),
            juce::jmax (marqueeA.x, marqueeB.x), juce::jmax (marqueeA.y, marqueeB.y));
        g.setColour (juce::Colour (0xff6ab0ff).withAlpha (0.15f));
        g.fillRect (box);
        g.setColour (juce::Colour (0xff6ab0ff));
        g.drawRect (box, 1.0f);
    }

    // A browser item is being dragged over the arrangement — invite the drop.
    if (dropHighlight)
    {
        g.setColour (Palette::accent.withAlpha (0.10f));
        g.fillRect (getLocalBounds());
        g.setColour (Palette::accent);
        g.drawRect (getLocalBounds(), 2);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("Drop to add to the project", getLocalBounds().removeFromTop (rulerHeight + 24),
                    juce::Justification::centred, false);
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

        // Is there a time-signature change near this bar (for Remove)?
        double nearTs = -1.0; int nearTsNum = 4, nearTsDen = 4;
        for (auto& c : meter.changes)
            if (std::abs (c.beat - beat) < 1e-6) { nearTs = c.beat; nearTsNum = c.num; nearTsDen = c.den; break; }

        juce::PopupMenu m;
        int hdrBar; double hdrBib; meter.beatToBarBeat (beat, hdrBar, hdrBib);
        m.addSectionHeader ("Bar " + juce::String (hdrBar));
        m.addItem (1, "Add tempo marker...");
        m.addItem (2, "Remove tempo marker", nearBeat >= 0.0);
        m.addSeparator();
        m.addItem (4, "Add marker...");                                   // named timeline marker
        m.addItem (5, "Remove marker" + (nearMarker.isNotEmpty() ? " (" + nearMarker + ")" : juce::String()), nearMarker.isNotEmpty());
        m.addSeparator();
        m.addItem (3, "Initial time signature... (" + juce::String (transport.getTimeSigNumerator())
                        + "/" + juce::String (transport.getTimeSigDenominator()) + ")");
        if (beat > 1.0e-6)
            m.addItem (6, "Add time-signature change here... (bar " + juce::String (hdrBar) + ")");
        if (nearTs >= 0.0)
            m.addItem (7, "Remove time-signature change (" + juce::String (nearTsNum) + "/" + juce::String (nearTsDen) + ")");
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
        m.showMenuAsync (juce::PopupMenu::Options(), [this, beat, nearBeat, nearMarker, nearTs] (int r)
        {
            if (r == 1) promptAddTempoMarker (beat);
            else if (r == 2 && nearBeat >= 0.0 && onRemoveTempoMarker) onRemoveTempoMarker (nearBeat);
            else if (r == 4) promptAddMarker (beat);
            else if (r == 5 && nearMarker.isNotEmpty() && onRemoveMarker) onRemoveMarker (nearMarker);
            else if (r == 3) promptTimeSignature();
            else if (r == 6) promptTimeSigChange (beat);
            else if (r == 7 && nearTs >= 0.0 && onRemoveTimeSigMarker) onRemoveTimeSigMarker (nearTs);
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
                GLOOPY_ELOCK(sl);
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
            {
                juce::PopupMenu inst;   // swap the sound source, keeping clips/routing (patches via the Presets browser)
                inst.addItem (30, "Surge XT");
                inst.addItem (31, "Basic synth");
                m.addSubMenu ("Change instrument", inst);
            }
            m.addItem (8, "Duplicate track");                   // clone clips + generator + inserts
            m.addItem (3, "Move up",   tk > 0);
            m.addItem (4, "Move down", tk < numTracks - 1);
            m.addItem (5, "Invert phase", true, curPolarity);   // checkable polarity flip
            m.addSeparator();
            m.addItem (6, "Export track (WAV)...");             // bounce this track to a stem
            if (sc.isSampler) { m.addSeparator(); m.addItem (2, "Sampler playback window..."); }
            m.addSeparator();
            m.addItem (7, "Delete track");                      // remove the track + its mixer insert (undoable)
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
                else if (r == 7) { if (onRemoveTrack) onRemoveTrack (tk); }   // delete track
                else if (r == 8) { if (onDuplicateTrack) onDuplicateTrack (tk); }   // clone track
                else if (r == 30 || r == 31) { if (onChangeInstrument) onChangeInstrument (tk, r - 30); }   // swap instrument
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
                aw->addTextEditor ("loopxf", juce::String (sc.loopXfade, 3), "Loop crossfade (s)");
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
                juce::StringArray interp { "Linear", "Cubic (smoother)" };
                aw->addComboBox ("interp", interp, "Interpolation");
                aw->getComboBoxComponent ("interp")->setSelectedItemIndex (sc.interp);
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
                        const float lxf = aw->getTextEditorContents ("loopxf").getFloatValue();
                        const int   rt  = aw->getTextEditorContents ("root").getIntValue();
                        const bool  rev = aw->getComboBoxComponent ("dir")->getSelectedItemIndex() == 1;
                        const bool  lp  = aw->getComboBoxComponent ("mode")->getSelectedItemIndex() == 1;
                        const bool  mn  = aw->getComboBoxComponent ("voices")->getSelectedItemIndex() == 1;
                        const int   itp = aw->getComboBoxComponent ("interp")->getSelectedItemIndex();
                        onSetSamplerControls (tk, s, en, rev, rt, fi, fo, lp, mn, lxf, itp);
                    }
                    delete aw;
                }), false);
                }
            });
        }
        return;
    }

    // Automation editing (direct manipulation over the overlay). A precise hit on a breakpoint
    // wins over the clip beneath: single-click-drag moves it, double/right-click deletes it.
    // Alt-click anywhere on a lane adds a point. Anything else falls through to clip editing.
    {
        int li = -1, pi = -1;
        if (hitAutoPoint (track, p, li, pi))
        {
            if (e.getNumberOfClicks() >= 2 || e.mods.isPopupMenu())   // delete this breakpoint
            {
                auto pts = autoLanes[(size_t) li].points;
                pts.erase (pts.begin() + pi);
                if (onSetAutomation) onSetAutomation (autoLanes[(size_t) li].target, pts);
                return;
            }
            dragTrack = track; dragAutoLane = li; dragAutoPoint = pi; drag = Drag::point;   // start moving it
            return;
        }
        const int subTop = rowTop (track) + th();
        if (isExpanded (track) && p.y >= subTop)   // anywhere in the expanded automation area
        {
            const int k = laneAtY (track, p.y);
            std::vector<int> idx; trackLaneIndices (track, idx);
            if (k >= 0 && k < (int) idx.size())
            {
                const auto& lane = autoLanes[(size_t) idx[(size_t) k]];
                const int bTop = subTop + pickerRowH + k * laneRowH;
                if (p.x >= headerWidth - 24 && p.x < headerWidth - 4 && p.y <= bTop + 22)   // × removes the lane
                { if (onSetAutomation) onSetAutomation (lane.target, {}); return; }
                if (e.mods.isPopupMenu())   // lane menu: ramp/step + curve + remove
                {
                    const juce::String target = lane.target; const bool step = lane.step; const float curve = lane.curve;
                    juce::PopupMenu m;
                    m.addItem (1, "Smooth (ramp)",  true, ! step);
                    m.addItem (2, "Stepped (hold)", true, step);
                    juce::PopupMenu cs;
                    cs.addItem (10, "Linear",   true, std::abs (curve) < 0.05f);
                    cs.addItem (11, "Ease out", true, curve < -0.05f);
                    cs.addItem (12, "Ease in",  true, curve >  0.05f);
                    m.addSubMenu ("Curve", cs, ! step);
                    m.addSeparator();
                    m.addItem (3, "Remove lane");
                    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (
                                         { e.getScreenPosition().x, e.getScreenPosition().y, 1, 1 }),
                        [this, target] (int r)
                        {
                            if      (r == 1  && onSetAutomationStep)  onSetAutomationStep  (target, false);
                            else if (r == 2  && onSetAutomationStep)  onSetAutomationStep  (target, true);
                            else if (r == 10 && onSetAutomationCurve) onSetAutomationCurve (target, 0.0f);
                            else if (r == 11 && onSetAutomationCurve) onSetAutomationCurve (target, -0.6f);
                            else if (r == 12 && onSetAutomationCurve) onSetAutomationCurve (target, 0.6f);
                            else if (r == 3  && onSetAutomation)      onSetAutomation (target, {});
                        });
                    return;
                }
                if (e.mods.isAltDown())   // add a point to this sub-lane
                {
                    float top, bot; laneBand (track, k, top, bot);
                    const float n = juce::jlimit (0.0f, 1.0f, (bot - p.y) / juce::jmax (1.0f, bot - top));
                    const double beat = juce::jmax (0.0, snapToGrid (beatForX (p.x)));
                    if (onAddAutomationPoint) onAddAutomationPoint (lane.target, beat, lane.lo + n * (lane.hi - lane.lo));
                    return;
                }
            }
            return;   // consume any other click in the sub-lane area (never create a clip here)
        }
        if (e.mods.isAltDown())   // collapsed: Alt-click adds to the track's first lane
        {
            const int la = firstAutoLane (track);
            if (la >= 0)
            {
                float top, bot; trackBand (track, top, bot);
                const auto& lane = autoLanes[(size_t) la];
                const float n = juce::jlimit (0.0f, 1.0f, (bot - p.y) / juce::jmax (1.0f, bot - top));
                const double beat = juce::jmax (0.0, snapToGrid (beatForX (p.x)));
                if (onAddAutomationPoint) onAddAutomationPoint (lane.target, beat, lane.lo + n * (lane.hi - lane.lo));
                return;
            }
        }
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
        bool isScript = false, isScriptLive = false, isLinked = false;
        double clipStart = 0.0, clipEnd = 0.0;
        {
            GLOOPY_ELOCK(sl);
            if (juce::isPositiveAndBelow (hit, (int) tracks[(size_t) track]->clips.size()))
            {
                const auto& cl = tracks[(size_t) track]->clips[(size_t) hit];
                isMidi = ! cl.isAudio();
                isTake = cl.takeId.isNotEmpty();
                isMuted = cl.muted;
                isMutedTake = isTake && cl.muted;
                isLoopedMidi = isMidi && cl.looped && cl.contentLenBeats > 0.0
                               && cl.contentLenBeats < cl.lengthBeats - 1.0e-9;   // actually tiles
                isScript = cl.isScript();
                isScriptLive = cl.scriptLive;
                isLinked = cl.isLinked();
                clipStart = cl.startBeat.toBeats();
                clipEnd   = (cl.startBeat + cl.lengthBeats).toBeats();
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
        {
            juce::PopupMenu se;                             // chop the clip into N equal pieces
            se.addItem (830, "2");
            se.addItem (831, "4");
            se.addItem (832, "8");
            se.addItem (833, "16");
            m.addSubMenu ("Split into", se);
        }
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
        m.addItem (26, "Duplicate linked", isMidi);      // the copy shares this clip's pattern (edit one -> both)
        m.addItem (27, "Make unique", isLinked);         // detach a linked clip into its own pattern
        m.addItem (3, "Reverse");
        m.addItem (4, "Snap to scale", isMidi);
        m.addItem (13, "Crop to loop region", transport.isLoopEnabled());   // MIDI notes or audio buffer
        m.addItem (14, "Consolidate loops", isLoopedMidi);   // bake looped repetitions into notes
        m.addItem (15, "Bounce to audio");                   // freeze clip -> audio on a new track
        m.addItem (17, "Mute clip", ! isTake, isMuted);      // disable/enable in the arrangement (takes use Use/Promote)
        m.addItem (18, "Loop this clip");                    // set the transport loop to this clip's span
        m.addItem (19, "Copy notes (JSON)", isMidi);         // notes -> system clipboard as JSON
        m.addItem (21, "Generate from script", isMidi);      // run the clip's script (kernel) -> notes
        m.addItem (22, "Edit script code...", isMidi);       // open the clip's source in $EDITOR
        m.addItem (25, "Set script generator...", isMidi);   // reference a named generator (pkg:sym) in the project's system/module
        m.addItem (24, "Live (auto-generate on playback)", isScript, isScriptLive);  // re-run the script ~1 bar ahead
        m.addItem (23, "Live-drive from script", isMidi);    // play the script live during playback (ephemeral)
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
            if (r == 21) { if (onClipCommand) onClipCommand (t, c, "regenerate"); return; }   // run the clip's script
            if (r == 22) { if (onClipCommand) onClipCommand (t, c, "editcode"); return; }     // edit the clip's source
            if (r == 25)   // Set script generator: reference a named generator in the project's system/module
            {
                juce::String curGen, curSys, curLang;
                { GLOOPY_ELOCK(sl);
                  if (juce::isPositiveAndBelow (t, (int) tracks.size())
                      && juce::isPositiveAndBelow (c, (int) tracks[(size_t) t]->clips.size()))
                  { auto& cl = tracks[(size_t) t]->clips[(size_t) c];
                    curGen = cl.scriptGenerator; curSys = cl.scriptSystem; curLang = cl.scriptLang; } }
                if (curLang.isEmpty()) curLang = "common-lisp";
                auto* gw = new juce::AlertWindow ("Script generator",
                    "Name a generator in the project's system/module.\n"
                    "Lisp: pkg:sym (system loaded via ASDF).   Python: pkg.mod:fn.",
                    juce::MessageBoxIconType::NoIcon);
                gw->addTextEditor ("generator", curGen, "Generator (pkg:sym)");
                gw->addTextEditor ("system", curSys, "System / import root (optional)");
                gw->addTextEditor ("lang", curLang, "Language");
                gw->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
                gw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                gw->enterModalState (true, juce::ModalCallbackFunction::create ([this, gw, t, c] (int rr)
                {
                    if (rr == 1 && onSetGenerator)
                        onSetGenerator (t, c, gw->getTextEditorContents ("generator").trim(),
                                        gw->getTextEditorContents ("system").trim(),
                                        gw->getTextEditorContents ("lang").trim());
                    delete gw;
                }));
                return;
            }
            if (r == 24) { if (onClipCommand) onClipCommand (t, c, "livetoggle"); return; }   // auto-generate ahead of playback
            if (r == 23) { if (onClipCommand) onClipCommand (t, c, "drive"); return; }        // live-drive the clip
            if (r == 20)   // Rename clip: prompt (prefilled with the clip's current name)
            {
                juce::String cur;
                { GLOOPY_ELOCK(sl);
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
            if (r >= 830 && r <= 833)   // Split into: 2 / 4 / 8 / 16 equal pieces
            {
                const int pieces = r == 830 ? 2 : r == 831 ? 4 : r == 832 ? 8 : 16;
                if (onClipCommand) onClipCommand (t, c, "spliteq:" + juce::String (pieces));
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
                            : r == 26 ? "duplicatelinked"
                            : r == 27 ? "makeunique"
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

    // Ctrl/Cmd-drag a box -> marquee zoom (H+V). Preempts clip drag/delete.
    if (e.mods.isCommandDown() && p.x >= (float) headerWidth && p.y >= (float) rulerHeight)
    {
        marquee = true; marqueeA = marqueeB = p; drag = Drag::none;
        return;
    }

    // Double-click a clip still deletes it directly (fast removal gesture).
    if (hit >= 0 && e.getNumberOfClicks() >= 2)
    {
        {
            GLOOPY_ELOCK(sl);
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
        else { drag = Drag::move; dragBeatOffset = beatForX (p.x) - c.startBeat.toBeats(); }
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
        // Land the clip in the bar the cursor is IN — floor, not snap-to-nearest, so a click in
        // the second half of a bar doesn't jump the new clip into the next bar (feels misplaced).
        const double sb  = juce::jmax (0.0, meter.barStart (juce::jmax (0.0, beatForX (p.x))));
        const double blen = meter.beatsPerBarAt (sb);   // one bar in the meter at that point
        c.startBeat      = sb;
        c.lengthBeats    = blen;
        c.contentLenBeats = blen;
        c.looped         = true;
        {
            GLOOPY_ELOCK(sl);
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

void ArrangeView::promptTimeSigChange (double beat)
{
    int barNo; double bib; meter.beatToBarBeat (beat, barNo, bib);
    int curNum, curDen; meter.signatureAt (beat, curNum, curDen);   // seed with the meter in effect here
    auto* aw = new juce::AlertWindow ("Time-signature change",
                                      "New signature from bar " + juce::String (barNo) + ":",
                                      juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("num", juce::String (curNum), "Numerator");
    aw->addTextEditor ("den", juce::String (curDen), "Denominator");
    aw->addButton ("Set",    1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, beat] (int r)
    {
        if (r == 1 && onAddTimeSigMarker)
        {
            const int n = aw->getTextEditorContents ("num").getIntValue();
            const int d = aw->getTextEditorContents ("den").getIntValue();
            if (n >= 1 && d >= 1) onAddTimeSigMarker (beat, n, d);
        }
        delete aw;
    }), false);
}

void ArrangeView::promptAddTempoMarker (double beat)
{
    auto* aw = new juce::AlertWindow ("Tempo marker",
                                      "BPM at bar " + juce::String ([&]{ int bn; double bb; meter.beatToBarBeat (beat, bn, bb); return bn; }()),
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
                                      "Name for the marker at bar " + juce::String ([&]{ int bn; double bb; meter.beatToBarBeat (beat, bn, bb); return bn; }()),
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

// Hovering a linked clip highlights its whole group (across tracks). Cheap: resolve the clip
// under the pointer, remember its link id, and only repaint when that id changes.
void ArrangeView::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    juce::String link;
    if (p.x >= headerWidth && p.y >= rulerHeight)
        if (const int track = trackAtY (p.y); track >= 0)
            if (const int hit = clipAt (track, p); hit >= 0)
            {
                GLOOPY_ELOCK(sl);
                if (juce::isPositiveAndBelow (track, (int) tracks.size())
                      && juce::isPositiveAndBelow (hit, (int) tracks[(size_t) track]->clips.size()))
                    link = tracks[(size_t) track]->clips[(size_t) hit].linkId;
            }
    if (link != hoveredLinkId) { hoveredLinkId = link; repaint(); }
}

void ArrangeView::mouseExit (const juce::MouseEvent&)
{
    if (hoveredLinkId.isNotEmpty()) { hoveredLinkId.clear(); repaint(); }
}

void ArrangeView::mouseDrag (const juce::MouseEvent& e)
{
    if (marquee) { marqueeB = e.position; repaint(); return; }
    if (rulerDrag)
    {
        const double b = juce::jmax (0.0, beatForX (e.position.x));
        if (std::abs (b - rulerStartBeat) > 0.5)   // became a range
        {
            double s = snapToBar (juce::jmin (rulerStartBeat, b));
            double f = snapToBar (juce::jmax (rulerStartBeat, b));
            const double oneBar = meter.beatsPerBarAt (s);
            if (f - s < oneBar) f = s + oneBar;
            if (rulerAlt)                          // Alt → punch region
            {
                if (onSetPunchRange) onSetPunchRange (true, s, f);
            }
            else                                   // plain → loop region
            {
                loopDragged = true;
                transport.setLoopRegion (gloopy::time::BeatPosition { s }, gloopy::time::BeatPosition { f });
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

    // Dragging an automation breakpoint: update the cached lane locally (value from y clamped to
    // the param range, beat grid-snapped and kept between its neighbours) and repaint. The model
    // is committed once on mouseUp — so a drag is one undo step, not one per mouse-move.
    if (drag == Drag::point)
    {
        if (dragAutoLane >= 0 && dragAutoLane < (int) autoLanes.size())
        {
            auto& lane = autoLanes[(size_t) dragAutoLane];
            if (dragAutoPoint >= 0 && dragAutoPoint < (int) lane.points.size())
            {
                float top, bot;
                if (isExpanded (dragTrack))   // sub-lane band of the dragged lane
                {
                    std::vector<int> idx; trackLaneIndices (dragTrack, idx);
                    int k = -1; for (int j = 0; j < (int) idx.size(); ++j) if (idx[(size_t) j] == dragAutoLane) { k = j; break; }
                    if (k >= 0) laneBand (dragTrack, k, top, bot); else trackBand (dragTrack, top, bot);
                }
                else trackBand (dragTrack, top, bot);
                const float n   = juce::jlimit (0.0f, 1.0f, (bot - e.position.y) / juce::jmax (1.0f, bot - top));
                const float val = lane.lo + n * (lane.hi - lane.lo);
                double beat = juce::jmax (0.0, snapToGrid (beatForX (e.position.x)));
                const double minB = (dragAutoPoint > 0) ? lane.points[(size_t) dragAutoPoint - 1].first + 1.0e-3 : 0.0;
                const double maxB = (dragAutoPoint + 1 < (int) lane.points.size())
                                        ? lane.points[(size_t) dragAutoPoint + 1].first - 1.0e-3 : 1.0e12;
                beat = juce::jlimit (minB, maxB, beat);
                lane.points[(size_t) dragAutoPoint] = { beat, val };
                repaint();
            }
        }
        return;
    }

    if (dragTrack < 0 || dragClip < 0)
        return;
    {
        GLOOPY_ELOCK(sl);
        if (! juce::isPositiveAndBelow (dragTrack, (int) tracks.size())) return;
        auto& clips = tracks[(size_t) dragTrack]->clips;
        if (! juce::isPositiveAndBelow (dragClip, (int) clips.size())) return;
        auto& c = clips[(size_t) dragClip];

        if (drag == Drag::move)
            c.startBeat = juce::jmax (0.0, snapToBar (beatForX (e.position.x) - dragBeatOffset));
        else if (drag == Drag::resize)
            c.lengthBeats = juce::jmax (meter.beatsPerBarAt (c.startBeat.toBeats()),
                                        snapToBar (beatForX (e.position.x)) - c.startBeat.toBeats());
    }
    if (onChanged) onChanged();
    repaint();
}

void ArrangeView::mouseUp (const juce::MouseEvent& e)
{
    if (marquee) { marquee = false; zoomToMarquee(); return; }
    // Commit a finished breakpoint drag as one undo step (only if it actually moved).
    if (drag == Drag::point && e.mouseWasDraggedSinceMouseDown()
          && dragAutoLane >= 0 && dragAutoLane < (int) autoLanes.size())
        if (onSetAutomation) onSetAutomation (autoLanes[(size_t) dragAutoLane].target,
                                              autoLanes[(size_t) dragAutoLane].points);
    drag = Drag::none;
    dragTrack = dragClip = -1;
    dragAutoLane = dragAutoPoint = -1;
    rulerDrag = false;
    rulerAlt = false;
}

void ArrangeView::timerCallback()
{
    // Repaint so the playhead tracks both playback and manual seeks.
    repaint();
    // Detect the play->stop edge to end an automation-write pass.
    const bool playing = transport.isPlaying();
    if (wasPlaying && ! playing && onPlaybackStopped) onPlaybackStopped();
    wasPlaying = playing;
}
