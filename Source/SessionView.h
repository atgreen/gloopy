// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <functional>
#include <cmath>
#include "Track.h"
#include "SessionModel.h"
#include "SessionLauncher.h"
#include "Transport.h"

// Shared layout constants + small drawing helpers for the session view. The session view is split
// into a scrolling track grid (SessionView) and a frozen scene/master column (SessionSceneColumn),
// composed by SessionPane so the scene launchers + master strip stay pinned to the LEFT while the
// tracks scroll horizontally (Ableton pins them right; we pin left so they sit next to track 1).
namespace sv
{
    constexpr int kPad = 8, kHeaderH = 30, kRowH = 30, kFooterH = 30, kTrackW = 128, kSceneW = 150, kMixerH = 150;

    inline int contentHeight (int numScenes) { return kHeaderH + numScenes * kRowH + kFooterH + kMixerH + 3 * kPad; }

    // A VU bar: background, level fill colored by zone (green -> yellow -> red), and a held-peak
    // marker line. `lvl` and `peak` are linear magnitudes (1.0 = 0 dBFS).
    inline void drawMeter (juce::Graphics& g, juce::Rectangle<int> bar, float lvl, float peak)
    {
        g.setColour (juce::Colour (0xff141417));
        g.fillRect (bar);
        const auto zone = [&] (float lo, float hi, juce::Colour c)
        {
            const float top = juce::jmin (juce::jlimit (0.0f, 1.0f, lvl), hi);
            if (top > lo)
            {
                auto seg = bar.toFloat().withTop  (bar.getY() + bar.getHeight() * (1.0f - top))
                                        .withBottom (bar.getY() + bar.getHeight() * (1.0f - lo));
                g.setColour (c);
                g.fillRect (seg);
            }
        };
        zone (0.0f,  0.55f, juce::Colour (0xff2ec26a));   // green  (up to ~ -5 dB)
        zone (0.55f, 0.80f, juce::Colour (0xffd8c341));   // yellow (~ -5..-2 dB)
        zone (0.80f, 1.0f,  juce::Colour (0xffe0553a));   // red    (hot / clipping)
        if (peak > 0.02f)                                 // held-peak marker
        {
            const float y = bar.getY() + bar.getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, peak));
            g.setColour (peak >= 0.99f ? juce::Colour (0xffff5544) : juce::Colours::white.withAlpha (0.9f));
            g.fillRect ((float) bar.getX(), y - 0.5f, (float) bar.getWidth(), 1.6f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRect (bar, 1);
    }
    inline float holdPeak (float cur, float held) { return cur > held ? cur : juce::jmax (0.0f, held - 0.012f); }

    inline juce::String quantumLabel (double b)
    {
        if (b <= 0.0)   return "Off";
        if (b <= 0.26)  return "1/16";
        if (b <= 0.51)  return "1/8";
        if (b <= 1.01)  return "1/4";
        if (b <= 2.01)  return "1/2";
        if (b <= 4.01)  return "1 bar";
        if (b <= 8.01)  return "2 bars";
        return juce::String (b, 1) + "bt";
    }
}

/** Session track grid — columns = tracks, rows = global scenes, with a per-column mini-mixer strip
    at the bottom (pan / volume fader / VU meter / solo / mute / arm / FX). This is the scrolling
    part; the scene-launch column and master strip live in SessionSceneColumn (pinned left).

    Simplified, color-coded cells: each is a track-colored bar for a launchable looping clip.
    Left-click a clip to launch it (an empty cell to stop that track); right-click a cell for a
    menu (new empty clip / copy the selected clip here / edit / clear). Playing cells glow; queued
    cells pulse green. The clip grid is custom-drawn; the mixer controls are real JUCE child
    components (they write the Track atomics directly). Repaints at 30 Hz. */
class SessionView : public juce::Component,
                    private juce::Timer
{
public:
    SessionView (std::vector<std::unique_ptr<Track>>& tracksRef,
                 std::vector<Scene>& scenesRef,
                 SessionLauncher& launcherRef,
                 Transport& transportRef,
                 juce::CriticalSection& engineLockRef)
        : tracks (tracksRef), scenes (scenesRef), launcher (launcherRef),
          transport (transportRef), engineLock (engineLockRef)
    {
        startTimerHz (30);
    }
    ~SessionView() override { stopTimer(); }

    // Owner callbacks -> api* methods.
    std::function<void (int, int)> onLaunchClip;      // (track index, scene)
    std::function<void (int, int)> onEmptyCell;       // (track index, scene) -> stop track / record into slot
    std::function<void (int, bool)> onArm;            // (track index, armed) -> record-enable the track
    std::function<bool (int)>      isArmed;           // (track index) -> record-armed?
    std::function<int (int)>       getRecordingScene; // (track index) -> scene being recorded, or -1
    std::function<void (int, int)> onNewClip;         // (track index, scene) -> empty MIDI clip
    std::function<void (int, int)> onCopySelectedClip;// (track index, scene) -> copy the selected arrangement clip
    std::function<void (int, int)> onClearSlot;       // (track index, scene)
    std::function<void (int, int)> onEditClip;        // (track index, scene) -> load into the piano-roll editor
    std::function<void (int, int)> onCopyToArrangement;   // (track index, scene) -> drop a copy onto the timeline
    std::function<void (int, float&, float&)> getTrackLevels;   // (track index) -> L,R peak for the stereo VU
    std::function<void (int)>      onOpenTrackFx;     // (track index) -> open that track's effects (mixer view)

    void rebuild()
    {
        const int nt = (int) tracks.size();
        strips.clear();
        for (int t = 0; t < nt; ++t)
        {
            Track* tr = tracks[(size_t) t].get();
            Strip s;
            s.vol = std::make_unique<juce::Slider> (juce::Slider::LinearVertical, juce::Slider::NoTextBox);
            s.vol->setRange (0.0, 1.0, 0.0);
            s.vol->setValue (tr->volume.load(), juce::dontSendNotification);
            s.vol->onValueChange = [tr, w = s.vol.get()] { tr->volume.store ((float) w->getValue()); };

            s.pan = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            s.pan->setRange (-1.0, 1.0, 0.0);
            s.pan->setValue (tr->pan.load(), juce::dontSendNotification);
            s.pan->onValueChange = [tr, w = s.pan.get()] { tr->pan.store ((float) w->getValue()); };

            s.solo = std::make_unique<juce::TextButton> ("S");
            s.solo->setClickingTogglesState (true);
            s.solo->setToggleState (tr->solo.load(), juce::dontSendNotification);
            s.solo->onClick = [tr, w = s.solo.get()] { tr->solo.store (w->getToggleState()); };

            s.mute = std::make_unique<juce::TextButton> ("M");
            s.mute->setClickingTogglesState (true);
            s.mute->setToggleState (tr->mute.load(), juce::dontSendNotification);
            s.mute->onClick = [tr, w = s.mute.get()] { tr->mute.store (w->getToggleState()); };

            s.arm = std::make_unique<juce::TextButton> (juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f")));   // ●
            s.arm->setClickingTogglesState (true);
            s.arm->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffcc3333));
            s.arm->setToggleState (isArmed && isArmed (t), juce::dontSendNotification);
            s.arm->onClick = [this, t, w = s.arm.get()] { if (onArm) onArm (t, w->getToggleState()); };

            s.fx = std::make_unique<juce::TextButton> ("FX");
            s.fx->onClick = [this, t] { if (onOpenTrackFx) onOpenTrackFx (t); };

            addAndMakeVisible (*s.vol);  addAndMakeVisible (*s.pan);
            addAndMakeVisible (*s.solo); addAndMakeVisible (*s.mute); addAndMakeVisible (*s.arm); addAndMakeVisible (*s.fx);
            strips.push_back (std::move (s));
        }
        meterL.assign ((size_t) nt, 0.0f);
        meterR.assign ((size_t) nt, 0.0f);
        peakHoldL.assign ((size_t) nt, 0.0f);   // keep held-peak buffers in lockstep with meterL/R,
        peakHoldR.assign ((size_t) nt, 0.0f);   // else timerCallback indexes them out of bounds (crash)
        meterRect.assign ((size_t) nt, {});

        updateSize();
        resized();
        repaint();
    }

    int preferredWidth()  const { return (int) tracks.size() * sv::kTrackW + 2 * sv::kPad; }
    int preferredHeight() const { return sv::contentHeight ((int) scenes.size()); }

    void resized() override
    {
        const int nt = juce::jmin ((int) tracks.size(), (int) strips.size());
        for (int t = 0; t < nt; ++t)
        {
            auto r = mixerStripRect (t).reduced (6, 6);
            r.removeFromTop (14);                              // dB readout (drawn in paint)
            strips[(size_t) t].pan->setBounds (r.removeFromTop (30).withSizeKeepingCentre (30, 30));   // rotary pan
            r.removeFromTop (2);
            auto btn = r.removeFromBottom (18);                // S | M | ● | FX
            r.removeFromBottom (4);
            auto fader = r.removeFromLeft ((int) (r.getWidth() * 0.48f));
            r.removeFromLeft (4);
            meterRect[(size_t) t] = r;                         // stereo VU (two bars, drawn in paint)
            strips[(size_t) t].vol->setBounds (fader);
            const int bw = (btn.getWidth() - 9) / 4;         // S | M | ● | FX
            strips[(size_t) t].solo->setBounds (btn.removeFromLeft (bw)); btn.removeFromLeft (3);
            strips[(size_t) t].mute->setBounds (btn.removeFromLeft (bw)); btn.removeFromLeft (3);
            strips[(size_t) t].arm ->setBounds (btn.removeFromLeft (bw)); btn.removeFromLeft (3);
            strips[(size_t) t].fx  ->setBounds (btn);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1b1b1f));
        const int nt = (int) tracks.size();
        const int ns = (int) scenes.size();

        std::vector<int> playing ((size_t) nt, SessionLauncher::kArrangement), pending ((size_t) nt, SessionLauncher::kNone);
        std::vector<juce::Colour> trackCol ((size_t) nt, juce::Colour (0xff6a6a72));
        std::vector<juce::String> trackName ((size_t) nt);
        std::vector<float> vol ((size_t) nt, 0.8f);
        {
            const juce::ScopedLock sl (engineLock);
            for (int t = 0; t < nt; ++t)
            {
                playing[(size_t) t]   = launcher.playingSlot (t);
                pending[(size_t) t]   = launcher.pendingSlot (t);
                trackCol[(size_t) t]  = tracks[(size_t) t]->colour;
                trackName[(size_t) t] = tracks[(size_t) t]->name;
                vol[(size_t) t]       = tracks[(size_t) t]->volume.load();
            }
        }

        // Faint full-height column dividers so the empty middle reads as columns.
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        for (int t = 1; t < nt; ++t)
            g.drawVerticalLine (sv::kPad + t * sv::kTrackW, (float) sv::kPad, (float) (getHeight() - sv::kPad));

        // Track header row.
        for (int t = 0; t < nt; ++t)
        {
            auto h = trackHeaderRect (t);
            g.setColour (trackCol[(size_t) t].withAlpha (0.85f));
            g.fillRect (h.removeFromTop (4.0f));
            g.setColour (juce::Colour (0xff2a2a30));
            g.fillRect (h);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (trackName[(size_t) t], h.reduced (6, 2), juce::Justification::centredLeft, true);
        }

        // Grid cells.
        for (int s = 0; s < ns; ++s)
            for (int t = 0; t < nt; ++t)
            {
                auto r = cellRect (t, s).reduced (2.0f);
                const bool has = hasClip (t, s);
                const bool isPlaying = playing[(size_t) t] == s;
                const bool isPending = pending[(size_t) t] == s;
                auto base = trackCol[(size_t) t];

                if (has)
                {
                    g.setColour (base.withAlpha (isPlaying ? 0.95f : 0.42f));
                    g.fillRoundedRectangle (r, 3.0f);
                    g.setColour (juce::Colours::black.withAlpha (isPlaying ? 0.85f : 0.6f));
                    juce::Path tri; auto tb = r.withWidth (14.0f).reduced (4.0f).translated (2.0f, 0.0f);
                    tri.addTriangle (tb.getX(), tb.getY(), tb.getX(), tb.getBottom(), tb.getRight(), tb.getCentreY());
                    g.fillPath (tri);
                    g.setColour (juce::Colours::white.withAlpha (isPlaying ? 0.95f : 0.75f));
                    g.setFont (juce::FontOptions (10.5f, isPlaying ? juce::Font::bold : juce::Font::plain));
                    g.drawText (clipName (t, s), r.withTrimmedLeft (18.0f).reduced (2, 0), juce::Justification::centredLeft, true);
                }
                else if (getRecordingScene && getRecordingScene (t) == s)   // recording into this slot
                {
                    g.setColour (juce::Colour (0xff5a1f1f).withAlpha (blinkOn ? 0.95f : 0.6f));
                    g.fillRoundedRectangle (r, 3.0f);
                    g.setColour (juce::Colour (0xffe04040));
                    g.fillEllipse (r.withSizeKeepingCentre (10.0f, 10.0f));
                }
                else
                {
                    g.setColour (juce::Colour (0xff242429));
                    g.fillRoundedRectangle (r, 3.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.12f));
                    g.drawRect (r.withSizeKeepingCentre (9.0f, 9.0f), 1.4f);
                }
                if (isPending)   // queued to launch at the next quantum boundary — pulse green
                {
                    g.setColour (juce::Colour (0xff33dd66).withAlpha (blinkOn ? 0.35f : 0.10f));
                    g.fillRoundedRectangle (r, 3.0f);
                    g.setColour (juce::Colour (0xff33dd66).withAlpha (blinkOn ? 1.0f : 0.4f));
                    g.drawRoundedRectangle (r, 3.0f, 2.0f);
                }
            }

        // Per-column mini-mixer strips (bottom band): background, dB readout, VU meter. The
        // pan/fader/solo/mute are child components drawn on top.
        for (int t = 0; t < nt && t < (int) meterRect.size(); ++t)
        {
            auto strip = mixerStripRect (t);
            g.setColour (juce::Colour (0xff232329));
            g.fillRoundedRectangle (strip.toFloat().reduced (3.0f), 4.0f);

            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.setFont (juce::FontOptions (9.5f));
            const float v = vol[(size_t) t];
            const juce::String db = v <= 0.0001f ? juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e"))
                                                 : juce::String (20.0f * std::log10 (v), 1) + " dB";
            g.drawText (db, strip.reduced (8, 4).removeFromTop (14).toFloat(), juce::Justification::centredLeft, false);

            // Stereo VU meter (routed mixer-track peakL/R): zone-colored fill + held-peak marker.
            auto m = meterRect[(size_t) t];
            if (! m.isEmpty())
            {
                const int bw = (m.getWidth() - 2) / 2;
                sv::drawMeter (g, m.withWidth (bw), meterL[(size_t) t], peakHoldL[(size_t) t]);
                sv::drawMeter (g, m.withX (m.getRight() - bw).withWidth (bw), meterR[(size_t) t], peakHoldR[(size_t) t]);
            }
        }

        if (nt == 0 || ns == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (nt == 0 ? "Add an instrument track to start a session grid"
                                : "Right-click a cell to add a clip, then click it to launch",
                        juce::Rectangle<int> (sv::kPad, sv::kHeaderH + 16, juce::jmax (200, getWidth() - 2 * sv::kPad), 24),
                        juce::Justification::centredLeft, true);
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int nt = (int) tracks.size(), ns = (int) scenes.size();
        const auto p = e.position;
        for (int s = 0; s < ns; ++s)
            for (int t = 0; t < nt; ++t)
                if (cellRect (t, s).contains (p) && hasClip (t, s)) { if (onEditClip) onEditClip (t, s); return; }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int nt = (int) tracks.size(), ns = (int) scenes.size();
        const auto p = e.position;
        for (int s = 0; s < ns; ++s)
            for (int t = 0; t < nt; ++t)
                if (cellRect (t, s).contains (p))
                {
                    if (e.mods.isPopupMenu()) { cellMenu (t, s); return; }
                    if (hasClip (t, s)) { if (onLaunchClip) onLaunchClip (t, s); }
                    else                { if (onEmptyCell)  onEmptyCell (t, s); }   // stop track / record into slot
                    return;
                }
    }

private:
    struct Strip
    {
        std::unique_ptr<juce::Slider> vol, pan;
        std::unique_ptr<juce::TextButton> solo, mute, arm, fx;
    };

    juce::Rectangle<float> trackHeaderRect (int t) const
    { return { (float) (sv::kPad + t * sv::kTrackW), (float) sv::kPad, (float) sv::kTrackW, (float) sv::kHeaderH }; }
    juce::Rectangle<float> cellRect (int t, int s) const
    { return { (float) (sv::kPad + t * sv::kTrackW), (float) (sv::kPad + sv::kHeaderH + s * sv::kRowH), (float) sv::kTrackW, (float) sv::kRowH }; }
    juce::Rectangle<int> mixerStripRect (int t) const
    { return { sv::kPad + t * sv::kTrackW, juce::jmax (sv::kPad, getHeight() - sv::kPad - sv::kMixerH), sv::kTrackW, sv::kMixerH }; }

    bool hasClip (int t, int s) const
    {
        const juce::ScopedLock sl (engineLock);
        return t >= 0 && t < (int) tracks.size() && slotClip (tracks[(size_t) t]->sessionSlots, s) != nullptr;
    }
    juce::String clipName (int t, int s) const
    {
        const juce::ScopedLock sl (engineLock);
        if (auto c = slotClip (tracks[(size_t) t]->sessionSlots, s))
            return c->name.isNotEmpty() ? c->name : juce::String ("Clip");
        return {};
    }

    void cellMenu (int t, int s)
    {
        juce::PopupMenu m;
        const bool has = hasClip (t, s);
        m.addItem (1, "New empty clip", ! has);
        m.addItem (2, "Copy selected clip here");
        m.addItem (4, "Edit clip", has);
        m.addItem (5, "Copy to arrangement", has);
        m.addItem (3, "Clear", has);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [this, t, s] (int r)
                         {
                             if      (r == 1 && onNewClip)           onNewClip (t, s);
                             else if (r == 2 && onCopySelectedClip)  onCopySelectedClip (t, s);
                             else if (r == 4 && onEditClip)          onEditClip (t, s);
                             else if (r == 5 && onCopyToArrangement) onCopyToArrangement (t, s);
                             else if (r == 3 && onClearSlot)         onClearSlot (t, s);
                         });
    }

    void updateSize() { setSize (juce::jmax (preferredWidth(), getWidth()), juce::jmax (preferredHeight(), getHeight())); }
    void timerCallback() override
    {
        const int nt = (int) tracks.size();
        if ((int) meterL.size() != nt || (int) peakHoldL.size() != nt)   // any buffer out of step -> resync all
        {
            meterL.assign ((size_t) nt, 0.0f); meterR.assign ((size_t) nt, 0.0f);
            peakHoldL.assign ((size_t) nt, 0.0f); peakHoldR.assign ((size_t) nt, 0.0f);
        }
        for (int t = 0; t < nt; ++t)
        {
            float l = 0.0f, r = 0.0f;
            if (getTrackLevels) getTrackLevels (t, l, r);
            meterL[(size_t) t] = juce::jmax (l, meterL[(size_t) t] * 0.80f);   // fast attack, smooth decay
            meterR[(size_t) t] = juce::jmax (r, meterR[(size_t) t] * 0.80f);
            peakHoldL[(size_t) t] = sv::holdPeak (l, peakHoldL[(size_t) t]);    // held-peak marker (slow fall)
            peakHoldR[(size_t) t] = sv::holdPeak (r, peakHoldR[(size_t) t]);
        }
        if (transport.isPlaying() && ++blink % 15 == 0) blinkOn = ! blinkOn;
        repaint();
    }

    std::vector<std::unique_ptr<Track>>& tracks;
    std::vector<Scene>&                  scenes;
    SessionLauncher&                     launcher;
    Transport&                           transport;
    juce::CriticalSection&               engineLock;
    std::vector<Strip>                   strips;
    std::vector<float>                   meterL, meterR;         // smoothed stereo VU per track
    std::vector<float>                   peakHoldL, peakHoldR;   // held-peak markers per track
    std::vector<juce::Rectangle<int>>    meterRect;              // per-track meter bounds (set in resized)
    int  blink { 0 };
    bool blinkOn { true };
};

/** The frozen scene/master column: the "Stop All" + launch-quantum header, one scene-launch button
    per global scene row, a "+ Scene" footer, and the master strip (fader + stereo meter) at the
    bottom. SessionPane pins this to the LEFT of the track grid; it scrolls vertically in lockstep
    with the grid (via its viewport) but never horizontally, so scene launchers stay on screen no
    matter how many tracks there are. Repaints at 30 Hz. */
class SessionSceneColumn : public juce::Component,
                           private juce::Timer
{
public:
    SessionSceneColumn (std::vector<std::unique_ptr<Track>>& tracksRef,
                        std::vector<Scene>& scenesRef,
                        SessionLauncher& launcherRef,
                        Transport& transportRef,
                        juce::CriticalSection& engineLockRef)
        : tracks (tracksRef), scenes (scenesRef), launcher (launcherRef),
          transport (transportRef), engineLock (engineLockRef)
    {
        startTimerHz (30);
    }
    ~SessionSceneColumn() override { stopTimer(); }

    std::function<void (int)>      onLaunchScene;     // (scene)
    std::function<void()>          onStopAll;
    std::function<void()>          onAddScene;
    std::function<void (int)>      onRemoveScene;     // (scene)
    std::function<double()>        getQuantumBeats;   // current launch quantum (beats; 0 = off)
    std::function<void (double)>   onSetQuantumBeats; // cycle the launch quantum
    std::function<float()>         getMasterVolume;   // master fader (0..1)
    std::function<void (float)>    onSetMasterVolume;
    std::function<void (float&, float&)> getMasterLevels;   // master L,R peak

    void rebuild()
    {
        masterVol = std::make_unique<juce::Slider> (juce::Slider::LinearVertical, juce::Slider::NoTextBox);
        masterVol->setRange (0.0, 1.0, 0.0);
        masterVol->setValue (getMasterVolume ? getMasterVolume() : 0.8, juce::dontSendNotification);
        masterVol->onValueChange = [this] { if (onSetMasterVolume) onSetMasterVolume ((float) masterVol->getValue()); };
        addAndMakeVisible (*masterVol);

        updateSize();
        resized();
        repaint();
    }

    int preferredHeight() const { return sv::contentHeight ((int) scenes.size()); }

    void resized() override
    {
        if (masterVol != nullptr)
        {
            auto r = masterStripRect().reduced (8, 8);
            r.removeFromTop (16);                              // "MASTER" + dB label (drawn in paint)
            masterMeterRect = r.removeFromRight (r.getWidth() / 2 - 3);
            masterVol->setBounds (r);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1b1b1f));
        const int nt = (int) tracks.size();
        const int ns = (int) scenes.size();

        std::vector<int> pending ((size_t) nt, SessionLauncher::kNone);
        {
            const juce::ScopedLock sl (engineLock);
            for (int t = 0; t < nt; ++t) pending[(size_t) t] = launcher.pendingSlot (t);
        }

        // Scene-column header: "Stop All" + the launch-quantum selector.
        {
            auto h = stopAllRect();
            g.setColour (juce::Colour (0xff3a2a2a));
            g.fillRoundedRectangle (h.reduced (2.0f), 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0")) + " Stop",
                        h.reduced (5, 0), juce::Justification::centredLeft, false);

            auto q = quantumRect();
            g.setColour (juce::Colour (0xff2a2a34));
            g.fillRoundedRectangle (q.reduced (2.0f), 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText ("Q " + sv::quantumLabel (getQuantumBeats ? getQuantumBeats() : 4.0),
                        q.reduced (5, 0), juce::Justification::centredLeft, false);
        }

        // Scene-launch column.
        for (int s = 0; s < ns; ++s)
        {
            auto r = sceneRect (s).reduced (2.0f);
            bool sceneQueued = false;   // any track queued to launch this scene's slot
            for (int t = 0; t < nt; ++t) if (pending[(size_t) t] == s) { sceneQueued = true; break; }
            g.setColour (juce::Colour (0xff30303a));
            g.fillRoundedRectangle (r, 3.0f);
            if (sceneQueued)
            {
                g.setColour (juce::Colour (0xff33dd66).withAlpha (blinkOn ? 0.9f : 0.35f));
                g.drawRoundedRectangle (r, 3.0f, 2.0f);
            }
            g.setColour (juce::Colour (0xff33dd66).withAlpha (0.9f));
            juce::Path tri; auto tb = r.withWidth (14.0f).reduced (4.0f).translated (2.0f, 0.0f);
            tri.addTriangle (tb.getX(), tb.getY(), tb.getX(), tb.getBottom(), tb.getRight(), tb.getCentreY());
            g.fillPath (tri);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (scenes[(size_t) s].name, r.withTrimmedLeft (18.0f).reduced (2, 0), juce::Justification::centredLeft, true);
        }

        // Footer: "+ Scene".
        {
            auto r = addSceneRect().reduced (2.0f);
            g.setColour (juce::Colour (0xff2a2a30));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText ("+ Scene", r, juce::Justification::centred, false);
        }

        // Master strip: label, dB, stereo meter; the fader is a child component.
        if (masterVol != nullptr && nt > 0)
        {
            auto strip = masterStripRect();
            g.setColour (juce::Colour (0xff26262e));
            g.fillRoundedRectangle (strip.toFloat().reduced (3.0f), 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            const float mv = getMasterVolume ? getMasterVolume() : 0.8f;
            const juce::String db = mv <= 0.0001f ? juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e"))
                                                  : juce::String (20.0f * std::log10 (mv), 1) + " dB";
            g.drawText ("MASTER  " + db, strip.reduced (8, 5).removeFromTop (14).toFloat(),
                        juce::Justification::centredLeft, false);
            if (! masterMeterRect.isEmpty())
            {
                const int bw = (masterMeterRect.getWidth() - 2) / 2;
                sv::drawMeter (g, masterMeterRect.withWidth (bw), masterMeterL, masterPeakL);
                sv::drawMeter (g, masterMeterRect.withX (masterMeterRect.getRight() - bw).withWidth (bw), masterMeterR, masterPeakR);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int ns = (int) scenes.size();
        const auto p = e.position;
        if (addSceneRect().contains (p)) { if (onAddScene) onAddScene(); return; }
        if (stopAllRect().contains (p))  { if (onStopAll)  onStopAll();  return; }
        if (quantumRect().contains (p))  { cycleQuantum(); return; }

        for (int s = 0; s < ns; ++s)
            if (sceneRect (s).contains (p))
            {
                if (e.mods.isPopupMenu()) sceneMenu (s);
                else if (onLaunchScene) onLaunchScene (s);
                return;
            }
    }

private:
    juce::Rectangle<float> sceneHeaderRect() const
    { return { (float) sv::kPad, (float) sv::kPad, (float) sv::kSceneW, (float) sv::kHeaderH }; }
    juce::Rectangle<float> stopAllRect() const { return sceneHeaderRect().removeFromLeft (sv::kSceneW * 0.5f); }
    juce::Rectangle<float> quantumRect() const { auto h = sceneHeaderRect(); h.removeFromLeft (sv::kSceneW * 0.5f); return h; }
    juce::Rectangle<float> sceneRect (int s) const
    { return { (float) sv::kPad, (float) (sv::kPad + sv::kHeaderH + s * sv::kRowH), (float) sv::kSceneW, (float) sv::kRowH }; }
    juce::Rectangle<float> addSceneRect() const
    { return { (float) sv::kPad, (float) (sv::kPad + sv::kHeaderH + (int) scenes.size() * sv::kRowH), (float) sv::kSceneW, (float) sv::kFooterH }; }
    juce::Rectangle<int> masterStripRect() const
    { return { sv::kPad, juce::jmax (sv::kPad, getHeight() - sv::kPad - sv::kMixerH), sv::kSceneW, sv::kMixerH }; }

    void sceneMenu (int s)
    {
        juce::PopupMenu m;
        m.addItem (1, "Launch scene");
        m.addItem (2, "Delete scene");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [this, s] (int r)
                         {
                             if      (r == 1 && onLaunchScene) onLaunchScene (s);
                             else if (r == 2 && onRemoveScene) onRemoveScene (s);
                         });
    }
    void cycleQuantum()
    {
        static const double opts[] = { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
        const double cur = getQuantumBeats ? getQuantumBeats() : 4.0;
        int idx = 5; double best = 1e9;
        for (int i = 0; i < 7; ++i) { const double d = std::abs (opts[i] - cur); if (d < best) { best = d; idx = i; } }
        if (onSetQuantumBeats) onSetQuantumBeats (opts[(idx + 1) % 7]);
        repaint();
    }

    void updateSize() { setSize (juce::jmax (sv::kSceneW + 2 * sv::kPad, getWidth()), juce::jmax (preferredHeight(), getHeight())); }
    void timerCallback() override
    {
        float l = 0.0f, r = 0.0f; if (getMasterLevels) getMasterLevels (l, r);
        masterMeterL = juce::jmax (l, masterMeterL * 0.80f); masterMeterR = juce::jmax (r, masterMeterR * 0.80f);
        masterPeakL = sv::holdPeak (l, masterPeakL); masterPeakR = sv::holdPeak (r, masterPeakR);
        if (transport.isPlaying() && ++blink % 15 == 0) blinkOn = ! blinkOn;
        repaint();
    }

    std::vector<std::unique_ptr<Track>>& tracks;
    std::vector<Scene>&                  scenes;
    SessionLauncher&                     launcher;
    Transport&                           transport;
    juce::CriticalSection&               engineLock;
    std::unique_ptr<juce::Slider>        masterVol;        // master strip fader
    juce::Rectangle<int>                 masterMeterRect;
    float                                masterMeterL { 0.0f }, masterMeterR { 0.0f };
    float                                masterPeakL { 0.0f }, masterPeakR { 0.0f };
    int  blink { 0 };
    bool blinkOn { true };
};

/** Composes the session view: the scene/master column pinned to the LEFT, and the track grid in a
    horizontally + vertically scrolling viewport to its right. The scene column follows the grid's
    vertical scroll (so scene rows stay aligned with clip rows) but is frozen horizontally, so the
    scene launchers + master strip are always on screen regardless of the track count. */
class SessionPane : public juce::Component
{
public:
    SessionPane (std::vector<std::unique_ptr<Track>>& tracksRef,
                 std::vector<Scene>& scenesRef,
                 SessionLauncher& launcherRef,
                 Transport& transportRef,
                 juce::CriticalSection& engineLockRef)
        : gridView (tracksRef, scenesRef, launcherRef, transportRef, engineLockRef),
          sceneCol (tracksRef, scenesRef, launcherRef, transportRef, engineLockRef)
    {
        sceneVp.setViewedComponent (&sceneCol, false);
        sceneVp.setScrollBarsShown (false, false);
        addAndMakeVisible (sceneVp);

        gridVp.setViewedComponent (&gridView, false);
        gridVp.setScrollBarsShown (true, true);
        gridVp.onArea = [this] (const juce::Rectangle<int>& r) { sceneVp.setViewPosition (0, r.getY()); };
        addAndMakeVisible (gridVp);
    }

    SessionView&        grid()        { return gridView; }
    SessionSceneColumn& sceneColumn() { return sceneCol; }

    void rebuild() { gridView.rebuild(); sceneCol.rebuild(); resized(); }

    void resized() override
    {
        auto a = getLocalBounds();
        const int wCol = sv::kSceneW + 2 * sv::kPad;
        sceneVp.setBounds (a.removeFromLeft (wCol));
        gridVp.setBounds (a);
        gridView.setSize (juce::jmax (gridView.preferredWidth(),  gridVp.getMaximumVisibleWidth()),
                          juce::jmax (gridView.preferredHeight(), gridVp.getHeight()));
        sceneCol.setSize (wCol, juce::jmax (sceneCol.preferredHeight(), sceneVp.getHeight()));
        sceneVp.setViewPosition (0, gridVp.getViewPositionY());   // keep the frozen column in sync
    }

private:
    // Viewport that reports scroll changes so the frozen scene column can mirror the vertical offset.
    struct SyncViewport : juce::Viewport
    {
        std::function<void (const juce::Rectangle<int>&)> onArea;
        void visibleAreaChanged (const juce::Rectangle<int>& r) override { if (onArea) onArea (r); }
    };
    // Frozen column: driven only by the grid's vertical scroll — swallow its own wheel so it can't desync.
    struct FrozenViewport : juce::Viewport
    {
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}
    };

    SessionView        gridView;
    SessionSceneColumn sceneCol;
    SyncViewport   gridVp;
    FrozenViewport sceneVp;
};
