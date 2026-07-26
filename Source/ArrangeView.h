// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <functional>
#include "Track.h"
#include "Transport.h"

/** The linear arrangement: track headers on the left, a timeline of clips on
    the right, one playhead. Create/move/resize/delete clips; selecting a clip
    notifies the owner so it can open the clip in the editor below. */
class ArrangeView : public juce::Component,
                    private juce::Timer
{
public:
    ArrangeView (std::vector<std::unique_ptr<Track>>& tracksRef,
                 Transport& transportRef,
                 juce::CriticalSection& engineLockRef);
    ~ArrangeView() override;

    /** Recreate per-track header controls after tracks are added/removed. */
    void rebuild();

    int preferredHeight() const;

    void setSelection (int track, int clip) { selTrack = track; selClip = clip; repaint(); }

    /** (track, clip) — clip = -1 means "no clip / just a track selected". */
    std::function<void (int, int)> onClipSelected;
    /** A clip's content/position changed (repaint editor previews etc.). */
    std::function<void()> onChanged;
    /** The loop region was enabled/disabled/moved from the ruler. */
    std::function<void()> onLoopChanged;
    /** Open the plugin editor for a plugin-instrument track (arg = track index). */
    std::function<void (int)> onOpenTrackEditor;
    /** Open the live-arpeggiator config menu for an instrument track (arg = track index).
        The owner builds the menu from apiGetTrackArp / apiSetTrackArp. */
    std::function<void (int)> onArpMenu;
    /** Right-click a Sampler track header -> playback-window menu. getSamplerControls
        returns {isSampler, start, end, reverse, root} for the track (index); the owner
        opens the prompt and routes the result to onSetSamplerControls. */
    struct SamplerCtl { bool isSampler { false }; float start { 0.0f }, end { 1.0f }; bool reverse { false }; int root { 60 }; float fadeIn { 0.0f }, fadeOut { 0.0f }; bool loop { false }; bool mono { false }; };
    std::function<SamplerCtl (int)>                                        getSamplerControls;
    std::function<void (int, float, float, bool, int, float, float, bool, bool)> onSetSamplerControls;   // track, start, end, reverse, root, fadeIn, fadeOut, loop, mono
    /** Right-click clip menu command: (track, clip, command). Commands: "split",
        "duplicate", "reverse", "snapscale", "delete". Owner routes to the api* ops. */
    std::function<void (int, int, const juce::String&)> onClipCommand;
    std::function<void (int, const juce::String&)>      onRenameTrack;   // track index, new name
    std::function<void (int, const juce::String&)>      onSetTrackColour; // track index, 8-hex ARGB
    std::function<void (int, int)>                      onMoveTrack;     // track index, delta (<0 up, >0 down)
    std::function<void (int, bool)>                     onSetTrackPolarity; // track index, invert
    std::function<void (int)>                           onExportTrack;   // track index -> bounce a stem
    /** Right-click an empty part of a track -> "Paste notes here": (track, beat). The
        owner reads JSON notes from the system clipboard and builds a clip at that beat. */
    std::function<void (int, double)> onPasteNotes;
    std::function<void (int, int, float)> onClipGain;   // track, clip, gain dB (audio clips)
    std::function<void (int, int, double, double)> onClipFades;   // track, clip, fadeIn, fadeOut (beats)
    /** Tempo markers drawn on the ruler + edited by right-clicking it. Owner wires
        these to apiListTempoMarkers/apiAddTempoMarker/apiRemoveTempoMarker. */
    std::function<std::vector<std::pair<double, double>>()> getTempoMarkers;   // (beat, bpm)
    std::function<std::vector<std::pair<juce::String, double>>()> getMarkers;   // timeline locations (name, beat) for "Split at marker"
    std::function<void (double, double)> onAddTempoMarker;                     // beat, bpm
    std::function<void (double)>         onRemoveTempoMarker;                  // beat
    std::function<void (const juce::String&, double)> onAddMarker;             // named timeline marker: name, beat
    std::function<void (const juce::String&)>         onRemoveMarker;          // remove a named marker
    std::function<void (int, int)>       onSetTimeSignature;                   // numerator, denominator
    std::function<double()>              getSwing;                             // current swing (0.5 = straight)
    std::function<float()>               getMetronomeLevel;                    // current click volume (0..1)
    std::function<void (float)>          onSetMetronomeLevel;                  // set click volume
    std::function<void (double)>         onSetSwing;                           // set the groove swing amount
    /** Recording punch region drawn on the ruler + set by Alt-dragging it (like the
        loop region). getPunchRange fills in/out and returns whether it's enabled;
        onSetPunchRange upserts via apiSetPunchRange. */
    std::function<bool (double&, double&)> getPunchRange;                      // -> enabled, fills in/out
    std::function<void (bool, double, double)> onSetPunchRange;                // enabled, in, out

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    int    numBars() const;
    float  barWidth() const;
    float  xForBeat (double beat) const;
    double beatForX (float x) const;
    int    trackAtY (float y) const;
    double snapToBar (double beat) const;
    int    clipAt (int track, juce::Point<float> p) const;
    void   drawClip (juce::Graphics&, const Track&, const Clip&, juce::Rectangle<float>, bool selected) const;
    void   promptAddTempoMarker (double beat);   // AlertWindow BPM prompt -> onAddTempoMarker
    void   promptAddMarker (double beat);        // AlertWindow name prompt -> onAddMarker
    void   promptTimeSignature();                // AlertWindow num/denom prompt -> onSetTimeSignature
    void   promptClipGain (int track, int clip); // AlertWindow dB prompt -> onClipGain
    void   promptClipFades (int track, int clip); // AlertWindow in/out prompt -> onClipFades

    static constexpr int headerWidth = 190;
    static constexpr int rulerHeight  = 22;
    static constexpr int trackHeight  = 64;
    double beatsPerBar = 4.0;   // refreshed from the transport's time signature on rebuild/resize/paint

    std::vector<std::unique_ptr<Track>>& tracks;
    Transport&             transport;
    juce::CriticalSection& engineLock;

    std::vector<std::unique_ptr<juce::TextButton>> muteButtons;
    std::vector<std::unique_ptr<juce::TextButton>> soloButtons;
    std::vector<std::unique_ptr<juce::TextButton>> editButtons;   // plugin UI (plugin tracks only)
    std::vector<std::unique_ptr<juce::TextButton>> armButtons;    // record-arm (audio tracks only)
    std::vector<std::unique_ptr<juce::TextButton>> arpButtons;    // live arpeggiator (instrument tracks)
    std::vector<std::unique_ptr<juce::Slider>>     volSliders;

    int selTrack { -1 }, selClip { -1 };

    enum class Drag { none, move, resize };
    Drag   drag { Drag::none };
    int    dragTrack { -1 }, dragClip { -1 };
    double dragBeatOffset { 0.0 };

    // Ruler drag (seek / loop region; Alt = punch region).
    bool   rulerDrag { false }, loopDragged { false }, rulerAlt { false };
    double rulerStartBeat { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeView)
};
