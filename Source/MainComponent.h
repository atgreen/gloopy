#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include "Transport.h"
#include "Pattern.h"
#include "Channel.h"
#include "ChannelRackView.h"
#include "PianoRoll.h"
#include "PlaylistClip.h"
#include "PlaylistView.h"
#include "MixerTrack.h"
#include "MixerView.h"
#include "Effects.h"
#include "Palette.h"
#include "GloopyLookAndFeel.h"

/** M2 workspace: channel rack + step sequencer, multiple patterns, and a
    per-channel piano roll. The step grid and piano roll edit the same note
    lists in the currently selected pattern. */
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

private:
    /** Bottom panel: a title bar + the piano roll for the selected channel. */
    struct PianoPanel : public juce::Component
    {
        explicit PianoPanel (Transport& t) : roll (t)
        {
            title.setJustificationType (juce::Justification::centredLeft);
            title.setFont (Palette::sectionFont());
            title.setColour (juce::Label::textColourId, Palette::textDim);
            addAndMakeVisible (title);
            addAndMakeVisible (roll);
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (Palette::header);
            g.fillRect (getLocalBounds().removeFromTop (24));
            g.setColour (Palette::line);
            g.fillRect (0, 23, getWidth(), 1);
        }
        void resized() override
        {
            auto a = getLocalBounds();
            title.setBounds (a.removeFromTop (24).withTrimmedLeft (10));
            roll.setBounds (a);
        }
        juce::Label title;
        PianoRoll   roll;
    };

    void timerCallback() override;

    void addChannel (std::unique_ptr<Channel> channel);
    void setupDefaultProject();

    void addPattern();
    void selectPattern (int index);
    void refreshPatternBox();
    Pattern& curPattern();
    void toggleSongMode();
    juce::Colour paletteColour (int index) const;
    void setupMixer();
    void openMixer();
    std::unique_ptr<Effect> makeEffect (const juce::String& type);

    // Project I/O (M5).
    void showFileMenu();
    void newProject();
    void saveProject (const juce::File& file);
    void openProject (const juce::File& file);
    juce::ValueTree toValueTree();
    void loadFromTree (const juce::ValueTree& root);
    void refreshUiAfterLoad();

    void openPianoRollFor (int channel);
    void writeBackPianoRoll();

    GloopyLookAndFeel     lookAndFeel;      // declared first → destroyed last

    // --- engine / model ---
    Transport             transport;
    juce::CriticalSection engineLock;                 // guards channels + patterns + index
    std::vector<std::unique_ptr<Channel>>    channels;
    std::vector<std::unique_ptr<Pattern>>    patterns;
    std::vector<PlaylistClip>                clips;
    std::vector<std::unique_ptr<MixerTrack>> mixerTracks;   // [0] = master
    int                   currentPatternIndex { 0 };

    juce::AudioFormatManager formatManager;
    double currentSampleRate { 44100.0 };
    int    currentBlockSize  { 512 };
    juce::AudioBuffer<float> mixBuffer;

    int pianoRollChannel { -1 };

    // --- UI ---
    juce::TextButton playButton    { "Play" };
    juce::TextButton stopButton    { "Stop" };
    juce::TextButton addSynthBtn   { "+ Synth" };
    juce::TextButton loadSampleBtn { "+ Sample" };
    juce::TextButton clearButton   { "Clear" };
    juce::Slider     bpmSlider;
    juce::Label      bpmLabel { {}, "BPM" };
    juce::Label      posLabel;

    juce::TextButton fileButton    { "File" };
    juce::ComboBox   patternBox;
    juce::TextButton addPatternBtn { "+ Pat" };
    juce::TextButton modeButton    { "Pattern" };
    juce::TextButton mixerButton   { "Mixer" };

    juce::File currentProjectFile;
    juce::Rectangle<int> displayBounds;    // inset transport readout, drawn in paint()
    juce::Rectangle<int> toolbarBounds;

    juce::Viewport   rackViewport;
    std::unique_ptr<ChannelRackView> rackView;

    PianoPanel pianoPanel { transport };

    std::unique_ptr<PlaylistView> playlistView;

    std::unique_ptr<MixerView>       mixerView;
    std::unique_ptr<juce::DocumentWindow> mixerWindow;

    juce::StretchableLayoutManager verticalLayout;
    std::unique_ptr<juce::StretchableLayoutResizerBar> dividerBar;
    std::unique_ptr<juce::StretchableLayoutResizerBar> dividerBar2;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
