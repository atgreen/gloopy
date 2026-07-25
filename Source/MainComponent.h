// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include "Transport.h"
#include "Track.h"
#include "Clip.h"
#include "ArrangeView.h"
#include "PianoRoll.h"
#include "StepEditor.h"
#include "IconButton.h"
#include "MixerTrack.h"
#include "MixerView.h"
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
#include <mutex>
#include <future>

/** Linear-arranger workspace: an arrangement of instrument tracks (each owning
    its clips), a clip editor (piano roll), and the mixer. */
class MainComponent : public juce::AudioAppComponent,
                      public juce::MidiInputCallback,
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

    /** Mix one block (transport → tracks → inserts → master → @p outBuf) and
        return the song length in samples. Shared by the live callback and the
        offline renderer; caller must hold the engine lock. */
    juce::int64 renderBlock (juce::AudioBuffer<float>& outBuf, int start, int num, bool ignoreLoopWindow);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;   // Ctrl+Z / Ctrl+Shift+Z

    void apiUndo();
    void apiRedo();

    /** Open a project on startup (from the command line). */
    void openProjectFile (const juce::File& f) { openAny (f); }   // .gloopy / dir / gloopy.toml / .zip

    /** Play the song once and capture the master output to a WAV, then the app
        quits itself when finished (headless offline bounce). */
    void beginRenderMode (const juce::File& out);
    bool isRenderFinished() const { return renderFinished.load(); }

    // ── gRPC control API (called from the server thread; plain types, no proto) ──
    struct TransportSnap { bool playing; double bpm; double positionBeats; };
    struct TrackSnap { int id; juce::String name; juce::String type; float volume; float pan; bool mute; int clips; };

    void apiPlay();
    void apiStop();
    void apiStartRecording();
    void apiStopRecording();
    void apiSetTempo (double bpm);
    void apiSetSwing (double amount);
    bool apiSetSynthParam (int trackId, const juce::String& name, float value);
    bool applySynthParam (Track* t, const juce::String& name, float value);   // atomic, thread-agnostic
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
                        std::vector<EffectSnap> effects; };
    struct ParamSnap  { juce::String name; float value; float min; float max; };

    std::vector<InsertSnap> apiListInserts();
    bool apiSetInsertParams (int index, bool hasVol, float vol, bool hasPan, float pan,
                             bool hasMute, bool mute, bool hasSolo, bool solo);
    int  apiAddEffect (int insert, int type);                 // slot, or -1
    bool apiRemoveEffect (int insert, int slot);
    bool apiSetEffectParam (int insert, int slot, const juce::String& name, float value);
    bool apiSetEffectBypass (int insert, int slot, bool bypassed);
    std::vector<ParamSnap> apiGetEffectParams (int insert, int slot);
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
    struct AutoLaneSnap  { int type; int id; int slot; juce::String param; std::vector<AutoPointSnap> points; };
    void apiSetAutomation (int type, int id, int slot, const juce::String& param,
                           const std::vector<AutoPointSnap>& points);        // empty points = clear the lane
    std::vector<AutoLaneSnap> apiGetAutomation();
    void evaluateAutomation (double beat);   // audio thread, under engineLock
    void apiNewProject();
    bool apiLoadProject (const juce::String& path);
    bool apiSaveProject (const juce::String& path);
    bool apiSaveComposition (const juce::String& path);   // directory "composition as repo" format
    bool apiLoadComposition (const juce::String& path);

    // --- headless CLI utilities (Cli.cpp) ---
    juce::String apiInspectJson();               // structural summary as JSON
    juce::String apiValidateJson (bool& ok);     // problems as JSON; ok=false if any errors

    // --- MIDI file import/export (Midi.cpp) ---
    bool apiExportMidi (const juce::String& path);   // all instrument tracks -> a Type-1 SMF
    int  apiImportMidi (const juce::String& path);   // SMF -> synth tracks + clips; count, or -1

    // --- mixer scenes (MixerScenes.cpp) ---
    // Named snapshots of the mixer strip (insert vol/pan/mute/solo + effect bypass),
    // recallable. Automation stays separate. Stored in the composition.
    bool apiDefineMixerScene (const juce::String& name);   // snapshot current mixer (upsert)
    std::vector<juce::String> apiListMixerScenes();
    bool apiRecallMixerScene (const juce::String& name);
    bool apiRemoveMixerScene (const juce::String& name);

    // --- clip / region operations (ClipOps.cpp) ---
    int  apiSplitClip (int trackId, int index, double beat);        // -> new (right) clip index, or -1
    int  apiDuplicateClip (int trackId, int index, double atBeat);  // atBeat<0 => right after; -> new index
    bool apiReverseClip (int trackId, int index);                   // reverse notes (MIDI) or audio buffer
    std::vector<Note> apiGetClipNotes (int trackId, int index);     // read-back for tooling/verification

    // track & clip management
    bool apiRemoveTrack (int id);
    int  apiAddAudioTrack (const juce::String& name);
    int  apiAddSamplerTrack (const juce::String& name, const juce::String& path, int rootNote);
    int  apiAddSfzTrack (const juce::String& name, const juce::String& path);   // native SFZ, or -1
    juce::File resolveSamplePath (const juce::String& stored) const;            // portable path resolution
    juce::String portableSamplePath (const juce::String& absolute) const;       // inverse, for saving
    juce::StringArray sampleSearchRoots() const;
    int  apiAddPluginTrack (const juce::String& identifier);           // track id, or -1
    bool apiRemoveClip (int trackId, int index);
    bool apiMoveClip (int trackId, int index, double startBeat, bool hasToTrack, int toTrackId);
    int  apiAddAudioClip (int trackId, double startBeat, const juce::String& path, float gain);  // clip index, or -1

    // plugins
    struct PluginSnap { juce::String name; juce::String format; bool isInstrument; juce::String identifier; };
    std::vector<PluginSnap> apiScanPlugins (bool force);
    std::vector<PluginSnap> apiListPlugins();
    int  apiAddPluginEffect (int insert, const juce::String& identifier);   // slot, or -1
    bool apiOpenPluginEditor (int trackId);
    bool apiRenderToFile (const juce::String& path, double tailSeconds,
                          double startBeat = 0.0, double endBeat = 0.0,
                          bool hasTrack = false, int trackId = -1);           // offline bounce / stem

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
            addAndMakeVisible (steps);
            addChildComponent (roll);
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (Palette::header);
            g.fillRect (getLocalBounds().removeFromTop (26));
            g.setColour (Palette::line);
            g.fillRect (0, 25, getWidth(), 1);
        }
        void resized() override
        {
            auto a = getLocalBounds();
            auto h = a.removeFromTop (26).reduced (0, 3);
            pianoBtn.setBounds (h.removeFromRight (58).reduced (2, 0));
            stepBtn .setBounds (h.removeFromRight (58).reduced (2, 0));
            title.setBounds (h.withTrimmedLeft (10));
            roll.setBounds (a);
            steps.setBounds (a);
        }
        juce::Label      title;
        juce::TextButton stepBtn  { "STEPS" };
        juce::TextButton pianoBtn { "PIANO" };
        PianoRoll   roll;
        StepEditor  steps;
    };

    void timerCallback() override;

    void addTrack (std::unique_ptr<Track> track);
    void setupDefaultProject();
    void selectClip (int track, int clip);
    void writeBackEditor();
    void setEditorMode (int mode);
    void loadSelectedClipIntoEditor();

    void setupMixer();
    void openMixer();
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
    std::vector<juce::ValueTree> undoStack, redoStack;
    bool undoSuppressed { false };

    void showFileMenu();
    void openAny (const juce::File& f);   // dispatch .gloopy / composition dir / .zip
    void newProject();
    void saveProject (const juce::File&);
    void openProject (const juce::File&);
    bool saveComposition (const juce::File& dir);   // implemented in Composition.cpp
    bool loadComposition (const juce::File& dir);
    juce::ValueTree toValueTree();
    void loadFromTree (const juce::ValueTree&);
    void refreshUiAfterLoad();

    GloopyLookAndFeel     lookAndFeel;

    Transport             transport;
    bool headlessCli { false };   // CLI tools: no OSC/gRPC/audio started
    juce::CriticalSection engineLock;
    std::vector<std::unique_ptr<Track>>      tracks;
    std::vector<std::unique_ptr<MixerTrack>> mixerTracks;

    juce::AudioFormatManager formatManager;
    PluginHost pluginHost;
    double currentSampleRate { 44100.0 };
    int    currentBlockSize  { 512 };
    juce::AudioBuffer<float> mixBuffer;

    int selTrack { -1 }, selClip { -1 };
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
    juce::TextButton mixerButton   { "Mixer" };

    juce::OwnedArray<juce::DocumentWindow> pluginWindows;
    bool pluginsScanned { false };

    // Control API.
    int nextTrackId { 0 };
    std::unordered_map<int, Track*> idMap;
    juce::CriticalSection idMapLock;
    std::unique_ptr<OscControl> osc;
    std::unique_ptr<GrpcServer> grpc;

    // Live MIDI input: the instrument track id that receives played notes.
    std::atomic<int> midiInputTarget { -1 };
    std::atomic<int> firstInstrumentId { -1 };   // fallback when nothing is selected

    struct ChangeSink { std::mutex m; std::vector<ChangeSnap> pending; };
    std::map<int, std::shared_ptr<ChangeSink>> changeSinks;
    std::mutex changeSinksLock;
    int nextSinkId { 0 };

    std::vector<AutoLaneSnap> automationLanes;   // guarded by engineLock
    struct TimelineLocation { juce::String name, kind; double startBeat, endBeat; };
    std::vector<TimelineLocation> locations;     // guarded by engineLock
    struct ExportProfile { juce::String name, target, rangeName, format; int trackId; double tailSeconds; };
    std::vector<ExportProfile> exportProfiles;   // guarded by engineLock
    struct MixerScene
    {
        juce::String name;
        struct Insert { float volume { 0.8f }, pan { 0.0f }; bool mute { false }, solo { false }; std::vector<char> bypass; };
        std::vector<Insert> inserts;
    };
    std::vector<MixerScene> mixerScenes;         // guarded by engineLock

    // MIDI recording: audio thread appends played input, message thread drains to a clip.
    void startRecording();
    void finalizeRecording();
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
    EditorPanel      editorPanel { transport };

    std::unique_ptr<MixerView>            mixerView;
    std::unique_ptr<juce::DocumentWindow> mixerWindow;

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
