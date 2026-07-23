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
#include "Palette.h"
#include "GloopyLookAndFeel.h"
#include <unordered_map>

/** Linear-arranger workspace: an arrangement of instrument tracks (each owning
    its clips), a clip editor (piano roll), and the mixer. */
class MainComponent : public juce::AudioAppComponent,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;
    void releaseResources() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Open a project on startup (from the command line). */
    void openProjectFile (const juce::File& f) { if (f.existsAsFile()) openProject (f); }

    /** Play the song once and capture the master output to a WAV, then the app
        quits itself when finished (headless offline bounce). */
    void beginRenderMode (const juce::File& out);
    bool isRenderFinished() const { return renderFinished.load(); }

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
    void showAddPluginMenu();
    void createInstrumentTrack (const juce::PluginDescription&);
    std::unique_ptr<Effect> makePluginEffect (const juce::PluginDescription&);
    void openPluginEditor (juce::AudioProcessor*, const juce::String& title);
    void closeAllPluginWindows();

    // Control API (OSC).
    void refreshTrackIds();
    Track* resolveTrack (int id);

    // Project I/O.
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
