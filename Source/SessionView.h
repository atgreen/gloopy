// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <functional>
#include "Track.h"
#include "SessionModel.h"
#include "SessionLauncher.h"
#include "Transport.h"

/** Session view — the Ableton-style clip-launch grid (columns = tracks, rows = global scenes).
    Simplified, color-coded style (per docs/session-view.md): each cell is a colored bar for a
    launchable looping clip, a right scene-launch column fires a whole row. Left-click a clip to
    launch it (or an empty cell to stop that track); left-click a scene to launch the row.
    Right-click a cell for a menu (new empty clip / copy the selected clip here / clear). Playing
    cells glow; queued (pending) cells pulse. The owner routes the callbacks to the api* launch/
    grid methods and calls rebuild() when tracks/scenes change.

    Custom-drawn (no child components); reads launcher/slot state briefly under the engine lock in
    paint(), repainting at 30 Hz so playing/queued feedback animates. */
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

    void rebuild() { updateSize(); repaint(); }

    int preferredWidth()  const { return (int) tracks.size() * kTrackW + kSceneW + 2 * kPad; }
    int preferredHeight() const { return kHeaderH + (int) scenes.size() * kRowH + kFooterH + 2 * kPad; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1b1b1f));
        const int nt = (int) tracks.size();
        const int ns = (int) scenes.size();

        // Snapshot the launch state (brief lock; drawing happens after).
        std::vector<int> playing ((size_t) nt, SessionLauncher::kArrangement), pending ((size_t) nt, SessionLauncher::kNone);
        std::vector<juce::Colour> trackCol ((size_t) nt, juce::Colour (0xff6a6a72));
        std::vector<juce::String> trackName ((size_t) nt);
        {
            const juce::ScopedLock sl (engineLock);
            for (int t = 0; t < nt; ++t)
            {
                playing[(size_t) t]   = launcher.playingSlot (t);
                pending[(size_t) t]   = launcher.pendingSlot (t);
                trackCol[(size_t) t]  = tracks[(size_t) t]->colour;
                trackName[(size_t) t] = tracks[(size_t) t]->name;
            }
        }

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

        // Scene-column header: a global "stop all clips" button.
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
        {
            for (int t = 0; t < nt; ++t)
            {
                auto r = cellRect (t, s).reduced (2.0f);
                const bool has = hasClip (t, s);
                const bool isPlaying = playing[(size_t) t] == s;
                const bool isPending = pending[(size_t) t] == s;   // queued launch of this cell
                auto base = trackCol[(size_t) t];

                if (has)
                {
                    const float a = isPlaying ? 0.95f : 0.42f;
                    g.setColour (base.withAlpha (a));
                    g.fillRoundedRectangle (r, 3.0f);
                    // Play triangle + clip name.
                    g.setColour (juce::Colours::black.withAlpha (isPlaying ? 0.85f : 0.6f));
                    juce::Path tri; auto tb = r.withWidth (14.0f).reduced (4.0f).translated (2.0f, 0.0f);
                    tri.addTriangle (tb.getX(), tb.getY(), tb.getX(), tb.getBottom(), tb.getRight(), tb.getCentreY());
                    g.fillPath (tri);
                    g.setColour (juce::Colours::white.withAlpha (isPlaying ? 0.95f : 0.75f));
                    g.setFont (juce::FontOptions (10.5f, isPlaying ? juce::Font::bold : juce::Font::plain));
                    g.drawText (clipName (t, s), r.withTrimmedLeft (18.0f).reduced (2, 0),
                                juce::Justification::centredLeft, true);
                }
                else
                {
                    g.setColour (juce::Colour (0xff242429));
                    g.fillRoundedRectangle (r, 3.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.12f));           // empty-slot "stop" dot
                    g.drawRect (r.withSizeKeepingCentre (9.0f, 9.0f), 1.4f);
                }

                if (isPending)                                                     // queued: pulsing outline
                {
                    g.setColour (juce::Colours::white.withAlpha (blinkOn ? 0.9f : 0.25f));
                    g.drawRoundedRectangle (r, 3.0f, 1.6f);
                }
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
            g.drawText (scenes[(size_t) s].name, r.withTrimmedLeft (18.0f).reduced (2, 0),
                        juce::Justification::centredLeft, true);
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

        if (nt == 0 || ns == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (nt == 0 ? "Add an instrument track to start a session grid"
                                : "Right-click a cell to add a clip, then click it to launch",
                        getLocalBounds().reduced (20).withTrimmedTop (kHeaderH + 10),
                        juce::Justification::centredTop, true);
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
                    if (hasClip (t, s))       { if (onLaunchClip) onLaunchClip (t, s); }
                    else                      { if (onStopTrack)  onStopTrack (t); }   // empty cell = stop the track
                    return;
                }
        }
    }

private:
    // --- geometry ---
    static constexpr int kPad = 8, kHeaderH = 30, kRowH = 30, kFooterH = 30, kTrackW = 128, kSceneW = 150;

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

    void updateSize() { setSize (juce::jmax (preferredWidth(), getWidth()), preferredHeight()); }
    void timerCallback() override
    {
        if (transport.isPlaying() && ++blink % 15 == 0) blinkOn = ! blinkOn;   // pulse queued cells while rolling
        repaint();
    }

    std::vector<std::unique_ptr<Track>>& tracks;
    std::vector<Scene>&                  scenes;
    SessionLauncher&                     launcher;
    Transport&                           transport;
    juce::CriticalSection&               engineLock;
    int  blink { 0 };
    bool blinkOn { true };
};
