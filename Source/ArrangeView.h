// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <functional>
#include <set>
#include <map>
#include "Track.h"
#include "Transport.h"

/** The linear arrangement: track headers on the left, a timeline of clips on
    the right, one playhead. Create/move/resize/delete clips; selecting a clip
    notifies the owner so it can open the clip in the editor below. */
class ArrangeView : public juce::Component,
                    public juce::DragAndDropTarget,   // accept browser rows dragged in
                    private juce::Timer
{
public:
    // A browser row was dropped on the arrangement — payload is "kind\tref\tlabel".
    // targetTrack = the track lane under the drop (-1 if it missed the lanes / hit the ruler);
    // beat = the bar-snapped drop position, so a sample lands where it was dropped.
    std::function<void (const juce::String& desc, int targetTrack, double beat)> onBrowserDrop;
    bool isInterestedInDragSource (const SourceDetails& d) override
    { return d.description.toString().containsChar ('\t'); }   // our rows carry tab-separated payloads
    void itemDragEnter (const SourceDetails&) override { dropHighlight = true; repaint(); }
    void itemDragExit  (const SourceDetails&) override { dropHighlight = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dropHighlight = false; repaint();
        if (! onBrowserDrop) return;
        const auto p = d.localPosition.toFloat();
        const int  tt = trackAtY (p.y);
        const double beat = tt >= 0 ? juce::jmax (0.0, snapToBar (beatForX (p.x))) : 0.0;
        onBrowserDrop (d.description.toString(), tt, beat);
    }

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
    struct SamplerCtl { bool isSampler { false }; float start { 0.0f }, end { 1.0f }; bool reverse { false }; int root { 60 }; float fadeIn { 0.0f }, fadeOut { 0.0f }; bool loop { false }; bool mono { false }; float loopXfade { 0.0f }; int interp { 0 }; };
    std::function<SamplerCtl (int)>                                        getSamplerControls;
    std::function<void (int, float, float, bool, int, float, float, bool, bool, float, int)> onSetSamplerControls;   // track, start, end, reverse, root, fadeIn, fadeOut, loop, mono, loopXfade, interp
    /** Right-click clip menu command: (track, clip, command). Commands: "split",
        "duplicate", "reverse", "snapscale", "delete". Owner routes to the api* ops. */
    std::function<void (int, int, const juce::String&)> onClipCommand;
    std::function<void (int, int, const juce::String&)> onRenameClip;   // track, clip, new name
    std::function<void (int, int, const juce::String&, const juce::String&, const juce::String&)> onSetGenerator;   // track, clip, generator, system, lang -> named-generator script clip
    std::function<void (int, const juce::String&)>      onRenameTrack;   // track index, new name
    std::function<void (int, const juce::String&)>      onSetTrackColour; // track index, 8-hex ARGB
    std::function<void (int, int)>                      onMoveTrack;     // track index, delta (<0 up, >0 down)
    std::function<void (int, bool)>                     onSetTrackPolarity; // track index, invert
    std::function<void (int)>                           onExportTrack;   // track index -> bounce a stem
    std::function<void (int)>                           onDuplicateTrack; // track index -> clone the track
    std::function<void (int, int)>                      onChangeInstrument; // track index, kind (0=Surge, 1=Basic synth)
    std::function<void (int)>                           onRemoveTrack;   // track index -> delete the track (+ its insert)
    /** Right-click an empty part of a track -> "Paste notes here": (track, beat). The
        owner reads JSON notes from the system clipboard and builds a clip at that beat. */
    std::function<void (int, double)> onPasteNotes;
    std::function<void (int, int, float)> onClipGain;   // track, clip, gain dB (audio clips)
    std::function<void (int, int, double, double)> onClipFades;   // track, clip, fadeIn, fadeOut (beats)
    /** Tempo markers drawn on the ruler + edited by right-clicking it. Owner wires
        these to apiListTempoMarkers/apiAddTempoMarker/apiRemoveTempoMarker. */
    std::function<std::vector<std::pair<double, double>>()> getTempoMarkers;   // (beat, bpm)
    std::function<std::vector<std::pair<juce::String, double>>()> getMarkers;   // timeline locations (name, beat) for "Split at marker"
    std::function<float(int trackId)> getMidiActivity;   // 0..1 input-LED brightness for a track (fades after a note)
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

    // --- automation lanes ---
    // One entry per parameter automation lane the owner wants shown, already resolved to the track
    // it belongs to plus the param's value range (so we can normalise the curve into the row).
    struct AutoLaneView
    {
        juce::String target;                       // ParamModel id, e.g. "track/3/macro/0"
        int          trackId { -1 };               // Track::id this lane draws on
        float        lo { 0.0f }, hi { 1.0f };     // param value range, for vertical normalisation
        bool         step { false };               // stepped (hold) vs ramped
        float        curve { 0.0f };               // ease amount (-1..1)
        std::vector<std::pair<double, float>> points;   // (beat, value), sorted by beat
    };
    std::function<std::vector<AutoLaneView>()> getAutomation;   // owner supplies track-owned lanes
    // Content-less bus/group/master rows below the tracks: each carries only automation lanes.
    struct BusRowView { int mixerIndex { 0 }; juce::String name; juce::Colour colour; std::vector<AutoLaneView> lanes; };
    std::function<std::vector<BusRowView>()> getBusRows;        // master + buses that have automation
    void refreshAutomation();                                   // re-pull + repaint (on edits / load)
    std::function<void (const juce::String&, double, float)> onAddAutomationPoint;  // target, beat, value
    std::function<void (const juce::String&, std::vector<std::pair<double, float>>)> onSetAutomation;  // target, points (commit)
    // Parameter picker: the automatable params of a track, as (label, target) — for the sub-lane menu.
    std::function<std::vector<std::pair<juce::String, juce::String>> (int trackId)> getTrackParams;
    std::function<void (int trackId, const juce::String& target)> onPickAutomationParam;   // create/focus a lane
    std::function<void (const juce::String&, bool)>  onSetAutomationStep;    // stepped (hold) vs ramped
    std::function<void (const juce::String&, float)> onSetAutomationCurve;   // ease -1..0..+1
    std::function<void ()>                           onPlaybackStopped;      // play->stop edge (end a write pass)

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
    double snapToGrid (double beat) const;                     // finer (1/4-beat) snap for breakpoints
    void   trackBand (int track, float& top, float& bot) const;   // padded content y-range of a row
    bool   hitAutoPoint (int track, juce::Point<float> p, int& laneOut, int& pointOut) const;  // grab a breakpoint
    int    firstAutoLane (int track) const;                    // first cached lane on a track (-1 if none)
    int    clipAt (int track, juce::Point<float> p) const;
    void   drawClip (juce::Graphics&, const Track&, const Clip&, juce::Rectangle<float>, bool selected) const;
    void   drawOneLane (juce::Graphics&, const AutoLaneView&, float top, float bot) const;   // one lane's curve
    void   drawAutomation (juce::Graphics&, int trackId, float top, float bot) const;        // overlay all lanes (collapsed)

    // Stacked sub-lanes when expanded: one band per automation lane on the track.
    int    laneCountFor (int track) const;
    void   trackLaneIndices (int track, std::vector<int>& out) const;   // global autoLanes indices, in order
    void   laneBand (int track, int k, float& top, float& bot) const;   // band of the k-th sub-lane
    int    laneAtY (int track, float y) const;                          // which sub-lane a y is in (-1 = none)

    // Variable row height: a track's row is trackHeight, plus laneExtra when its automation lane is
    // expanded (broken out below the clips). All track→y math goes through rowTop/rowHeight.
    bool   isExpanded (int i) const;
    int    rowHeight (int i) const;   // trackHeight (+ laneExtra if expanded)
    int    rowTop (int i) const;      // y of the top of track i's row (below the ruler)
    void   promptAddTempoMarker (double beat);   // AlertWindow BPM prompt -> onAddTempoMarker
    void   promptAddMarker (double beat);        // AlertWindow name prompt -> onAddMarker
    void   promptTimeSignature();                // AlertWindow num/denom prompt -> onSetTimeSignature
    void   promptClipGain (int track, int clip); // AlertWindow dB prompt -> onClipGain
    void   promptClipFades (int track, int clip); // AlertWindow in/out prompt -> onClipFades

    static constexpr int headerWidth = 190;
    static constexpr int rulerHeight  = 22;
    static constexpr int trackHeight  = 64;
    static constexpr int pickerRowH   = 22;   // "+ Lane" picker strip at the top of an expanded row
    static constexpr int laneRowH     = 46;   // height of each stacked automation sub-lane
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
    std::vector<std::unique_ptr<juce::TextButton>> expandButtons;   // per-track automation-lane disclosure
    std::vector<std::unique_ptr<juce::TextButton>> paramButtons;    // per-track sub-lane parameter picker

    int selTrack { -1 }, selClip { -1 };

    std::vector<AutoLaneView> autoLanes;   // cached from getAutomation(); refreshed on edits/load
    std::vector<BusRowView>   busRows;     // cached content-less bus/master rows below the tracks
    static constexpr int      busRowH = 52;
    std::set<int> expandedTracks;          // track ids whose automation lane is broken out below
    std::map<int, juce::String> focusedTarget;   // track id -> the param its sub-lane shows/edits

    enum class Drag { none, move, resize, point };
    Drag   drag { Drag::none };
    int    dragTrack { -1 }, dragClip { -1 };
    double dragBeatOffset { 0.0 };
    int    dragAutoLane { -1 }, dragAutoPoint { -1 };   // automation breakpoint being dragged
    bool   wasPlaying { false };                        // for the play->stop edge (write mode)

    // Ruler drag (seek / loop region; Alt = punch region).
    bool   rulerDrag { false }, loopDragged { false }, rulerAlt { false };
    bool   dropHighlight { false };   // a browser drag is hovering the arrangement
    double rulerStartBeat { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeView)
};
