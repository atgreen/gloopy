// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include "Transport.h"
#include "Track.h"
#include "SessionLauncher.h"
#include "SessionView.h"
#include "FileDrop.h"
#include "BrowserDrag.h"
#include "Clip.h"
#include "TimeTypes.h"
#include "ArrangeView.h"
#include "BusyOverlay.h"
#include "KernelHost.h"
#include "PianoRoll.h"
#include "StepEditor.h"
#include "IconButton.h"
#include "MixerTrack.h"
#include "MappingsView.h"
#include "MixerView.h"
#include "DevicePanel.h"
#include "BrowserSidebar.h"
#include "Effects.h"
#include "PluginHost.h"
#include "PluginInstrument.h"
#include "PluginEffect.h"
#include "OscControl.h"
#include "GrpcServer.h"
#include "Palette.h"
#include "GloopyLookAndFeel.h"
#include <unordered_map>
#include <map>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <deque>
#include <future>

/** Linear-arranger workspace: an arrangement of instrument tracks (each owning
    its clips), a clip editor (piano roll), and the mixer. */
class MainComponent : public juce::AudioAppComponent,
                      public juce::MidiInputCallback,
                      public juce::FileDragAndDropTarget,
                      public juce::DragAndDropContainer,   // browser rows drag into the arrange view
                      public juce::KeyListener,
                      private juce::Timer
{
public:
    explicit MainComponent (bool headlessCli = false);   // headlessCli skips OSC/gRPC/audio (CLI tools)
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;
    void releaseResources() override;

    /** Hardware / virtual MIDI input — routed to the selected instrument track. */
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    /** Drag-and-drop: drop projects (.gloopy/.zip/composition dir), MIDI (.mid/.midi),
        or audio (.wav/.aif/.flac) onto the window to open/import them. Routes to the
        same ops as the File menu (openAny / apiImportMidi / apiImportAudio). */
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** Mix one block (transport → tracks → inserts → master → @p outBuf) and
        return the song length in samples. Shared by the live callback and the
        offline renderer; caller must hold the engine lock. */
    juce::int64 renderBlock (juce::AudioBuffer<float>& outBuf, int start, int num, bool ignoreLoopWindow);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;   // Ctrl+Z / Ctrl+Shift+Z / Tab
    // KeyListener on the top-level window: catches keys even when no component has focus, so Tab
    // (view switch) works from a fresh screen without clicking in first.
    bool keyPressed (const juce::KeyPress& k, juce::Component*) override { return keyPressed (k); }
    void parentHierarchyChanged() override;             // (re)register the top-level key listener

    void apiUndo();
    void apiRedo();

    /** Open a project on startup (from the command line). */
    void openProjectFile (const juce::File& f) { openAny (f); }   // .gloopy / dir / gloopy.toml / .zip

    /** Play the song once and capture the master output to a WAV, then the app
        quits itself when finished (headless offline bounce). */
    void beginRenderMode (const juce::File& out);
    bool isRenderFinished() const { return renderFinished.load(); }

    // ── gRPC control API (called from the server thread; plain types, no proto) ──
    struct TransportSnap { bool playing; double bpm; double positionBeats; bool loopEnabled; double loopStart, loopEnd; double swing; };
    struct TrackSnap { int id; juce::String name; juce::String type; float volume; float pan; bool mute; int clips; juce::String colour; bool polarity; };

    void apiPlay();
    void apiStop();
    void apiPanic();   // all-notes-off across every track (clears stuck/hanging notes)
    void apiStartRecording();
    void apiStopRecording();
    void apiSetTempo (double bpm);
    void apiSetSwing (double amount);
    bool apiSetSynthParam (int trackId, const juce::String& name, float value);
    bool applySynthParam (Track* t, const char* name, float value);   // atomic, thread-agnostic; alloc-free name (audio thread)
    void apiSeek (double beats);
    void apiSetLoop (bool enabled, double startBeat, double endBeat);
    TransportSnap apiGetTransport();
    int  apiAddSynthTrack (const juce::String& name, int wave, float a, float d, float s, float r, float g);
    bool apiSetTrackParams (int id, bool hasVol, float vol, bool hasPan, float pan,
                            bool hasMute, bool mute, bool hasSolo, bool solo,
                            bool hasName, const juce::String& name);
    std::vector<TrackSnap> apiListTracks();
    int  apiAddClip (int trackId, double start, double len, double content, bool looped,
                     const std::vector<Note>& notes, const juce::String& name);

    struct EffectSnap { int slot; juce::String name; bool bypassed; };
    struct InsertSnap { int index; juce::String name; float volume; float pan; bool mute; bool solo;
                        bool isBus; std::vector<EffectSnap> effects;
                        std::vector<std::pair<int, float>> sends; };
    struct ParamSnap  { juce::String name; float value; float min; float max; };

    std::vector<InsertSnap> apiListInserts();
    bool apiSetInsertParams (int index, bool hasVol, float vol, bool hasPan, float pan,
                             bool hasMute, bool mute, bool hasSolo, bool solo);
    bool apiSetInsertName (int index, const juce::String& name);   // rename a mixer strip
    int  apiAddEffect (int insert, int type);                 // slot, or -1
    bool apiRemoveEffect (int insert, int slot);
    bool apiSetEffectParam (int insert, int slot, const juce::String& name, float value);
    bool apiSetEffectBypass (int insert, int slot, bool bypassed);
    std::vector<ParamSnap> apiGetEffectParams (int insert, int slot);
    std::vector<float>     apiGetAnalyzerData (int insert, int slot);   // a scope/analyzer effect's live snapshot
    bool apiSnapshotMeters (std::vector<float>& L, std::vector<float>& R, std::vector<char>& clip);   // gRPC thread (try-lock)
    void clearClipIndicators();

    // Structural change events, streamed to Subscribe clients.
    struct ChangeSnap { juce::String kind; int trackId; int insert; };
    int  apiAddChangeSink();                                          // returns a sink id
    void apiPollChanges (int sinkId, std::vector<ChangeSnap>& out);   // drains one sink
    void apiRemoveChangeSink (int sinkId);
    void emitChange (const juce::String& kind, int trackId = -1, int insert = -1);

    // Parameter automation (built-in targets). type: 0=track vol, 1=track pan,
    // 2=insert vol, 3=insert pan, 4=effect param.
    struct AutoPointSnap { double beat; float value; };
    // A lane is either id-addressed (target = a ParamModel id, the unified path) or a
    // legacy tuple (type/id/slot/param). When target is non-empty it wins and the lane
    // is written through applyParamValue — the same id a controller/LFO addresses.
    struct AutoLaneSnap  { int type; int id; int slot; juce::String param; std::vector<AutoPointSnap> points; juce::String target; bool step { false }; float curve { 0.0f }; };
    bool apiSetAutomationStep (const juce::String& target, bool step);   // stepped (hold) vs linear interpolation
    bool apiGetAutomationStep (const juce::String& target);
    bool apiSetAutomationCurve (const juce::String& target, float curve);   // ease: -1 ease-out .. 0 linear .. +1 ease-in
    float apiGetAutomationCurve (const juce::String& target);
    void apiSetAutomation (int type, int id, int slot, const juce::String& param,
                           const std::vector<AutoPointSnap>& points);        // empty points = clear the lane
    void apiSetAutomationById (const juce::String& target, const std::vector<AutoPointSnap>& points);   // id-addressed lane (upsert; empty = clear)
    bool apiAddAutomationPointById (const juce::String& target, double beat, float value);              // append/replace one point on a target's lane
    std::vector<AutoLaneSnap> apiGetAutomation();
    void evaluateAutomation (double beat);   // audio thread, under engineLock
    void apiNewProject();
    std::vector<juce::String> apiListTemplates();                 // built-in + user project templates
    bool apiNewFromTemplate (const juce::String& name);           // empty the project + seed a template
    bool apiSaveAsTemplate (const juce::String& name);            // save the current project as a reusable user template
    juce::File templatesDir() const;                              // user templates dir (GLOOPY_TEMPLATE_PATH override)
    juce::File demosDir() const;                                  // bundled example compositions (GLOOPY_EXAMPLES_PATH override)
    std::vector<juce::String> listDemos() const;                  // composition-folder names under demosDir()
    juce::File samplesDir() const;                                // audio-sample folder (GLOOPY_SAMPLES_PATH override)
    std::vector<juce::String> listSamples() const;                // audio files (wav/aiff/flac) under samplesDir()

    // Favorites — a user-level library of go-to items (plugin / sample / preset / template),
    // persisted OUTSIDE the composition (like presets/templates), so they follow the user, not
    // the song. A favorite is { kind, ref, label }: kind picks the action, ref is its argument.
    struct FavoriteInfo { juce::String kind, ref, label; };
    // Instantiate a browser item by kind (shared by the Favs tab and browser drag-and-drop):
    // plugin -> instrument track, sample -> audio track, template -> seed, demo -> open, preset -> Surge.
    void dispatchBrowserItem (const juce::String& kind, const juce::String& ref, const juce::String& label);
    juce::File favoritesFile() const;                             // GLOOPY_FAVORITES_PATH override
    std::vector<FavoriteInfo> apiListFavorites() const;
    bool apiAddFavorite (const juce::String& kind, const juce::String& ref, const juce::String& label);
    bool apiRemoveFavorite (const juce::String& kind, const juce::String& ref);
    bool apiLoadProject (const juce::String& path);
    bool apiSaveProject (const juce::String& path);
    bool apiSaveComposition (const juce::String& path);   // directory "composition as repo" format
    bool apiLoadComposition (const juce::String& path);

    // --- headless CLI utilities (Cli.cpp) ---
    juce::String apiInspectJson();               // structural summary as JSON
    juce::String apiValidateJson (bool& ok, bool withLoudness = false);   // problems as JSON; ok=false if any errors
    void runMcpStdio();                          // MCP (Model Context Protocol) stdio server (Mcp.cpp)

    // --- RT diagnostics (Diagnostics.cpp) ---
    struct DiagSnap { double sampleRate; int blockSize, inputs, outputs;
                      double callbackUs, maxCallbackUs, dspLoad, renderSpeedX; long long dropouts;
                      long long audioAllocs; };   // heap allocations seen on the audio thread (proof: steady 0)
    DiagSnap apiGetDiagnostics();

    // --- offline loudness analysis (Loudness.cpp) ---
    // Peak (sample), true-peak (4x oversampled), RMS, and integrated LUFS
    // (ITU-R BS.1770 / EBU R128: K-weighting + gated mean of 400ms blocks).
    struct LoudnessReport { float peakDbfs, truePeakDbtp, rmsDbfs, lufs;
                            float momentaryLufs, shortTermLufs, lra; };   // max momentary/short-term + loudness range
    bool apiAnalyzeFile (const juce::String& path, LoudnessReport& out);   // false if unreadable

    // --- MIDI file import/export (Midi.cpp) ---
    bool apiExportMidi (const juce::String& path);   // all instrument tracks -> a Type-1 SMF
    int  apiImportMidi (const juce::String& path);   // SMF -> synth tracks + clips; count, or -1
    int  apiImportAudio (const juce::String& path);  // wav/aiff/flac -> a new audio track; track id, or -1

    // --- project notes (Notes.cpp) ---
    // Free-form markdown (arrangement notes, lyrics, TODOs, credits, licenses).
    // Stored in the composition as notes.md; travels with the song.
    juce::String apiGetProjectNotes();
    void apiSetProjectNotes (const juce::String& text);
    void openNotes();                                   // UI: a notes editor window

    // --- git project management (Git.cpp) ---
    // Version control over the composition-as-repo by shelling out to the system git
    // (juce::ChildProcess, off the audio thread). Git state lives in .git, out of band.
    struct GitFileChange { juce::String xy, path; };    // porcelain code (" M", "??") + path
    struct GitStatusSnap {
        bool available = false;                         // git binary present
        bool isRepo = false;                            // dir is inside a git work tree
        bool detached = false;                          // detached HEAD
        juce::String branch;                            // branch, or "(<short-hash>)" when detached
        int ahead = 0, behind = 0;                      // vs upstream
        juce::String dir;                               // the working dir inspected
        std::vector<GitFileChange> changes;             // dirty / untracked files
    };
    struct GitResult { bool ok = false; juce::String error; };   // for git write-ops (init/commit/...)
    // Project status summary (backs the status bar + the GetProjectStatus RPC).
    struct ProjectStatusSnap {
        juce::String version, dir, name;
        bool modified = false, untitled = true, isComposition = false;
        bool gitAvailable = false, gitRepo = false, gitDetached = false;
        juce::String gitBranch;
        int gitUncommitted = 0, gitAhead = 0, gitBehind = 0;
    };
    ProjectStatusSnap apiProjectStatus();               // one snapshot for the status bar / RPC
    bool apiGitAvailable (juce::String& version);       // true + fills version if git is on PATH
    GitStatusSnap apiGitStatus (const juce::String& dirOverride = {});   // empty = the open project's dir
    GitResult apiGitInit (const juce::String& dir);     // git init a folder (creating it if needed)
    GitResult apiGitAdd (const juce::String& dir, const juce::StringArray& paths);   // stage (empty = all)
    GitResult apiGitCommit (const juce::String& dir, const juce::String& message, bool amend);
    struct GitCommitInfo { juce::String hash; juce::StringArray parents;
                           juce::String refs, author, date, subject; };
    std::vector<GitCommitInfo> apiGitLog (const juce::String& dir, int maxCount);   // newest first
    struct GitBranches { juce::String current; juce::StringArray branches; };
    GitBranches apiGitBranches (const juce::String& dir);
    GitResult apiGitBranchCreate (const juce::String& dir, const juce::String& name, const juce::String& startPoint);
    GitResult apiGitCheckout (const juce::String& dir, const juce::String& ref);   // changes the working tree
    GitResult apiGitMerge (const juce::String& dir, const juce::String& name);     // merge <name> into current
    GitResult apiGitBranchDelete (const juce::String& dir, const juce::String& name, bool force);
    GitResult apiGitBranchRename (const juce::String& dir, const juce::String& oldName, const juce::String& newName);
    juce::StringArray apiGitTags (const juce::String& dir);
    GitResult apiGitTagCreate (const juce::String& dir, const juce::String& name, const juce::String& message);   // message empty = lightweight
    GitResult apiGitTagDelete (const juce::String& dir, const juce::String& name);
    struct GitDiffFile { juce::String status, path; };  // name-status code (M/A/D/R…) + path
    struct GitDiff { bool ok = false; juce::String error; juce::String diff;   // unified diff text
                     std::vector<GitDiffFile> files; };
    // revA+revB = between two revisions; revA only = revA..working tree; neither = working tree vs HEAD.
    GitDiff apiGitDiff (const juce::String& dir, const juce::String& pathspec,
                        const juce::String& revA, const juce::String& revB);
    GitResult apiGitDiscard (const juce::String& dir, const juce::StringArray& paths);   // empty = all tracked
    GitResult apiGitStash (const juce::String& dir, const juce::String& message);        // shelve tracked changes
    GitResult apiGitStashPop (const juce::String& dir);                                  // restore the latest stash
    juce::StringArray apiGitStashList (const juce::String& dir);                         // "stash@{0}: ..." lines
    GitResult apiGitRevert (const juce::String& dir, const juce::String& commit);        // new commit undoing <commit>
    GitResult apiGitReset (const juce::String& dir, const juce::String& mode, const juce::String& ref);   // soft|mixed|hard
    struct GitRemote { juce::String name, url; };
    GitResult apiGitAddRemote (const juce::String& dir, const juce::String& name, const juce::String& url);
    std::vector<GitRemote> apiGitListRemotes (const juce::String& dir);
    GitResult apiGitFetch (const juce::String& dir, const juce::String& remote);          // remote empty = --all
    GitResult apiGitPull (const juce::String& dir, const juce::String& remote, const juce::String& branch);
    GitResult apiGitPush (const juce::String& dir, const juce::String& remote, const juce::String& branch);   // sets upstream (-u)
    juce::StringArray apiGitConflicts (const juce::String& dir);                          // files with merge conflicts
    GitResult apiGitResolve (const juce::String& dir, const juce::String& path, const juce::String& mode);   // ours|theirs|both
    GitResult apiGitMergeContinue (const juce::String& dir);                              // finish the merge (commit)
    GitResult apiGitMergeAbort (const juce::String& dir);                                 // bail out of the merge
    struct GitIdentity { juce::String name, email; };
    GitResult apiGitSetIdentity (const juce::String& dir, const juce::String& name, const juce::String& email);
    GitIdentity apiGitGetIdentity (const juce::String& dir);                              // per-repo user.name/email
    GitResult apiGitSetAutoCommit (const juce::String& dir, bool on);                     // gloopy.autocommit in .git/config
    bool apiGitGetAutoCommit (const juce::String& dir);
    GitResult apiGitAutoCommitOnSave (const juce::String& dir);                           // stage+commit if enabled & dirty
    juce::String gitStatusReport();                     // human-readable status text (Git.cpp)
    juce::String gitHistoryReport();                    // human-readable commit log (Git.cpp)
    void openSourceControl();                           // UI: a Source Control status window (MainComponent.cpp)
    void showCommitDialog();                            // UI: stage-all + commit dialog (MainComponent.cpp)
    void openHistory();                                 // UI: a commit-history window (MainComponent.cpp)
    void showBranchMenu();                              // UI: branch popup — checkout/create/merge (MainComponent.cpp)
    void showTagMenu();                                 // UI: tag popup — tag this version / checkout (MainComponent.cpp)
    void showVersionPicker();                           // UI: Open at version — branch/tag/commit checkout (MainComponent.cpp)
    void openDiff();                                    // UI: a working-tree diff window (MainComponent.cpp)
    void showWorkingTreeMenu();                         // UI: discard / stash / revert / reset popup (MainComponent.cpp)
    void showRemoteMenu();                              // UI: remotes / fetch / pull / push popup (MainComponent.cpp)
    void showConflictMenu();                            // UI: merge-conflict resolver popup (MainComponent.cpp)
    void showGitSettings();                             // UI: identity + auto-commit dialog (MainComponent.cpp)

    // --- waveform thumbnail cache (Waveform.cpp) ---
    // Min/max peaks per bucket for an audio file, cached by path+mtime+size. Feeds
    // audio-clip / sampler display and external visualisers.
    bool apiGetWaveform (const juce::String& path, int buckets,
                         std::vector<float>& mins, std::vector<float>& maxs, double& durationSeconds);

    // --- controller mapping / MIDI-learn (Controllers.cpp) ---
    // source: "cc:<n>" MIDI CC, "osc:<name>", or any string -> a ParamModel target,
    // scaling the 0..1 input to [lo, hi].
    struct CtrlMap { juce::String source, target; float lo, hi; bool bypass { false }; };   // lo>hi = inverted
    bool apiAddControllerMap (const juce::String& source, const juce::String& target, float lo, float hi);
    bool apiRemoveControllerMap (const juce::String& source);
    std::vector<CtrlMap> apiListControllerMaps();
    bool apiSetControllerBypass (const juce::String& source, const juce::String& target, bool bypass);
    void apiSetController (const juce::String& source, float value01);   // apply mapped params (or capture if learning)
    void apiMidiLearn (const juce::String& target);                      // arm learn for the next controller ("" cancels)
    void apiAuditionNote (int pitch, float velocity, bool noteOn);        // play a note through the edited instrument (piano-roll preview)

    // --- tempo map (Tempo.cpp) ---
    // Model + exact piecewise beat<->seconds conversion. Render-path integration
    // (variable samplesPerBeat across the map) is a checkpointed follow-up.
    struct TempoMarker { gloopy::time::BeatPosition beat; double bpm; };
    bool apiAddTempoMarker (double beat, double bpm);   // upsert by beat
    bool apiRemoveTempoMarker (double beat);
    std::vector<TempoMarker> apiListTempoMarkers();
    double apiBeatsToSeconds (double beat);
    double apiSecondsToBeats (double seconds);
    double tempoAtBeat (double beat);                   // effective bpm (map or transport)
    // Time signature + bars<->beats conversion (bars/beat-in-bar are 1-based, "1.1").
    bool apiSetTimeSignature (int num, int denom);
    void apiGetTimeSignature (int& num, int& denom);
    void apiBeatsToBarBeat (double beat, int& bar, double& beatInBar);
    double apiBarBeatToBeats (int bar, double beatInBar);
    // Tempo-aware sample<->beat conversion (the foundation for the render-path
    // integration). Byte-identical to beat*spb / samples/spb when the map is empty.
    // engineLock is recursive, so these are safe to call from renderBlock.
    juce::int64                beatToSamples  (gloopy::time::BeatPosition beat);
    gloopy::time::BeatPosition samplesToBeats (juce::int64 samples);

    // --- modulation matrix (Modulation.cpp) ---
    // shape: 0 sine, 1 triangle, 2 saw, 3 square. rate in Hz. Upsert by target id.
    bool apiSetModulation (const juce::String& target, float rate, float depth, int shape, float center,
                           float syncBeats = 0.0f, float phase = 0.0f, bool unipolar = false, float slewMs = 0.0f);
    bool apiAddModulation (const juce::String& target, float rate, float depth, int shape, float center,
                           float syncBeats = 0.0f, float phase = 0.0f, bool unipolar = false, float slewMs = 0.0f);
    void resetModulationSmoothing();   // clear transient slew state (call before an offline render / on restart)
    bool apiRemoveModulation (const juce::String& target);
    struct ModSnap { juce::String target; float rate, depth, center; int shape; float syncBeats, phase; bool unipolar; float slewMs; };
    std::vector<ModSnap> apiListModulations();
    void evaluateModulation (double timeSeconds, double beatPos);   // audio thread, under engineLock
    void applyParamValue (const juce::String& id, float v); // audio-thread-safe direct write by ParamModel id

    // --- scales & microtuning (Scales.cpp) ---
    // Set by explicit intervals, or by a built-in name (major, minor, dorian,
    // pentatonic-minor, blues, whole-tone, chromatic, ...). Snap rounds each note's
    // pitch to the nearest scale degree (ties round up).
    bool apiSetScale (int root, const juce::String& name, const std::vector<int>& intervals);
    void apiGetScale (int& root, juce::String& name, std::vector<int>& intervals);
    // Microtuning: a 12-entry cents-offset-from-equal-temperament table (index = pitch class).
    bool apiSetTuning (const std::vector<double>& cents12);   // all 0 = 12-TET
    std::vector<double> apiGetTuning();
    bool apiImportScl (const juce::String& path);             // parse a Scala .scl -> tuning table
    void applyTuningToSynths();                                // push projectTuning into every synth voice
    int  apiSnapClipToScale (int trackId, int clipIndex);   // notes changed, or -1
    int  snapPitchToScale (int pitch) const;                // nearest in-scale pitch

    // --- buses & sends (Buses.cpp) ---
    int  apiAddBus (const juce::String& name);                       // append a bus mixer track; -> its index
    bool apiRemoveBus (int busIndex);                                // remove a bus + re-index sends
    bool apiSetSend (int insert, int bus, float level, bool postFader = false);   // upsert an aux send (level<=0 removes)
    bool apiSetInsertOutput (int insert, int target);                // route main output: 0=master, else a group/bus
    int  apiGroupInserts (const std::vector<int>& inserts, const juce::String& name);   // create a bus + route members into it
    bool apiUngroup (int busIndex);                                  // dissolve a group bus: reparent members, then remove it
    bool apiGatherGroup (int busIndex);                              // reorder a bus's member tracks contiguous
    // Dynamic per-track inserts: each arrangement track owns exactly one mixer insert, created and
    // removed with it (Master + track inserts + buses — no fixed pool). These keep the routing index
    // space consistent (sends, main outputs, tracks' mixerTrack). Callers hold engineLock.
    int  firstBusIndex() const;                                      // index of the first bus, or size() if none
    void insertMixerTrackAt (int pos, std::unique_ptr<MixerTrack> mt);   // insert + reindex references above it
    void removeMixerTrackAt (int pos);                               // erase + reindex references above it
    void pruneUnbackedInserts();                                     // drop legacy fixed-pool leftovers (unbacked non-bus inserts)
    void foldAllGroups (bool fold);                                  // fold/unfold every group column (session)
    void toggleFoldAllGroups();                                      // any open -> fold all, else unfold all

    // --- control groups / VCA-lite (ControlGroups.cpp) ---
    // A named group whose fader SCALES its member inserts' volumes (control scaling,
    // not audio routing). Members carry the group name on their MixerTrack.
    struct ControlGroup
    {
        juce::String       name;
        std::atomic<float> gain { 1.0f };
        std::atomic<bool>  mute { false };
        std::atomic<bool>  solo { false };   // VCA solo: soloing a group solos all its members
    };
    std::vector<std::unique_ptr<ControlGroup>> controlGroups;
    ControlGroup* findControlGroup (const juce::String& name);      // caller holds engineLock; nullptr if none

    bool apiDefineControlGroup (const juce::String& name, float gain);   // upsert a group (gain, mute=false if new)
    bool apiSetControlGroupGain (const juce::String& name, float gain);  // set group fader (0..1); false if unknown
    bool apiSetControlGroupMute (const juce::String& name, bool mute);   // group mute; false if unknown
    bool apiSetControlGroupSolo (const juce::String& name, bool solo);   // VCA solo: only soloed groups' members audible
    bool apiAssignInsertToGroup (int insert, const juce::String& group); // group="" clears; defines the group if new
    bool apiRemoveControlGroup (const juce::String& name);              // remove group + clear members
    struct ControlGroupInfo { juce::String name; float gain; bool mute; bool solo; int members; };
    std::vector<ControlGroupInfo> apiListControlGroups();

    // --- mixer scenes (MixerScenes.cpp) ---
    // Named snapshots of the mixer strip (insert vol/pan/mute/solo + effect bypass),
    // recallable. Automation stays separate. Stored in the composition.
    bool apiDefineMixerScene (const juce::String& name);   // snapshot current mixer (upsert)
    std::vector<juce::String> apiListMixerScenes();
    bool apiRecallMixerScene (const juce::String& name);
    bool apiRemoveMixerScene (const juce::String& name);

    // --- clip / region operations (ClipOps.cpp) ---
    int  apiSplitClip (int trackId, int index, double beat);        // -> new (right) clip index, or -1
    int  apiSplitClipEqual (int trackId, int index, int pieces);    // chop into N equal clips -> pieces made
    int  apiSplitClipAtMarker (int trackId, int index, const juce::String& marker);   // split at a named timeline location; -1 if no marker / outside clip
    int  apiSliceClipAtTransients (int trackId, int index, float sensitivity);   // audio clip -> slices at detected onsets; slice count, or -1
    bool apiSetClipMuted (int trackId, int index, bool muted);       // mute/enable a clip in the arrangement (MIDI or audio)
    bool apiSetClipScriptLive (int trackId, int index, bool live);   // toggle auto-generate-on-playback for a script clip
    bool apiSetLoopToClip (int trackId, int index);                  // set the transport loop to a clip's [start,end) and enable it
    bool apiSetMetronome (bool enabled);                             // toggle the beat click; -> new state
    bool apiGetMetronome();
    void  apiSetMetronomeLevel (float level);                        // click volume (0..1)
    float apiGetMetronomeLevel();
    int  apiDuplicateClip (int trackId, int index, double atBeat);  // atBeat<0 => right after; -> new index
    int  apiRepeatClip (int trackId, int index, int copies);        // tile N butted copies after the clip; -> copies added, or -1
    bool apiReverseClip (int trackId, int index);                   // reverse notes (MIDI) or audio buffer
    bool apiCropClip (int trackId, int index, double startBeat, double endBeat);   // trim a MIDI clip to a beat range
    bool apiConsolidateClip (int trackId, int index);               // flatten a looped MIDI clip's repetitions into explicit notes
    bool apiScaleClipTime (int trackId, int index, double factor);  // time-scale a MIDI clip (0.5 double-time, 2 half-time)
    int  apiBounceClip (int trackId, int index);                    // render a clip to audio on a new track (freeze); new track id or -1
    bool  apiSetClipGain (int trackId, int index, float gainDb);    // audio clip gain (dB); false if not audio
    float apiNormalizeClip (int trackId, int index, float targetDbfs); // gain so peak=target; applied gain or -1
    bool  apiSetClipFades (int trackId, int index, double fadeInBeats, double fadeOutBeats); // audio clip edges
    bool  apiSetClipFadeShape (int trackId, int index, int shape);  // 0 linear, 1 equal-power, 2 exponential
    std::vector<Note> apiGetClipNotes (int trackId, int index);     // read-back for tooling/verification
    bool apiSetClipTranspose (int trackId, int index, int semitones);   // non-destructive playback pitch offset
    bool apiSetClipVelocity (int trackId, int index, float scale);      // non-destructive playback velocity scale
    bool apiSetClipProbability (int trackId, int index, float prob);    // set every note's fire probability (0..1)
    juce::String apiExportClipNotesJson (int trackId, int index);   // clip notes -> JSON (copy / ExportNotesJSON)
    int  apiImportClipNotesJson (int trackId, double startBeat, const juce::String& json);   // JSON -> new clip
    bool apiQuantizeClip (int trackId, int index, double grid, double strength = 1.0);   // snap starts toward a beat grid
    bool apiTransposeClip (int trackId, int index, int semitones);
    bool apiHumanizeClip (int trackId, int index, double timing, double velocity);
    bool apiStrumClip (int trackId, int index, double stepBeats, bool down);   // fan out chord voices
    bool apiArpeggiateClip (int trackId, int index, double stepBeats, int mode);   // chord -> arp (0 up/1 down/2 updown)
    bool apiLegatoClip (int trackId, int index, float amount);      // stretch each note to the next onset (0..1 blend)
    bool apiRampClipVelocity (int trackId, int index, float fromVel, float toVel);   // crescendo/decrescendo across the clip
    bool apiEchoClip (int trackId, int index, double delayBeats, int repeats, float feedback);   // MIDI echo: decaying repeats
    bool apiInvertClip (int trackId, int index);                    // melodic inversion (mirror pitches around a pivot)
    bool apiRatchetClip (int trackId, int index, int subdivisions); // subdivide each note into rapid same-pitch hits
    bool apiHarmonizeClip (int trackId, int index, int semitones);  // add a parallel interval voice to each note
    bool apiSwingClip (int trackId, int index, double grid, float amount); // bake a groove: delay off-beat notes
    bool apiChordifyClip (int trackId, int index, int chordType);   // turn each note into a named chord
    bool apiRenameClip (int trackId, int index, const juce::String& name);   // rename a clip (empty -> track name)
    bool apiSetClipColour (int trackId, int index, const juce::String& hexArgb);   // per-clip colour override ("" clears)
    bool apiGateClip (int trackId, int index, double factor);       // scale note lengths (staccato/tenuto)
    bool apiFlattenClipVelocity (int trackId, int index, float value);   // set all note velocities to a value
    bool apiSplitNotesAtBeat (int trackId, int index, double beat);   // knife: cut notes spanning a clip-relative beat
    // Live (non-destructive) arpeggiator per track. mode 0 up/1 down/2 updown/3 random.
    bool apiSetTrackArp (int trackId, bool enabled, double rate, int octaves, float gate, int mode,
                         float swing, bool hold, float probability = 1.0f);
    bool apiGetTrackArp (int trackId, bool& enabled, double& rate, int& octaves, float& gate, int& mode,
                         float& swing, bool& hold, float& probability);
    void applyArpToTrack (Track& t);   // recompute clip.arpNotes from raw notes (message thread, engineLock held)
    bool apiAddChord (int trackId, int index, int root, const juce::String& type,
                      double startBeat, double lengthBeats, float velocity, int inversion);   // stamp a chord

    // Session view (clip-launch grid). Grid mutation + quantized launching; see docs/session-view.md.
    int  apiAddScene (const juce::String& name = {});          // append a scene row; returns its index
    bool apiRemoveScene (int scene);                           // remove a scene row (all tracks)
    int  sceneCount() const { return (int) scenes.size(); }
    bool apiSetSessionClip (int trackId, int scene, const Clip& clip);   // put a clip in a slot (grows scenes)
    // --- session-view control API (SessionApi.cpp): drive the clip-launch grid headlessly ---
    struct SessionTrackSnap { int trackId, playing, pending, slots; };   // playing: -1 arrangement / >=0 scene; pending: -2 none / -1 stop / >=0 scene
    struct SessionSnap { std::vector<SessionTrackSnap> tracks; int scenes; double quantumBeats; bool anyPlaying; };
    bool apiCopyClipToSessionSlot (int trackId, int clipIndex, int scene);   // populate a slot from an arrangement clip
    bool apiSetSessionSlotColour (int trackId, int scene, const juce::String& hexArgb);   // colour a slot's clip ("" = inherit)
    bool apiSessionLaunchClip  (int trackId, int scene);   // queue a clip launch (false if the slot is empty / track missing)
    bool apiSessionLaunchScene (int scene);                // queue a scene (row) launch — fires every occupied slot
    bool apiSessionStopTrack   (int trackId);              // queue a stop (back to arrangement) on one track
    bool apiSessionStopAll();                              // queue a stop on every track
    SessionSnap apiGetSessionState();                      // per-track playing/pending slot + scenes + launch quantum
    bool apiClearSessionSlot (int trackId, int scene);         // empty a slot
    bool apiLaunchClip (int trackId, int scene);               // queue-launch a slot (quantized)
    bool apiStopTrackClip (int trackId);                       // queue-stop a track (back to arrangement)
    bool apiLaunchScene (int scene);                           // queue-launch a whole row
    void apiStopAllClips();                                    // queue-stop every track
    void apiSetLaunchQuantumBeats (double beats);              // 0 = immediate
    double apiGetLaunchQuantumBeats() const { return sessionLauncher.quantumBeats(); }
    void syncSessionTrackCount();   // resize sessionLauncher to tracks.size() (engineLock held)

    // track & clip management
    bool apiRemoveTrack (int id);
    bool apiRenameTrack (int id, const juce::String& name);   // rename an existing track
    int  apiDuplicateTrack (int id);   // clone a track (clips + generator + inserts state); -> new id or -1
    bool apiSetTrackColour (int id, const juce::String& hexArgb);   // recolour a track (8-hex ARGB)
    bool apiMoveTrack (int id, int delta);   // reorder: delta<0 up (toward top), delta>0 down
    bool apiSetTrackPolarity (int id, bool invert);   // phase invert a track's contribution
    int  apiAddAudioTrack (const juce::String& name);
    int  apiAddSamplerTrack (const juce::String& name, const juce::String& path, int rootNote);
    bool apiSetSamplerControls (int trackId, float startFrac, float endFrac, bool reverse, int rootNote, float fadeIn, float fadeOut, bool loop, bool mono = false, float loopXfade = 0.0f, int interp = 0);
    struct SamplerSnap { bool ok { false }; float start { 0.0f }, end { 1.0f }; bool reverse { false }; int rootNote { 60 }; float fadeIn { 0.0f }, fadeOut { 0.0f }; bool loop { false }; bool mono { false }; float loopXfade { 0.0f }; int interp { 0 }; juce::String name; };
    SamplerSnap apiGetSamplerControls (int trackId);
    int  apiAddSfzTrack (const juce::String& name, const juce::String& path);   // native SFZ, or -1
    int  apiAddSurgeTrack (const juce::String& name, const juce::String& patch); // embedded Surge XT, or -1
    void addSurgeTrackAsync (const juce::String& patchPath, const juce::String& name);  // build off-thread + install
    std::vector<juce::String> listSurgePatches() const;   // "Category/Name" labels for the factory .fxp set
    std::map<juce::String, juce::String> browserSurgePatches;   // preset label -> full .fxp path
    juce::File resolveSamplePath (const juce::String& stored) const;            // portable path resolution
    juce::String portableSamplePath (const juce::String& absolute) const;       // inverse, for saving
    juce::StringArray sampleSearchRoots() const;
    int  apiAddPluginTrack (const juce::String& identifier);           // track id, or -1
    bool apiRemoveClip (int trackId, int index);
    // Mark a clip as a script clip and generate its notes from the language kernel (cave #9).
    bool apiRegenerateClip (int trackId, int index, const juce::String& source,
                            const juce::String& generator, const juce::String& system,
                            const juce::String& lang, juce::int64 seed, juce::String& error);
    void submitKernelResult (const juce::String& job, bool ok, std::vector<Note> notes, const juce::String& error);
    bool apiKernelPoll (const juce::String& lang, KernelHost::GenParams& params, juce::String& job);   // a kernel long-polls for a job in its language
    void apiKernelReady (int slynkPort);   // warm kernel reports its Slynk port is up (cave #15)
    bool apiStartKernelRepl (int& slynkPort, juce::String& error);   // hand back the warm kernel's Slynk port
    bool fetchKernelNotes (const KernelHost::GenParams& p, std::vector<Note>& out, juce::String& error);
    bool apiStartDriver (int trackId, int index, const juce::String& source,
                         const juce::String& generator, const juce::String& system,
                         const juce::String& lang, juce::int64 seed, juce::String& error);   // live-drive a clip (cave #12)
    void stopDriver();
    // Script-clip desktop actions (cave #10): clip context menu → edit source / regenerate.
    juce::File   projectDir() const;                 // the project's dir (composition dir, or a scratch dir while untitled)
    juce::File   resolveScriptFile (const juce::String& src) const;   // absolute -> as-is; relative -> under projectDir()
    mutable juce::File scratchProjectDir;            // per-session home for an untitled project's scripts/envs
    juce::File   scriptsDir() const;
    juce::String defaultScriptTemplate() const;
    void         launchEditor (const juce::File& f);
    static juce::File emacsPresenceFile();               // gloopy.el's presence file (M-x gloopy-connect)
    bool         openInConnectedEmacs (const juce::File& f);   // route an edit to a connected Emacs
    void         toggleClipScriptLive (int trackIdx, int clip);   // flip a clip's auto-generate-on-playback flag

    // "Live" script clips: regenerate ~1 bar before they play, so a generator you redefine
    // in Emacs is heard the next time the clip comes around (see scheduleLiveClips).
    void scheduleLiveClips (double playheadBeats);       // called from the message-thread timer
    void autoRegenScriptClip (int trackId, int clipIndex);   // async regen, no undo/dirty
    std::map<juce::int64, int> liveRegenPass;            // clip key -> pass index last regenerated
    std::set<juce::int64>      liveRegenInFlight;        // clip keys with a regen currently running
    int    livePass { 0 };                               // increments each loop/rewind
    double liveLastBeats { -1.0 };
    void         editClipScript (int trackIdx, int clip);
    void         regenerateClipScript (int trackIdx, int clip);
    void         driveClipScript (int trackIdx, int clip);
    // "Set script generator..." — point a clip at a named generator in the project's system/module.
    void         setClipGenerator (int trackIdx, int clip, const juce::String& generator,
                                   const juce::String& system, const juce::String& lang);
    bool apiMoveClip (int trackId, int index, double startBeat, bool hasToTrack, int toTrackId);
    int  apiAddAudioClip (int trackId, double startBeat, const juce::String& path, float gain);  // clip index, or -1

    // plugins
    struct PluginSnap { juce::String name; juce::String format; bool isInstrument; juce::String identifier;
                        juce::String vendor; juce::String category; juce::String version;
                        int numInputs; int numOutputs; };
    std::vector<PluginSnap> apiScanPlugins (bool force);
    std::vector<PluginSnap> apiListPlugins();
    int  apiAddPluginEffect (int insert, const juce::String& identifier);   // slot, or -1
    bool apiOpenPluginEditor (int trackId);
    bool apiRenderToFile (const juce::String& path, double tailSeconds,
                          double startBeat = 0.0, double endBeat = 0.0,
                          bool hasTrack = false, int trackId = -1);           // offline bounce / stem
    bool apiExportLoopRegion (const juce::String& path);                      // bounce the current loop window
    bool apiExportTrack (int trackId, const juce::String& path,
                         double startBeat = 0.0, double endBeat = 0.0);       // bounce one track (soloed) to a stem
    std::vector<juce::String> apiExportStems (const juce::String& dirPath,
                         double startBeat = 0.0, double endBeat = 0.0);       // bounce every instrument track to a stem

private:
    struct EditorPanel : public juce::Component
    {
        explicit EditorPanel (Transport& t) : roll (t), steps (t)
        {
            title.setJustificationType (juce::Justification::centredLeft);
            title.setFont (Palette::sectionFont());
            title.setColour (juce::Label::textColourId, Palette::textDim);
            addAndMakeVisible (title);
            stepBtn.setClickingTogglesState (true);
            pianoBtn.setClickingTogglesState (true);
            addAndMakeVisible (stepBtn);
            addAndMakeVisible (pianoBtn);

            // Swap the whole bottom area to the selected track's effect chain (owner wires onClick).
            devicesBtn.setTooltip ("Show the selected track's device chain (effects)");
            addAndMakeVisible (devicesBtn);
            addAndMakeVisible (steps);
            addChildComponent (roll);

            // Note-audition toggle: play notes through the instrument as you click / brush.
            auditionBtn.setClickingTogglesState (true);
            auditionBtn.setToggleState (true, juce::dontSendNotification);
            auditionBtn.setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            auditionBtn.setTooltip ("Play notes through the instrument as you click or brush");
            addAndMakeVisible (auditionBtn);

            // Strum: fan out chord voices (Shift-click = up-strum). Keyboard: S / Shift+S.
            strumBtn.setTooltip ("Strum chord voices (down); Shift-click for up-strum");
            strumBtn.onClick = [this] { roll.strumRollNotes (0.05, ! juce::ModifierKeys::getCurrentModifiers().isShiftDown()); };
            addAndMakeVisible (strumBtn);

            // Legato: stretch every note to the next onset (one-click; keyboard L / Shift+L half).
            legatoBtn.setTooltip ("Legato: stretch each note to the next onset (Shift-click = half)");
            legatoBtn.onClick = [this] { roll.legatoRollNotes (juce::ModifierKeys::getCurrentModifiers().isShiftDown() ? 0.5f : 1.0f); };
            addAndMakeVisible (legatoBtn);

            // Snap-to-scale: constrain drawn/moved pitches to the project scale.
            scaleLockBtn.setClickingTogglesState (true);
            scaleLockBtn.setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            scaleLockBtn.setTooltip ("Snap drawn/moved notes to the project scale");
            scaleLockBtn.onClick = [this] { roll.setSnapToScale (scaleLockBtn.getToggleState()); };
            addAndMakeVisible (scaleLockBtn);

            // Knife: click a note to split it at the click (cut through the roll).
            knifeBtn.setClickingTogglesState (true);
            knifeBtn.setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            knifeBtn.setTooltip ("Knife: click a note to split it at that beat");
            knifeBtn.onClick = [this] { roll.setKnifeMode (knifeBtn.getToggleState()); };
            addAndMakeVisible (knifeBtn);

            // Arpeggiate: chords -> sequences. Button opens an Up/Down/Up-Down menu.
            arpBtn.setTooltip ("Arpeggiate chords into a sequence");
            arpBtn.onClick = [this]
            {
                juce::PopupMenu m;
                m.addItem (1, "Arp Up");
                m.addItem (2, "Arp Down");
                m.addItem (3, "Arp Up-Down");
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (arpBtn),
                                 [this] (int r) { if (r) roll.arpeggiateRollNotes (0.25, r - 1); });
            };
            addAndMakeVisible (arpBtn);

            // Chord-stamp selector: pick a type, then click empty grid to stamp the
            // whole voicing. "Note" = ordinary single-note drawing.
            chordCombo.addItem ("Note", 1);
            const char* types[] = { "maj", "min", "7", "maj7", "min7",
                                    "sus2", "sus4", "dim", "aug", "add9", "6", "9" };
            for (int i = 0; i < (int) (sizeof (types) / sizeof (types[0])); ++i)
                chordCombo.addItem (types[i], i + 2);
            chordCombo.setSelectedId (1, juce::dontSendNotification);
            chordCombo.setTooltip ("Chord stamp: click the grid to lay down this chord");
            chordCombo.onChange = [this]
            {
                const int id = chordCombo.getSelectedId();
                roll.setChordType (id <= 1 ? juce::String()
                                           : chordCombo.getItemText (chordCombo.getSelectedItemIndex()));
            };
            addAndMakeVisible (chordCombo);
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (Palette::header);
            g.fillRect (getLocalBounds().removeFromTop (26));
            g.setColour (Palette::lineSoft);
            g.fillRect (0, 25, getWidth(), 1);
        }
        void resized() override
        {
            auto a = getLocalBounds();
            auto h = a.removeFromTop (26).reduced (0, 3);
            devicesBtn.setBounds (h.removeFromRight (68).reduced (2, 0));
            pianoBtn.setBounds (h.removeFromRight (58).reduced (2, 0));
            stepBtn .setBounds (h.removeFromRight (58).reduced (2, 0));
            chordCombo.setBounds (h.removeFromRight (74).reduced (2, 0));
            strumBtn.setBounds (h.removeFromRight (52).reduced (2, 0));
            legatoBtn.setBounds (h.removeFromRight (58).reduced (2, 0));
            arpBtn.setBounds (h.removeFromRight (46).reduced (2, 0));
            knifeBtn.setBounds (h.removeFromRight (52).reduced (2, 0));
            scaleLockBtn.setBounds (h.removeFromRight (52).reduced (2, 0));
            auditionBtn.setBounds (h.removeFromRight (72).reduced (2, 0));
            title.setBounds (h.withTrimmedLeft (10));
            roll.setBounds (a);
            steps.setBounds (a);
        }
        juce::Label      title;
        juce::TextButton stepBtn  { "STEPS" };
        juce::TextButton pianoBtn { "PIANO" };
        juce::TextButton devicesBtn { "DEVICES" };
        juce::TextButton auditionBtn { "AUDITION" };
        juce::TextButton strumBtn { "STRUM" };
        juce::TextButton legatoBtn { "LEGATO" };
        juce::TextButton arpBtn { "ARP" };
        juce::TextButton scaleLockBtn { "SCALE" };
        juce::TextButton knifeBtn { "KNIFE" };
        juce::ComboBox   chordCombo;
        PianoRoll   roll;
        StepEditor  steps;
    };

    // Keyboard-shortcut help overlay, toggled with '?'. A dimmed scrim over the whole
    // window listing the bindings; dismiss with '?', Esc, or a click. The rows are the
    // single visible source of truth for what keyPressed() handles — keep them in sync.
    struct HelpOverlay : public juce::Component
    {
        struct Row   { juce::String keys, desc; };
        struct Group { juce::String title; std::vector<Row> rows; };
        std::vector<Group> groups;
        std::function<void()> onClose;

        HelpOverlay()
        {
            setWantsKeyboardFocus (true);
            groups = {
                { "Transport", { { "Space", "Play / Stop" } } },
                { "Views",     { { "Tab", "Cycle Arrange / Session / Mixer" } } },
                { "Editing",   { { "Del / Backspace", "Delete selected clip / clear session slot" },
                                 { "Cmd/Ctrl+Z", "Undo" },
                                 { "Cmd/Ctrl+Shift+Z, Cmd/Ctrl+Y", "Redo" } } },
                { "Mixer",     { { "Cmd/Ctrl+G", "Group selected strips" },
                                 { "Cmd/Ctrl+Shift+G", "Ungroup" } } },
                { "Session",   { { "Cmd/Ctrl+G", "Group selected track columns" },
                                 { "Cmd/Ctrl+Shift+G", "Ungroup" } } },
                { "Groups",    { { "Cmd/Ctrl+Shift+F", "Fold / unfold all groups" } } },
                { "Project",   { { "Cmd/Ctrl+S", "Save project" } } },
                { "MIDI",      { { "Cmd/Ctrl+.", "Panic - all notes off" } } },
                { "Help",      { { "?", "Toggle this overlay" }, { "Esc", "Close" } } },
            };
        }
        void mouseDown (const juce::MouseEvent&) override { if (onClose) onClose(); }
        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::escapeKey || k.getTextCharacter() == '?') { if (onClose) onClose(); return true; }
            return false;
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xe0121316));   // dim scrim
            auto area = getLocalBounds().withSizeKeepingCentre (
                            juce::jmin (640, getWidth() - 40), juce::jmin (600, getHeight() - 40));
            g.setColour (Palette::panel);   g.fillRoundedRectangle (area.toFloat(), Palette::radius);
            g.setColour (Palette::line);    g.drawRoundedRectangle (area.toFloat(), Palette::radius, 1.0f);

            auto body = area.reduced (24, 20);
            g.setColour (Palette::textBright);
            g.setFont (Palette::valueFont (18.0f));
            g.drawText ("Keyboard shortcuts", body.removeFromTop (26), juce::Justification::centredLeft);
            body.removeFromTop (10);
            for (auto& grp : groups)
            {
                g.setColour (Palette::accent);  g.setFont (Palette::sectionFont());
                g.drawText (grp.title.toUpperCase(), body.removeFromTop (18), juce::Justification::centredLeft);
                for (auto& r : grp.rows)
                {
                    auto row = body.removeFromTop (20);
                    g.setColour (Palette::textDim);   g.setFont (Palette::valueFont (12.5f));
                    g.drawText (r.keys, row.removeFromLeft (230), juce::Justification::centredLeft);
                    g.setColour (Palette::text);
                    g.drawText (r.desc, row, juce::Justification::centredLeft);
                }
                body.removeFromTop (8);
            }
            g.setColour (Palette::textDim);   g.setFont (Palette::sectionFont());
            g.drawText ("Press ? or Esc to close", area.reduced (24, 14).removeFromBottom (16),
                        juce::Justification::centredRight);
        }
    };

    void timerCallback() override;

    void addTrack (std::unique_ptr<Track> track);
    void buildTemplate (const juce::String& name);          // seed a built-in template into the current project
    juce::StringArray builtinTemplateNames() const;
    juce::File findPianoSfz() const;                        // installed piano SFZ for the Piano template, if any
    void selectClip (int track, int clip);
    void selectSessionClip (int trackIndex, int scene);   // load a session slot's clip into the editor
    void writeBackEditor();
    void setEditorMode (int mode);
    void loadSelectedClipIntoEditor();
    void refreshDevicePanel();                          // point the device panel at the selected track's insert

    void setupMixer();
    void openMixer();
    void openMappings();                                // UI: the controller/LFO mapping rack
    std::unique_ptr<Effect> makeEffect (const juce::String& type);
    juce::Colour paletteColour (int index) const;

    // Plugins.
    void scanPlugins (bool force = false);
    juce::PluginDescription resolvePluginDescription (const juce::PluginDescription& saved);
    void showAddPluginMenu();
    void createInstrumentTrack (const juce::PluginDescription&);
    std::unique_ptr<Effect> makePluginEffect (const juce::PluginDescription&);
    void openPluginEditor (juce::AudioProcessor*, const juce::String& title);
    void closeAllPluginWindows();

    // Control API (OSC).
    void refreshTrackIds();
    Track* resolveTrack (int id);
    Track* trackByIndex (int i) { return (i >= 0 && i < (int) tracks.size()) ? tracks[(size_t) i].get() : nullptr; }

    // Live MIDI input (hardware + a virtual "Gloopy MIDI In" port).
    void setupMidiInputs();
    void openAvailableMidiInputs();
    void teardownMidiInputs();

    /** Run @p fn on the message thread and return its result (blocks the caller
        if invoked from another thread). Used by the gRPC api* methods. */
    template <typename Fn>
    auto callOnMessageThread (Fn&& fn) -> decltype (fn())
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            return fn();
        std::promise<decltype (fn())> prom;
        auto fut = prom.get_future();
        juce::MessageManager::callAsync ([&] { prom.set_value (fn()); });
        return fut.get();
    }

    // Project I/O.
    // Snapshot-based undo/redo (reuses toValueTree/loadFromTree).
    void pushUndoSnapshot();
    void undo();
    void redo();
    void toggleTransport();       // Space: play/stop in place
    void saveCurrentProject();    // Ctrl/Cmd+S: save to the current file (or Save As if unsaved)
    std::vector<juce::ValueTree> undoStack, redoStack;
    bool undoSuppressed { false };

    // --- Unsaved-changes tracking + status bar state ---
    bool projectModified { false };                 // edits since the last save/load
    void markModified();                            // an edit happened → flag dirty + repaint the bar
    void markSaved();                               // saved to disk → clear the flag + refresh git
    juce::String projectDisplayName() const;        // composition folder name / .gloopy file name ("" = untitled)
    juce::String projectDisplayPath() const;        // full path (~ for home) of the composition dir / .gloopy file ("" = untitled)
    void pollGitStatusAsync();                       // refresh the cached git summary off the message thread
    void paintStatusBar (juce::Graphics&);           // bottom status strip (project · unsaved · git · version)
    // Cached git working-tree summary for the status bar (updated by pollGitStatusAsync).
    bool statusGitAvailable { false }, statusGitRepo { false }, statusGitDetached { false };
    juce::String statusGitBranch;
    int statusGitUncommitted { 0 }, statusGitAhead { 0 }, statusGitBehind { 0 };
    std::atomic<bool> gitPollInFlight { false };
    juce::Rectangle<int> statusBarBounds;

    void showFileMenu();
    void openAny (const juce::File& f);   // dispatch .gloopy / composition dir / .zip
    void newProject();
    void saveProject (const juce::File&);
    void openProject (const juce::File&);
    bool saveComposition (const juce::File& dir);   // implemented in Composition.cpp
    bool saveCompositionZip (const juce::File& zipFile);   // .gloopy single-file archive (zipped composition)
    bool loadComposition (const juce::File& dir);
    juce::ValueTree toValueTree();
    void loadFromTree (const juce::ValueTree&);
    std::unique_ptr<Track> buildTrackFromTree (const juce::ValueTree& tr);   // one TRACK subtree -> a Track (no reload)
    // Clip <-> ValueTree, shared by arrangement clips and session slots (and both save formats).
    juce::ValueTree clipToTree (const Clip& c, const juce::Identifier& type = "CLIP");
    Clip clipFromTree (const juce::ValueTree& cl);   // resolves referenced audio via formatManager
    void refreshUiAfterLoad();

    GloopyLookAndFeel     lookAndFeel;
    juce::TooltipWindow   tooltipWindow { this };     // one instance so the app's setTooltip() calls render

    Transport             transport;
    bool headlessCli { false };   // CLI tools: no OSC/gRPC/audio started
    struct CachedWave { juce::int64 mtime, size; int buckets; std::vector<float> mins, maxs; double durationSeconds; };
    std::map<juce::String, CachedWave> waveformCache;   // message thread; keyed by resolved path
    juce::CriticalSection engineLock;
    std::vector<std::unique_ptr<Track>>      tracks;
    std::vector<std::unique_ptr<MixerTrack>> mixerTracks;

    // Session view (clip-launch grid). `scenes` are the global rows; each Track owns one slot
    // per scene (Track::sessionSlots). `sessionLauncher` is the quantized launch state machine;
    // `sessionBeat` is a monotonic clock (advanced while playing) that drives launch timing and
    // clip loop phase, independent of the (loopable) arrangement playhead. See docs/session-view.md.
    std::vector<Scene> scenes;
    SessionLauncher    sessionLauncher;
    double             sessionBeat { 0.0 };

    juce::AudioFormatManager formatManager;
    PluginHost pluginHost;
    double currentSampleRate { 44100.0 };
    int    currentBlockSize  { 512 };

    // RT diagnostics — written lock-free from the audio thread / offline render.
    std::atomic<double>      diagLastCallbackUs { 0.0 };
    std::atomic<double>      diagMaxCallbackUs  { 0.0 };
    std::atomic<juce::int64> diagDropouts       { 0 };
    std::atomic<double>      diagRenderSpeedX   { 0.0 };   // last offline bounce, x realtime
    juce::AudioBuffer<float> mixBuffer;
    // Reused per-instrument-track MIDI scratch in renderBlock — cleared each track, never
    // reconstructed, so the mix stays allocation-free on the audio thread (principle 4 / #24).
    juce::MidiBuffer         scratchMidi, scratchLive;
    std::vector<char>        soloImplied;   // audio-thread scratch: transitive insert-solo path (grow-only)

    int selTrack { -1 }, selClip { -1 };
    // Session-clip editing: when selSessionScene >= 0 the editor edits that track's session slot
    // (Track::sessionSlots) instead of an arrangement clip. Mutually exclusive with selTrack/selClip.
    int selSessionTrack { -1 }, selSessionScene { -1 };
    Clip* editingClip (int& outTrackIndex);   // the clip loaded in the editor (arrangement or session); caller holds engineLock
    int editorMode { 1 };   // 0 = piano roll, 1 = step grid

    // --- UI ---
    IconButton       playButton   { IconButton::Play };
    IconButton       stopButton   { IconButton::Stop };
    IconButton       recordButton { IconButton::Record };
    juce::TextButton fileButton   { "File" };
    juce::Slider     bpmSlider;
    juce::Label      bpmLabel { {}, "BPM" };
    juce::Label      posLabel;
    juce::TextButton addSynthBtn   { "+ Synth" };
    juce::TextButton loadSampleBtn { "+ Sample" };
    juce::TextButton addSfzBtn     { "+ SFZ" };
    juce::TextButton addAudioBtn   { "+ Audio" };
    juce::TextButton addPluginBtn  { "+ Plugin" };
    juce::TextButton loopButton    { "Loop" };
    juce::TextButton metroButton   { "Metro" };
    juce::TextButton panicButton   { "Panic" };
    juce::TextButton mixerButton   { "Mixer" };
    juce::TextButton mapsButton    { "Maps" };     // see + remove all controller/LFO mappings
    juce::TextButton browseButton  { juce::String::fromUTF8 ("\xe2\x98\xb0") };   // hamburger: toggle the left browser sidebar
    juce::ComboBox   scaleRootBox;                 // C..B    — project scale selector
    juce::ComboBox   scaleNameBox;                 // chromatic/major/minor/...
    void applyScaleFromToolbar();                  // reads both boxes -> apiSetScale
    void refreshScaleToolbar();                    // model -> both boxes (after load)

    juce::OwnedArray<juce::DocumentWindow> pluginWindows;
    bool pluginsScanned { false };

    // Control API.
    int nextTrackId { 1 };   // 1-based: id 0 would be omitted by proto3 (indistinguishable from "unset")

    // Long-task spinner: run heavy work off the message thread with a busy overlay so
    // the UI stays responsive (sample/SFZ loading, etc.). `heavy` runs on a pool thread;
    // `done` runs back on the message thread (safe to touch the engine) before the
    // overlay hides.
    BusyOverlay busyOverlay;

    // Script-clip kernel jobs (cave #9): a kernel generates and posts notes back via the
    // KernelSubmit RPC; apiRegenerateClip waits on the matching job id.
    struct KernelJob { std::mutex m; std::condition_variable cv; bool done { false }, ok { false };
                       std::vector<Note> notes; juce::String error; };
    std::mutex kernelJobsMutex;
    std::map<juce::String, std::shared_ptr<KernelJob>> kernelJobs;
    // The warm kernel long-polls for jobs from this queue (compile once, then instant).
    struct PendingJob { juce::String id; KernelHost::GenParams params; std::shared_ptr<KernelJob> result; };
    std::mutex jobQueueMutex;
    std::condition_variable jobQueueCv;
    std::deque<PendingJob> jobQueue;
    std::unique_ptr<juce::ChildProcess> warmKernel;    // resident generate kernel (+ Slynk, cave #15)
    std::mutex warmKernelMutex;
    void ensureWarmKernel();                           // launch it if not running (unless opted out)
    std::atomic<int> kernelSlynkPort { 0 };            // warm kernel's Slynk port, 0 until it reports ready
    void checkWarmKernelHealth();                      // clear a stale indicator + respawn if the kernel died
    juce::uint32 lastKernelSpawnMs { 0 };              // throttles respawns (message thread)
    // Resident Python kernel (gloopy._serve): the Python twin of the warm SBCL kernel, so Python
    // clips generate headlessly. Attach-to-live: a notebook attaches to this exact process.
    std::unique_ptr<juce::ChildProcess> warmPyKernel;
    std::mutex warmPyKernelMutex;
    juce::uint32 lastPyKernelSpawnMs { 0 };            // throttles respawns
    void ensureWarmPythonKernel();                     // launch it if not running (unless opted out)
    juce::File pyKernelConnFile() const;               // Jupyter connection file the notebook attaches to
    std::mutex kernelReadyMutex;
    std::condition_variable kernelReadyCv;
    void writeKernelDiscoveryFile (int slynkPort);     // ~/.cache/gloopy/kernel.json for gloopy.el (Sly)
    static juce::File kernelDiscoveryFile();
    std::thread driverThread;                          // live-driver playback thread (cave #12)
    std::atomic<bool> driverStop { false };
    juce::ThreadPool bgPool { 1 };
    void runBackground (const juce::String& label,
                        std::function<void()> heavy, std::function<void()> done);
    // Show the busy overlay, then run message-thread-bound `work` after it has painted, so
    // a long load shows the "Loading…" indicator rather than a frozen UI. Prefer
    // runBackground() when the work can move off the message thread (that also animates).
    void showBusyThen (const juce::String& label, std::function<void()> work);
    std::unordered_map<int, Track*> idMap;
    juce::CriticalSection idMapLock;
    std::unique_ptr<OscControl> osc;
    std::unique_ptr<GrpcServer> grpc;

    // Live MIDI input: the instrument track id that receives played notes.
    std::atomic<int> midiInputTarget { -1 };
    std::atomic<int> firstInstrumentId { -1 };   // fallback when nothing is selected
    // Live-MIDI input activity, for the track-header LED: the last note-on time (ms) and the
    // track that received it. Written on the MIDI thread, read on the message thread.
    std::atomic<double> midiActivityMs     { -1.0e12 };
    std::atomic<int>    midiActivityTrackId { -1 };
    std::atomic<bool>   panicRequested { false };   // MIDI panic; the audio thread clears stuck notes

    struct ChangeSink { std::mutex m; std::vector<ChangeSnap> pending; };
    std::map<int, std::shared_ptr<ChangeSink>> changeSinks;
    std::mutex changeSinksLock;
    int nextSinkId { 0 };

    std::vector<AutoLaneSnap> automationLanes;   // guarded by engineLock
    struct TimelineLocation { juce::String name, kind; gloopy::time::BeatPosition startBeat, endBeat; };  // typed positions (Time.h); double at the API/wire edges
    std::vector<TimelineLocation> locations;     // guarded by engineLock
    struct ExportProfile { juce::String name, target, rangeName, format; int trackId; double tailSeconds; };
    std::vector<ExportProfile> exportProfiles;   // guarded by engineLock
    struct MixerScene
    {
        juce::String name;
        struct Insert { float volume { 0.8f }, pan { 0.0f }; bool mute { false }, solo { false };
                        std::vector<char> bypass; std::vector<std::pair<int, float>> sends; };   // sends: (bus, level)
        std::vector<Insert> inserts;
    };
    std::vector<MixerScene> mixerScenes;         // guarded by engineLock
    // Project scale (for piano-roll highlighting, snap-to-scale, generative clients).
    // Default chromatic = all 12 pitch classes, so snap is a no-op until a scale is set.
    int scaleRoot { 0 };                          // 0=C .. 11=B
    juce::String scaleName { "chromatic" };
    std::vector<int> scaleIntervals { 0,1,2,3,4,5,6,7,8,9,10,11 };   // semitone offsets from root
    std::array<double, 12> projectTuning {};      // microtuning: cents offset from ET per pitch class (all 0 = 12-TET)
    // Modulation matrix: LFO sources that drive a ParamModel target each block.
    // value = center + depth * osc(rate * t). One LFO per target (upsert by target).
    struct Mod { juce::String target; float rate { 1.0f }, depth { 0.0f }, center { 0.0f }; int shape { 0 };
                 float syncBeats { 0.0f };      // >0: cycle length in beats (tempo-synced); 0: free-running Hz
                 float phase { 0.0f };          // cycle offset 0..1 (shifts the waveform start)
                 bool  unipolar { false };      // false: center ± depth; true: center .. center+depth (one-sided)
                 float slewMs { 0.0f };         // >0: one-pole slew (ms time constant) softening value changes
                 float smoothState { 0.0f };    // transient: last smoothed value (not serialised)
                 bool  smoothInit { false }; }; // transient: seeded on the first block after a (re)start
    std::vector<Mod> modulations;                 // guarded by engineLock
    // Tempo map: sorted tempo markers {beat, bpm}. Empty => constant transport.bpm
    // (behaviour unchanged). Drives the beat<->seconds helpers; the render path does
    // NOT yet vary tempo across the map (see the checkpoint in the grind skill).
    // (TempoMarker struct is declared with its api methods, above.)
    std::vector<TempoMarker> tempoMap;            // guarded by engineLock, sorted by beat
    // Controller mappings (CtrlMap declared with its api methods, above).
    std::vector<CtrlMap> controllerMaps;          // guarded by engineLock
    juce::String learnTarget;                     // non-empty => next controller binds to it

    // MIDI recording: audio thread appends played input, message thread drains to a clip.
    void startRecording();
    void finalizeRecording();
    void startSessionRecord (int trackIndex, int scene);   // record live MIDI into a session slot
    int  sessionRecTrack { -1 }, sessionRecScene { -1 };   // slot currently recording (-1 = none)

    // "Arrangement Record from Session": while armed, the audio thread logs each session clip's
    // played span (start/end arrangement beat) as it launches/stops; finalize drops those spans
    // onto the arrangement timeline as clips (Ableton's capture-a-session-jam workflow).
    void startSessionCapture();
    void finalizeSessionCapture();
    bool sessionCapturing() const { return sessionCapture.load(); }
    std::atomic<bool> sessionCapture { false };
    struct CaptureSeg { int track; int scene; double startBeat; double endBeat; };
    std::vector<CaptureSeg> captureSegs;                   // preallocated FIFO (audio thread writes)
    std::atomic<int>        captureWrite { 0 };
    std::vector<int>        capturePrevSlot;               // audio-thread: last playing slot per track
    std::vector<double>     captureStartBeat;              // audio-thread: current span start per track
    double                  captureLastBeat { 0.0 };       // audio-thread: latest arrangement beat
    struct RecordedEvent { juce::int64 sample; juce::MidiMessage msg; };
    std::vector<RecordedEvent> recordBuffer;         // preallocated in prepareToPlay
    std::atomic<int>  recordWrite { 0 };
    std::atomic<bool> recording { false };
    std::atomic<int>  recordTrackId { -1 };
    juce::int64       recordStartSample { 0 };

    // --- audio input recording (Recording.cpp) ---
    struct TakeRecorder
    {
        int trackId = -1, channels = 2, input = 0;
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
        std::atomic<int> frames { 0 };
        juce::File file;
        double startBeat = 0.0;
    };
    bool startAudioRecording();
    void stopAudioRecording();
    void captureRecordingInput (const juce::AudioSourceChannelInfo&);   // audio thread
    void addMonitoring (const juce::AudioSourceChannelInfo&);           // audio thread
    void finalizeTake (int trackId, const juce::File& take, double startBeat, bool muted = false);
    juce::File recordingsDir() const;
    juce::TimeSliceThread recordThread { "gloopy-record" };
    juce::CriticalSection takeWriterLock;
    std::vector<std::unique_ptr<TakeRecorder>> takeRecorders;   // guarded by takeWriterLock
    std::atomic<bool>   audioRecActive   { false };
    std::atomic<double> recordTestToneHz { 0.0 };   // >0 injects a test tone (self-test)
    double  recordTonePhase   { 0.0 };
    double  audioRecRate      { 44100.0 };
    juce::AudioBuffer<float> monitorStash;          // input copy for monitoring
    // punch / count-in
    std::atomic<bool>   punchEnabled { false };
    std::atomic<double> punchInBeat  { 0.0 };
    std::atomic<double> punchOutBeat { 1.0e12 };
    std::atomic<double> countInBeats { 0.0 };

    // Metronome: a monitor click at each beat (audio-thread state; touched only in
    // renderBlock, except the atomic enable flag). Not serialised (a session toggle).
    std::atomic<bool> metronomeEnabled { false };
    std::atomic<float> metronomeLevel { 1.0f };   // click volume (0..1); a session setting
    int    metroSamplesLeft { 0 };
    double metroPhase { 0.0 }, metroInc { 0.0 };
    float  metroAmp { 0.0f };
    // Phase 3: format, latency, loop recording
    std::atomic<int>    recordFormat        { 0 };   // 0 = WAV, 1 = FLAC
    std::atomic<double> recordLatencyOffset { 0.0 }; // manual, seconds (added to device latency)
    std::atomic<bool>   loopRecRotate       { false };// audio thread -> message thread: loop wrapped
    double lastRecPlayheadBeat { -1.0 };
    juce::File rawTakesDir() const;
    double recordLatencySeconds() const;
    void   rotateLoopTakes();
  public:
    // --- presets (Presets.cpp) ---
    juce::File presetsDir (const juce::String& category) const;
    std::vector<juce::String> apiListPresets (const juce::String& category);
    bool apiSaveSynthPreset (int trackId, const juce::String& name);
    bool apiLoadSynthPreset (int trackId, const juce::String& name);
    bool apiSaveInstrumentPreset (int trackId, const juce::String& name);   // synth or SFZ
    bool apiLoadInstrumentPreset (int trackId, const juce::String& name);
    bool apiSaveEffectPreset (int insert, const juce::String& name);
    bool apiLoadEffectPreset (int insert, const juce::String& name);

    // --- universal parameter model (Parameters.cpp) ---
    // One flat, stable, string-addressed view of every automatable value, so UI
    // knobs, automation, MIDI/OSC mapping, gRPC state, and modulation all speak the
    // same ids. Canonical id grammar:
    //   track/<id>/{volume|pan|mute|solo}          insert/<index>/{volume|pan|mute|solo}
    //   track/<id>/synth/<name>                     effect/<insert>/<slot>/<paramName>
    struct ParamDesc { juce::String id, name, unit, scaling; float value, min, max, def; };
    std::vector<ParamDesc> apiListParameters();
    bool apiGetParameter (const juce::String& id, ParamDesc& out);   // false if unknown
    bool apiSetParameter (const juce::String& id, float value);      // false if unknown/rejected
    bool apiSetParameterNormalized (const juce::String& id, float pos01);   // set from a 0..1 knob position, honouring the param's scaling
    std::vector<ParamDesc> apiSnapshotParameters();   // non-plugin param id->value snapshot for the composition manifest

    // --- timeline locations (Locations.cpp) ---
    // Named points and ranges on the timeline (markers, sections, loop/punch/export
    // ranges). Stored in the composition; render/export can target a range by name.
    // kind: "marker" | "range" | "section" | "loop" | "punch" | "export" | "skip".
    // (TimelineLocation struct is declared with the `locations` member, below.)
    bool apiAddLocation (const juce::String& name, const juce::String& kind,
                         double startBeat, double endBeat);            // upsert by name
    std::vector<TimelineLocation> apiListLocations();
    bool apiRemoveLocation (const juce::String& name);
    bool apiResolveRange (const juce::String& name, double& startBeat, double& endBeat);  // range/section lookup

    // --- export profiles (Exports.cpp) ---
    // Named render targets stored in the composition. target: "mix" (whole song),
    // "range" (+ rangeName), "track" (+ trackId), "stems" (one file per instrument
    // track). Output goes to <project>/exports/ with deterministic filenames.
    bool apiDefineExportProfile (const juce::String& name, const juce::String& target,
                                 const juce::String& rangeName, const juce::String& format,
                                 int trackId, double tailSeconds);   // upsert by name
    std::vector<ExportProfile> apiListExportProfiles();
    bool apiRemoveExportProfile (const juce::String& name);
    bool apiRunExport (const juce::String& name, const juce::String& outDirOverride,
                       std::vector<juce::String>& filesOut);     // false if unknown/failed

    std::vector<juce::String> apiListAudioInputs();
    std::vector<juce::String> apiListMidiInputs();   // live MIDI sources Gloopy is listening to
    bool apiArmTrack (int trackId, bool armed, int input, int channels, bool monitor);
    bool apiSetPunchRange (bool enabled, double inBeat, double outBeat, double countIn);
    bool apiSetRecordSettings (int format, double latencyOffsetSeconds);
    bool apiPromoteTake (const juce::String& takeId);
    int  apiCleanupTakes();
    int  apiRecoverTakes();
    void apiSetRecordTestTone (double hz) { recordTestToneHz.store (hz); }
  private:
    juce::StringArray openMidiInputs;
    std::unique_ptr<juce::MidiInput> virtualMidiIn;
    juce::MidiDeviceListConnection midiListConnection;   // hot-plug notifications

    juce::Viewport   arrangeViewport;
    std::unique_ptr<ArrangeView> arrangeView;
    std::unique_ptr<SessionPane> sessionPane;         // frozen scene/master column (left) + scrolling track grid
    juce::Viewport   mixerViewport;                   // embedded Mixer (a view, not a floating window)
    enum class ViewMode { Arrange, Session, Mixer };
    ViewMode viewMode { ViewMode::Arrange };
    void cycleView();                                 // Tab: Arrange -> Session -> Mixer -> Arrange
    void setViewMode (ViewMode m);                    // switch directly (toolbar buttons)
    void applyViewMode();                             // show/hide the three embedded views
    juce::Component::SafePointer<juce::Component> keyListenerHost;   // top-level we listen to for Tab
    EditorPanel      editorPanel { transport };
    DevicePanel      devicePanel;                    // bottom-area effect chain for the selected track
    HelpOverlay      helpOverlay;                    // '?' keyboard-shortcut overlay (covers the window)
    void             toggleHelpOverlay();
    enum class BottomMode { Clip, Devices };
    BottomMode       bottomMode { BottomMode::Clip };
    int              deviceTrack { -1 };             // track whose device chain the panel shows

    std::unique_ptr<MixerView>            mixerView;
    std::unique_ptr<BrowserSidebar>       browser;          // collapsible left browser (templates, ...)
    bool                                  browserVisible { false };
    std::map<juce::String, juce::String>  browserPluginIds; // Plugins tab: row label -> plugin identifier
    std::vector<FavoriteInfo>             browserFavorites;  // Favorites tab: row label -> favorite (for dispatch)
    std::unique_ptr<juce::DocumentWindow> mixerWindow;
    juce::String projectNotes;                          // free-form markdown (message thread)
    std::unique_ptr<juce::DocumentWindow> notesWindow;
    std::unique_ptr<juce::DocumentWindow> sourceControlWindow;   // git status readout (Git.cpp)
    juce::TextEditor sourceControlEditor;
    std::unique_ptr<juce::DocumentWindow> historyWindow;         // git commit log (Git.cpp)
    juce::TextEditor historyEditor;
    std::unique_ptr<juce::DocumentWindow> diffWindow;            // git working-tree diff (Git.cpp)
    juce::TextEditor diffEditor;
    MappingsView mappingsView;
    std::unique_ptr<juce::DocumentWindow> mappingsWindow;
    juce::TextEditor notesEditor;
    std::unique_ptr<juce::Component> commitDialog;               // custom modal commit dialog (MainComponent.cpp)
    std::unique_ptr<juce::Component> formDialog;                 // reusable styled prompt for the git dialogs
    void presentModal (std::unique_ptr<juce::Component>& slot, juce::Component* dlg);   // add as full-window modal overlay

    // Detachable device-chain windows (Reaper/Ardour-style): each hosts a DevicePanel pinned to one
    // insert, so several tracks'/groups' effect controls can be seen at once. Bound by MixerTrack*
    // (stable across reindexing); pruned when their insert is removed.
    juce::OwnedArray<juce::DocumentWindow> deviceWindows;
    void openDeviceWindow (int insert);                        // pop a floating device chain for this insert
    int  indexOfMixerTrack (const MixerTrack* mt) const;       // current index of a MixerTrack, or -1 if gone
    void pruneDeviceWindows();                                 // close windows whose insert no longer exists

    juce::StretchableLayoutManager verticalLayout;
    std::unique_ptr<juce::StretchableLayoutResizerBar> dividerBar;

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::File currentProjectFile;
    juce::Rectangle<int> toolbarBounds, transportBounds, displayBounds;

    // Offline render.
    std::atomic<bool> renderMode { false };
    std::atomic<bool> renderFinished { false };
    std::unique_ptr<juce::AudioFormatWriter> renderWriter;
    juce::File  renderFile;
    juce::int64 renderTarget { 0 }, renderWritten { 0 }, renderSongLen { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
