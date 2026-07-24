// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: GPL-3.0-only

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
    MainComponent();
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
    void openProjectFile (const juce::File& f) { if (f.existsAsFile()) openProject (f); }

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
    void apiSeek (double beats);
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
    void apiNewProject();
    bool apiLoadProject (const juce::String& path);
    bool apiSaveProject (const juce::String& path);

    // track & clip management
    bool apiRemoveTrack (int id);
    int  apiAddAudioTrack (const juce::String& name);
    int  apiAddSamplerTrack (const juce::String& name, const juce::String& path, int rootNote);
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
    void newProject();
    void saveProject (const juce::File&);
    void openProject (const juce::File&);
    juce::ValueTree toValueTree();
    void loadFromTree (const juce::ValueTree&);
    void refreshUiAfterLoad();

    GloopyLookAndFeel     lookAndFeel;

    Transport             transport;
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

    // MIDI recording: audio thread appends played input, message thread drains to a clip.
    void startRecording();
    void finalizeRecording();
    struct RecordedEvent { juce::int64 sample; juce::MidiMessage msg; };
    std::vector<RecordedEvent> recordBuffer;         // preallocated in prepareToPlay
    std::atomic<int>  recordWrite { 0 };
    std::atomic<bool> recording { false };
    std::atomic<int>  recordTrackId { -1 };
    juce::int64       recordStartSample { 0 };
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
