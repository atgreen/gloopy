#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include "Transport.h"
#include "Track.h"
#include "Clip.h"
#include "ArrangeView.h"
#include "PianoRoll.h"
#include "IconButton.h"
#include "MixerTrack.h"
#include "MixerView.h"
#include "Effects.h"
#include "Palette.h"
#include "GloopyLookAndFeel.h"

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

private:
    struct EditorPanel : public juce::Component
    {
        explicit EditorPanel (Transport& t) : roll (t)
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

    void addTrack (std::unique_ptr<Track> track);
    void setupDefaultProject();
    void selectClip (int track, int clip);
    void writeBackEditor();

    void setupMixer();
    void openMixer();
    std::unique_ptr<Effect> makeEffect (const juce::String& type);
    juce::Colour paletteColour (int index) const;

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
    double currentSampleRate { 44100.0 };
    int    currentBlockSize  { 512 };
    juce::AudioBuffer<float> mixBuffer;

    int selTrack { -1 }, selClip { -1 };

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
    juce::TextButton mixerButton   { "Mixer" };

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
