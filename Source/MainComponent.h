// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include "Transport.h"
#include "Track.h"
#include "FileDrop.h"
#include "Clip.h"
#include "ArrangeView.h"
#include "BusyOverlay.h"
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
                      public juce::FileDragAndDropTarget,
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
                        bool isBus; std::vector<EffectSnap> effects;
                        std::vector<std::pair<int, float>> sends; };
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
    std::vector<juce::String> apiListTemplates();                 // built-in project templates
    bool apiNewFromTemplate (const juce::String& name);           // empty the project + seed a template
    bool apiLoadProject (const juce::String& path);
    bool apiSaveProject (const juce::String& path);
    bool apiSaveComposition (const juce::String& path);   // directory "composition as repo" format
    bool apiLoadComposition (const juce::String& path);

    // --- headless CLI utilities (Cli.cpp) ---
    juce::String apiInspectJson();               // structural summary as JSON
    juce::String apiValidateJson (bool& ok, bool withLoudness = false);   // problems as JSON; ok=false if any errors

    // --- RT diagnostics (Diagnostics.cpp) ---
    struct DiagSnap { double sampleRate; int blockSize, inputs, outputs;
                      double callbackUs, maxCallbackUs, dspLoad, renderSpeedX; long long dropouts; };
    DiagSnap apiGetDiagnostics();

    // --- offline loudness analysis (Loudness.cpp) ---
    // Peak (sample), true-peak (4x oversampled), RMS, and integrated LUFS
    // (ITU-R BS.1770 / EBU R128: K-weighting + gated mean of 400ms blocks).
    struct LoudnessReport { float peakDbfs, truePeakDbtp, rmsDbfs, lufs; };
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

    // --- waveform thumbnail cache (Waveform.cpp) ---
    // Min/max peaks per bucket for an audio file, cached by path+mtime+size. Feeds
    // audio-clip / sampler display and external visualisers.
    bool apiGetWaveform (const juce::String& path, int buckets,
                         std::vector<float>& mins, std::vector<float>& maxs, double& durationSeconds);

    // --- controller mapping / MIDI-learn (Controllers.cpp) ---
    // source: "cc:<n>" MIDI CC, "osc:<name>", or any string -> a ParamModel target,
    // scaling the 0..1 input to [lo, hi].
    struct CtrlMap { juce::String source, target; float lo, hi; };
    bool apiAddControllerMap (const juce::String& source, const juce::String& target, float lo, float hi);
    bool apiRemoveControllerMap (const juce::String& source);
    std::vector<CtrlMap> apiListControllerMaps();
    void apiSetController (const juce::String& source, float value01);   // apply mapped params (or capture if learning)
    void apiMidiLearn (const juce::String& target);                      // arm learn for the next controller ("" cancels)
    void apiAuditionNote (int pitch, float velocity, bool noteOn);        // play a note through the edited instrument (piano-roll preview)

    // --- tempo map (Tempo.cpp) ---
    // Model + exact piecewise beat<->seconds conversion. Render-path integration
    // (variable samplesPerBeat across the map) is a checkpointed follow-up.
    struct TempoMarker { double beat, bpm; };
    bool apiAddTempoMarker (double beat, double bpm);   // upsert by beat
    bool apiRemoveTempoMarker (double beat);
    std::vector<TempoMarker> apiListTempoMarkers();
    double apiBeatsToSeconds (double beat);
    double apiSecondsToBeats (double seconds);
    double tempoAtBeat (double beat);                   // effective bpm (map or transport)
    // Tempo-aware sample<->beat conversion (the foundation for the render-path
    // integration). Byte-identical to beat*spb / samples/spb when the map is empty.
    // engineLock is recursive, so these are safe to call from renderBlock.
    juce::int64 beatToSamples (double beat);
    double      samplesToBeats (juce::int64 samples);

    // --- modulation matrix (Modulation.cpp) ---
    // shape: 0 sine, 1 triangle, 2 saw, 3 square. rate in Hz. Upsert by target id.
    bool apiSetModulation (const juce::String& target, float rate, float depth, int shape, float center);
    bool apiRemoveModulation (const juce::String& target);
    struct ModSnap { juce::String target; float rate, depth, center; int shape; };
    std::vector<ModSnap> apiListModulations();
    void evaluateModulation (double timeSeconds);          // audio thread, under engineLock
    void applyParamValue (const juce::String& id, float v); // audio-thread-safe direct write by ParamModel id

    // --- scales & microtuning (Scales.cpp) ---
    // Set by explicit intervals, or by a built-in name (major, minor, dorian,
    // pentatonic-minor, blues, whole-tone, chromatic, ...). Snap rounds each note's
    // pitch to the nearest scale degree (ties round up).
    bool apiSetScale (int root, const juce::String& name, const std::vector<int>& intervals);
    void apiGetScale (int& root, juce::String& name, std::vector<int>& intervals);
    int  apiSnapClipToScale (int trackId, int clipIndex);   // notes changed, or -1
    int  snapPitchToScale (int pitch) const;                // nearest in-scale pitch

    // --- buses & sends (Buses.cpp) ---
    int  apiAddBus (const juce::String& name);                       // append a bus mixer track; -> its index
    bool apiRemoveBus (int busIndex);                                // remove a bus + re-index sends
    bool apiSetSend (int insert, int bus, float level);             // upsert an aux send (level<=0 removes)

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
    bool  apiSetClipGain (int trackId, int index, float gainDb);    // audio clip gain (dB); false if not audio
    float apiNormalizeClip (int trackId, int index, float targetDbfs); // gain so peak=target; applied gain or -1
    bool  apiSetClipFades (int trackId, int index, double fadeInBeats, double fadeOutBeats); // audio clip edges
    std::vector<Note> apiGetClipNotes (int trackId, int index);     // read-back for tooling/verification
    bool apiQuantizeClip (int trackId, int index, double grid);     // snap starts to a beat grid
    bool apiTransposeClip (int trackId, int index, int semitones);
    bool apiHumanizeClip (int trackId, int index, double timing, double velocity);
    bool apiStrumClip (int trackId, int index, double stepBeats, bool down);   // fan out chord voices
    bool apiArpeggiateClip (int trackId, int index, double stepBeats, int mode);   // chord -> arp (0 up/1 down/2 updown)
    // Live (non-destructive) arpeggiator per track. mode 0 up/1 down/2 updown/3 random.
    bool apiSetTrackArp (int trackId, bool enabled, double rate, int octaves, float gate, int mode,
                         float swing, bool hold);
    bool apiGetTrackArp (int trackId, bool& enabled, double& rate, int& octaves, float& gate, int& mode,
                         float& swing, bool& hold);
    void applyArpToTrack (Track& t);   // recompute clip.arpNotes from raw notes (message thread, engineLock held)
    bool apiAddChord (int trackId, int index, int root, const juce::String& type,
                      double startBeat, double lengthBeats, float velocity, int inversion);   // stamp a chord

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

            // Snap-to-scale: constrain drawn/moved pitches to the project scale.
            scaleLockBtn.setClickingTogglesState (true);
            scaleLockBtn.setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            scaleLockBtn.setTooltip ("Snap drawn/moved notes to the project scale");
            scaleLockBtn.onClick = [this] { roll.setSnapToScale (scaleLockBtn.getToggleState()); };
            addAndMakeVisible (scaleLockBtn);

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
            g.setColour (Palette::line);
            g.fillRect (0, 25, getWidth(), 1);
        }
        void resized() override
        {
            auto a = getLocalBounds();
            auto h = a.removeFromTop (26).reduced (0, 3);
            pianoBtn.setBounds (h.removeFromRight (58).reduced (2, 0));
            stepBtn .setBounds (h.removeFromRight (58).reduced (2, 0));
            chordCombo.setBounds (h.removeFromRight (74).reduced (2, 0));
            strumBtn.setBounds (h.removeFromRight (52).reduced (2, 0));
            arpBtn.setBounds (h.removeFromRight (46).reduced (2, 0));
            scaleLockBtn.setBounds (h.removeFromRight (52).reduced (2, 0));
            auditionBtn.setBounds (h.removeFromRight (72).reduced (2, 0));
            title.setBounds (h.withTrimmedLeft (10));
            roll.setBounds (a);
            steps.setBounds (a);
        }
        juce::Label      title;
        juce::TextButton stepBtn  { "STEPS" };
        juce::TextButton pianoBtn { "PIANO" };
        juce::TextButton auditionBtn { "AUDITION" };
        juce::TextButton strumBtn { "STRUM" };
        juce::TextButton arpBtn { "ARP" };
        juce::TextButton scaleLockBtn { "SCALE" };
        juce::ComboBox   chordCombo;
        PianoRoll   roll;
        StepEditor  steps;
    };

    void timerCallback() override;

    void addTrack (std::unique_ptr<Track> track);
    void buildTemplate (const juce::String& name);          // seed a built-in template into the current project
    juce::StringArray builtinTemplateNames() const;
    juce::File findPianoSfz() const;                        // installed piano SFZ for the Piano template, if any
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
    struct CachedWave { juce::int64 mtime, size; int buckets; std::vector<float> mins, maxs; double durationSeconds; };
    std::map<juce::String, CachedWave> waveformCache;   // message thread; keyed by resolved path
    juce::CriticalSection engineLock;
    std::vector<std::unique_ptr<Track>>      tracks;
    std::vector<std::unique_ptr<MixerTrack>> mixerTracks;

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
    juce::ThreadPool bgPool { 1 };
    void runBackground (const juce::String& label,
                        std::function<void()> heavy, std::function<void()> done);
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
    // Modulation matrix: LFO sources that drive a ParamModel target each block.
    // value = center + depth * osc(rate * t). One LFO per target (upsert by target).
    struct Mod { juce::String target; float rate { 1.0f }, depth { 0.0f }, center { 0.0f }; int shape { 0 }; };
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
    juce::String projectNotes;                          // free-form markdown (message thread)
    std::unique_ptr<juce::DocumentWindow> notesWindow;
    juce::TextEditor notesEditor;

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
