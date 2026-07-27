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

/** Session view — the Ableton-style clip-launch grid (columns = tracks, rows = global scenes),
    with a per-column **mini-mixer strip** at the bottom (pan / volume fader / VU meter / solo /
    mute), matching the simplified redesign in docs/session-view.md.

    Simplified, color-coded cells: each is a track-colored bar for a launchable looping clip; a
    right scene-launch column fires a whole row. Left-click a clip to launch it (an empty cell to
    stop that track); left-click a scene to launch the row. Right-click a cell for a menu (new
    empty clip / copy the selected clip here / clear). Playing cells glow; queued cells pulse.

    The clip grid is custom-drawn; the mixer controls are real JUCE child components (they write
    the Track atomics directly, like ArrangeView) so dragging/toggling is robust. The VU meter is
    drawn from the routed mixer track's peak via getTrackLevel. Repaints at 30 Hz. */
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
    std::function<void (int)>      onStopTrack;       // (track index) -> back to arrangement
    std::function<void (int)>      onLaunchScene;     // (scene)
    std::function<void()>          onStopAll;
    std::function<void()>          onAddScene;
    std::function<void (int)>      onRemoveScene;     // (scene)
    std::function<void (int, int)> onNewClip;         // (track index, scene) -> empty MIDI clip
    std::function<void (int, int)> onCopySelectedClip;// (track index, scene) -> copy the selected arrangement clip
    std::function<void (int, int)> onClearSlot;       // (track index, scene)
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

            s.fx = std::make_unique<juce::TextButton> ("FX");
            s.fx->onClick = [this, t] { if (onOpenTrackFx) onOpenTrackFx (t); };

            addAndMakeVisible (*s.vol);  addAndMakeVisible (*s.pan);
            addAndMakeVisible (*s.solo); addAndMakeVisible (*s.mute); addAndMakeVisible (*s.fx);
            strips.push_back (std::move (s));
        }
        meterL.assign ((size_t) nt, 0.0f);
        meterR.assign ((size_t) nt, 0.0f);
        meterRect.assign ((size_t) nt, {});
        updateSize();
        resized();
        repaint();
    }

    int preferredWidth()  const { return (int) tracks.size() * kTrackW + kSceneW + 2 * kPad; }
    int preferredHeight() const { return kHeaderH + (int) scenes.size() * kRowH + kFooterH + kMixerH + 3 * kPad; }

    void resized() override
    {
        const int nt = juce::jmin ((int) tracks.size(), (int) strips.size());
        for (int t = 0; t < nt; ++t)
        {
            auto r = mixerStripRect (t).reduced (6, 6);
            r.removeFromTop (14);                              // dB readout (drawn in paint)
            strips[(size_t) t].pan->setBounds (r.removeFromTop (30).withSizeKeepingCentre (30, 30));   // rotary pan
            r.removeFromTop (2);
            auto btn = r.removeFromBottom (18);                // S | M | FX
            r.removeFromBottom (4);
            auto fader = r.removeFromLeft ((int) (r.getWidth() * 0.48f));
            r.removeFromLeft (4);
            meterRect[(size_t) t] = r;                         // stereo VU (two bars, drawn in paint)
            strips[(size_t) t].vol->setBounds (fader);
            const int bw = (btn.getWidth() - 8) / 3;
            strips[(size_t) t].solo->setBounds (btn.removeFromLeft (bw)); btn.removeFromLeft (4);
            strips[(size_t) t].mute->setBounds (btn.removeFromLeft (bw)); btn.removeFromLeft (4);
            strips[(size_t) t].fx->setBounds (btn);
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
            g.drawVerticalLine (kPad + t * kTrackW, (float) kPad, (float) (getHeight() - kPad));

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

        // Scene-column header: "Stop All".
        {
            auto h = stopAllRect();
            g.setColour (juce::Colour (0xff3a2a2a));
            g.fillRoundedRectangle (h.reduced (2.0f), 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0")) + "  Stop All",
                        h.reduced (6, 0), juce::Justification::centredLeft, false);
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
                else
                {
                    g.setColour (juce::Colour (0xff242429));
                    g.fillRoundedRectangle (r, 3.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.12f));
                    g.drawRect (r.withSizeKeepingCentre (9.0f, 9.0f), 1.4f);
                }
                if (isPending)
                {
                    g.setColour (juce::Colours::white.withAlpha (blinkOn ? 0.9f : 0.25f));
                    g.drawRoundedRectangle (r, 3.0f, 1.6f);
                }
            }

        // Scene-launch column.
        for (int s = 0; s < ns; ++s)
        {
            auto r = sceneRect (s).reduced (2.0f);
            g.setColour (juce::Colour (0xff30303a));
            g.fillRoundedRectangle (r, 3.0f);
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

            // Stereo VU meter (routed mixer-track peakL/R, smoothed), track-colored, two bars.
            auto m = meterRect[(size_t) t];
            if (! m.isEmpty())
            {
                const int bw = (m.getWidth() - 2) / 2;
                auto drawBar = [&] (juce::Rectangle<int> bar, float lvl)
                {
                    g.setColour (juce::Colour (0xff141417));
                    g.fillRect (bar);
                    auto fill = bar.toFloat().withTrimmedTop (bar.getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, lvl)));
                    g.setColour (trackCol[(size_t) t].withAlpha (0.9f));
                    g.fillRect (fill);
                    g.setColour (juce::Colours::white.withAlpha (0.12f));
                    g.drawRect (bar, 1);
                };
                drawBar (m.withWidth (bw), meterL[(size_t) t]);
                drawBar (m.withX (m.getRight() - bw).withWidth (bw), meterR[(size_t) t]);
            }
        }

        if (nt == 0 || ns == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (nt == 0 ? "Add an instrument track to start a session grid"
                                : "Right-click a cell to add a clip, then click it to launch",
                        juce::Rectangle<int> (kPad, kHeaderH + 16, juce::jmax (200, getWidth() - 2 * kPad), 24),
                        juce::Justification::centredLeft, true);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int nt = (int) tracks.size(), ns = (int) scenes.size();
        const auto p = e.position;
        if (addSceneRect().contains (p)) { if (onAddScene) onAddScene(); return; }
        if (stopAllRect().contains (p))  { if (onStopAll)  onStopAll();  return; }

        for (int s = 0; s < ns; ++s)
        {
            if (sceneRect (s).contains (p))
            {
                if (e.mods.isPopupMenu()) sceneMenu (s);
                else if (onLaunchScene) onLaunchScene (s);
                return;
            }
            for (int t = 0; t < nt; ++t)
                if (cellRect (t, s).contains (p))
                {
                    if (e.mods.isPopupMenu()) { cellMenu (t, s); return; }
                    if (hasClip (t, s)) { if (onLaunchClip) onLaunchClip (t, s); }
                    else                { if (onStopTrack)  onStopTrack (t); }
                    return;
                }
        }
    }

private:
    struct Strip
    {
        std::unique_ptr<juce::Slider> vol, pan;
        std::unique_ptr<juce::TextButton> solo, mute, fx;
    };

    static constexpr int kPad = 8, kHeaderH = 30, kRowH = 30, kFooterH = 30, kTrackW = 128, kSceneW = 150, kMixerH = 150;

    juce::Rectangle<float> trackHeaderRect (int t) const
    { return { (float) (kPad + t * kTrackW), (float) kPad, (float) kTrackW, (float) kHeaderH }; }
    juce::Rectangle<float> stopAllRect() const
    { return { (float) (kPad + (int) tracks.size() * kTrackW), (float) kPad, (float) kSceneW, (float) kHeaderH }; }
    juce::Rectangle<float> cellRect (int t, int s) const
    { return { (float) (kPad + t * kTrackW), (float) (kPad + kHeaderH + s * kRowH), (float) kTrackW, (float) kRowH }; }
    juce::Rectangle<float> sceneRect (int s) const
    { return { (float) (kPad + (int) tracks.size() * kTrackW), (float) (kPad + kHeaderH + s * kRowH), (float) kSceneW, (float) kRowH }; }
    juce::Rectangle<float> addSceneRect() const
    { return { (float) (kPad + (int) tracks.size() * kTrackW), (float) (kPad + kHeaderH + (int) scenes.size() * kRowH), (float) kSceneW, (float) kFooterH }; }
    juce::Rectangle<int> mixerStripRect (int t) const
    { return { kPad + t * kTrackW, juce::jmax (kPad, getHeight() - kPad - kMixerH), kTrackW, kMixerH }; }

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
        m.addItem (3, "Clear", has);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [this, t, s] (int r)
                         {
                             if      (r == 1 && onNewClip)          onNewClip (t, s);
                             else if (r == 2 && onCopySelectedClip) onCopySelectedClip (t, s);
                             else if (r == 3 && onClearSlot)        onClearSlot (t, s);
                         });
    }
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

    void updateSize() { setSize (juce::jmax (preferredWidth(), getWidth()), juce::jmax (preferredHeight(), getHeight())); }
    void timerCallback() override
    {
        const int nt = (int) tracks.size();
        if ((int) meterL.size() != nt) { meterL.assign ((size_t) nt, 0.0f); meterR.assign ((size_t) nt, 0.0f); }
        for (int t = 0; t < nt; ++t)
        {
            float l = 0.0f, r = 0.0f;
            if (getTrackLevels) getTrackLevels (t, l, r);
            meterL[(size_t) t] = juce::jmax (l, meterL[(size_t) t] * 0.80f);   // fast attack, smooth decay
            meterR[(size_t) t] = juce::jmax (r, meterR[(size_t) t] * 0.80f);
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
    std::vector<float>                   meterL, meterR;   // smoothed stereo VU per track
    std::vector<juce::Rectangle<int>>    meterRect;        // per-track meter bounds (set in resized)
    int  blink { 0 };
    bool blinkOn { true };
};
