#include "MainComponent.h"
#include "NoteScheduler.h"
#include "Sampler.h"
#include "SynthGenerator.h"
#include "DrumSynth.h"
#include <array>
#include <cmath>

MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);
    juce::Desktop::getInstance().setDefaultLookAndFeel (&lookAndFeel);
    formatManager.registerBasicFormats();

    transport.setBpm (128.0);

    // ---- transport (icon) ----
    addAndMakeVisible (playButton);
    playButton.setClickingTogglesState (true);
    playButton.onClick = [this]
    {
        const bool playing = playButton.getToggleState();
        transport.setPlaying (playing);
        playButton.setIcon (playing ? IconButton::Pause : IconButton::Play);
    };

    addAndMakeVisible (stopButton);
    stopButton.onClick = [this]
    {
        transport.setPlaying (false);
        transport.requestReset();
        playButton.setToggleState (false, juce::dontSendNotification);
        playButton.setIcon (IconButton::Play);
    };

    addAndMakeVisible (recordButton);
    recordButton.setTooltip ("Recording — coming soon");

    addAndMakeVisible (fileButton);
    fileButton.onClick = [this] { showFileMenu(); };

    // ---- tempo / position ----
    addAndMakeVisible (bpmSlider);
    bpmSlider.setSliderStyle (juce::Slider::IncDecButtons);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 52, 22);
    bpmSlider.setRange (40.0, 260.0, 1.0);
    bpmSlider.setValue (transport.getBpm(), juce::dontSendNotification);
    bpmSlider.onValueChange = [this] { transport.setBpm (bpmSlider.getValue()); };

    addAndMakeVisible (posLabel);
    posLabel.setJustificationType (juce::Justification::centred);
    posLabel.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    posLabel.setColour (juce::Label::textColourId, Palette::accent);

    // ---- add tracks ----
    addAndMakeVisible (addSynthBtn);
    addSynthBtn.onClick = [this]
    {
        addTrack (std::make_unique<Track> ("Synth",
                      std::make_unique<SynthGenerator>(), 48,
                      paletteColour ((int) tracks.size())));
    };

    addAndMakeVisible (loadSampleBtn);
    loadSampleBtn.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Load a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (! file.existsAsFile()) return;
                auto sampler = std::make_unique<Sampler>();
                sampler->prepare (currentSampleRate, currentBlockSize);
                if (sampler->loadFile (file, formatManager))
                    addTrack (std::make_unique<Track> (file.getFileNameWithoutExtension(),
                                  std::move (sampler), 60, paletteColour ((int) tracks.size())));
            });
    };

    addAndMakeVisible (mixerButton);
    mixerButton.onClick = [this] { openMixer(); };

    // ---- arrange view ----
    arrangeView = std::make_unique<ArrangeView> (tracks, transport, engineLock);
    arrangeView->onClipSelected = [this] (int t, int c) { selectClip (t, c); };
    arrangeView->onChanged      = [this] { if (arrangeView) arrangeView->repaint(); };
    arrangeViewport.setViewedComponent (arrangeView.get(), false);
    arrangeViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (arrangeViewport);

    // ---- clip editor ----
    addAndMakeVisible (editorPanel);
    editorPanel.roll.setShowPlayhead (false);
    editorPanel.roll.setEnabledEditing (false);
    editorPanel.title.setText ("EDITOR", juce::dontSendNotification);
    editorPanel.roll.onNotesChanged = [this] { writeBackEditor(); };

    verticalLayout.setItemLayout (0, 120.0, -0.85, -0.60);   // arrangement
    verticalLayout.setItemLayout (1, 6.0, 6.0, 6.0);         // divider
    verticalLayout.setItemLayout (2, 90.0, -0.85, -0.40);    // editor
    dividerBar = std::make_unique<juce::StretchableLayoutResizerBar> (&verticalLayout, 1, false);
    addAndMakeVisible (*dividerBar);

    setupMixer();
    mixerView = std::make_unique<MixerView> (mixerTracks, engineLock,
                    [this] (const juce::String& t) { return makeEffect (t); });

    setupDefaultProject();

    setSize (1180, 820);
    setAudioChannels (0, 2);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    mixerWindow = nullptr;
    juce::Desktop::getInstance().setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
    shutdownAudio();
}

juce::Colour MainComponent::paletteColour (int index) const
{
    static const juce::uint32 palette[] = {
        0xffe0663a, 0xffd9a441, 0xff56b26b, 0xffb266d9,
        0xff4a90d9, 0xff3ab0c4, 0xffd94a7a, 0xff8bbf3a
    };
    return juce::Colour (palette[(size_t) (index % (int) juce::numElementsInArray (palette))]);
}

void MainComponent::addTrack (std::unique_ptr<Track> track)
{
    track->generator->prepare (currentSampleRate, currentBlockSize);
    {
        const juce::ScopedLock sl (engineLock);
        track->mixerTrack.store (juce::jmin ((int) tracks.size() + 1, (int) mixerTracks.size() - 1));
        tracks.push_back (std::move (track));
    }
    if (arrangeView) arrangeView->rebuild();
    resized();
}

void MainComponent::setupDefaultProject()
{
    auto makeSamplerTrack = [this] (const juce::String& name,
                                    juce::AudioBuffer<float> buf, juce::Colour col)
    {
        auto sampler = std::make_unique<Sampler>();
        sampler->prepare (currentSampleRate, currentBlockSize);
        sampler->setSample (std::move (buf), DrumSynth::kRate, name);
        addTrack (std::make_unique<Track> (name, std::move (sampler), 60, col));
    };

    makeSamplerTrack ("Kick",  DrumSynth::makeKick(),  juce::Colours::orangered);
    makeSamplerTrack ("Snare", DrumSynth::makeSnare(), juce::Colours::gold);
    makeSamplerTrack ("Hat",   DrumSynth::makeHat(),   juce::Colours::aquamarine);
    makeSamplerTrack ("Clap",  DrumSynth::makeClap(),  juce::Colours::violet);

    auto bass = std::make_unique<SynthGenerator>();
    bass->engine.params.waveform.store (1);
    bass->engine.params.release.store (0.15f);
    addTrack (std::make_unique<Track> ("Bass", std::move (bass), 36, juce::Colours::skyblue));

    // Seed a 2-bar clip on each track containing a 1-bar looping groove.
    const juce::ScopedLock sl (engineLock);
    auto seed = [this] (int ti, std::initializer_list<int> steps)
    {
        if (! juce::isPositiveAndBelow (ti, (int) tracks.size())) return;
        Clip c;
        c.startBeat = 0.0; c.lengthBeats = 8.0; c.contentLenBeats = 4.0; c.looped = true;
        c.name = tracks[(size_t) ti]->name;
        for (int s : steps)
            c.notes.push_back ({ tracks[(size_t) ti]->defaultPitch, s * 0.25, 0.25, 0.85f });
        tracks[(size_t) ti]->clips.push_back (std::move (c));
    };
    seed (0, { 0, 4, 8, 12 });
    seed (1, { 4, 12 });
    seed (2, { 0, 2, 4, 6, 8, 10, 12, 14 });
    seed (3, { 8 });
    seed (4, { 0, 3, 6, 8, 11, 14 });
}

void MainComponent::selectClip (int track, int clip)
{
    selTrack = track; selClip = clip;
    if (arrangeView) arrangeView->setSelection (track, clip);

    bool valid = false;
    {
        const juce::ScopedLock sl (engineLock);
        if (juce::isPositiveAndBelow (track, (int) tracks.size())
              && juce::isPositiveAndBelow (clip, (int) tracks[(size_t) track]->clips.size()))
        {
            const auto& c = tracks[(size_t) track]->clips[(size_t) clip];
            editorPanel.roll.setLength (c.looped ? c.contentLenBeats : c.lengthBeats);
            editorPanel.roll.loadNotes (c.notes);
            valid = true;
        }
    }

    editorPanel.roll.setEnabledEditing (valid);
    if (valid)
        editorPanel.title.setText ("EDITOR   \xe2\x80\xa2   " + tracks[(size_t) track]->name.toUpperCase()
                                    + "  \xe2\x80\xa2  CLIP", juce::dontSendNotification);
    else
        editorPanel.title.setText ("EDITOR", juce::dontSendNotification);
}

void MainComponent::writeBackEditor()
{
    {
        const juce::ScopedLock sl (engineLock);
        if (juce::isPositiveAndBelow (selTrack, (int) tracks.size())
              && juce::isPositiveAndBelow (selClip, (int) tracks[(size_t) selTrack]->clips.size()))
            tracks[(size_t) selTrack]->clips[(size_t) selClip].notes = editorPanel.roll.getNotes();
    }
    if (arrangeView) arrangeView->repaint();
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlockExpected;
    transport.prepare (sampleRate);

    const juce::ScopedLock sl (engineLock);
    mixBuffer.setSize (2, juce::jmax (16, samplesPerBlockExpected));
    for (auto& t : tracks)
        t->generator->prepare (sampleRate, samplesPerBlockExpected);
    for (auto& mt : mixerTracks)
    {
        mt->buffer.setSize (2, juce::jmax (16, samplesPerBlockExpected));
        for (auto& fx : mt->effects)
            fx->prepare (sampleRate, samplesPerBlockExpected, 2);
    }
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();
    auto* out = info.buffer;
    const int start = info.startSample;
    const int num   = info.numSamples;

    const juce::ScopedTryLock stl (engineLock);
    if (! stl.isLocked())
        return;

    if (transport.consumeReset())
    {
        transport.setPlayheadSamples (0);
        for (auto& t : tracks) t->generator->allNotesOff();
    }

    const bool   playing = transport.isPlaying();
    const double spb     = transport.samplesPerBeat();

    // Song length = furthest clip end (min 1 bar).
    double songBeats = 4.0;
    for (auto& t : tracks)
        for (auto& c : t->clips)
            songBeats = juce::jmax (songBeats, c.endBeat());
    juce::int64 loopLen = (juce::int64) std::llround (songBeats * spb);
    if (loopLen < 1) loopLen = 1;

    // Split the block into song-loop segments.
    struct Seg { juce::int64 loopStart; int chunk; int tsOffset; bool wrap; };
    std::array<Seg, 16> segs;
    int nseg = 0;
    if (playing)
    {
        juce::int64 ph = transport.getPlayheadSamples();
        int rem = num, local = 0;
        while (rem > 0 && nseg < (int) segs.size())
        {
            if (ph >= loopLen) ph = 0;
            const int chunk = (int) juce::jmin ((juce::int64) rem, loopLen - ph);
            const bool wrap = (ph + chunk >= loopLen);
            segs[(size_t) nseg++] = { ph, chunk, local, wrap };
            ph += chunk; if (ph >= loopLen) ph = 0;
            rem -= chunk; local += chunk;
        }
        transport.setPlayheadSamples (ph);
    }

    // Collect a clip's notes over a song-sample window (content loops to fill).
    auto collectClip = [spb] (juce::MidiBuffer& midi, const Clip& clip,
                              juce::int64 songStart, int chunk, int tsOffset)
    {
        const juce::int64 songEnd  = songStart + chunk;
        const juce::int64 clipStart = (juce::int64) std::llround (clip.startBeat * spb);
        const juce::int64 clipEnd   = (juce::int64) std::llround (clip.endBeat() * spb);
        const double repBeats = clip.looped ? clip.contentLenBeats : clip.lengthBeats;
        juce::int64 repUnit = (juce::int64) std::llround (repBeats * spb);
        if (repUnit < 1) repUnit = clipEnd - clipStart;
        if (repUnit < 1) return;

        const juce::int64 lo = juce::jmax (songStart, clipStart);
        const juce::int64 hi = juce::jmin (songEnd, clipEnd);
        if (lo >= hi) return;

        juce::int64 repStart = clipStart + ((lo - clipStart) / repUnit) * repUnit;
        for (; repStart < hi; repStart += repUnit)
        {
            const juce::int64 winLo = juce::jmax (lo, repStart);
            const juce::int64 winHi = juce::jmin (hi, repStart + repUnit);
            if (winLo < winHi)
                collectNotes (clip.notes, midi, winLo - repStart, (int) (winHi - winLo),
                              tsOffset + (int) (winLo - songStart), spb);
        }
    };

    bool anySolo = false;
    for (auto& t : tracks) if (t->solo.load()) { anySolo = true; break; }

    const int numTracks = (int) mixerTracks.size();
    if (mixBuffer.getNumSamples() < num) mixBuffer.setSize (2, num, false, false, true);
    for (auto& mt : mixerTracks)
    {
        if (mt->buffer.getNumSamples() < num) mt->buffer.setSize (2, num, false, false, true);
        mt->buffer.clear();
    }

    // --- each track -> its mixer insert ---
    for (auto& t : tracks)
    {
        juce::MidiBuffer midi;
        if (playing)
        {
            for (int s = 0; s < nseg; ++s)
            {
                for (auto& c : t->clips)
                    collectClip (midi, c, segs[(size_t) s].loopStart,
                                 segs[(size_t) s].chunk, segs[(size_t) s].tsOffset);
                if (segs[(size_t) s].wrap)
                    midi.addEvent (juce::MidiMessage::allNotesOff (1),
                                   segs[(size_t) s].tsOffset + segs[(size_t) s].chunk);
            }
        }

        mixBuffer.clear();
        t->generator->render (mixBuffer, midi, 0, num);

        const bool audible = ! t->mute.load() && (! anySolo || t->solo.load());
        if (! audible) continue;

        const float v = t->volume.load();
        const float pan = t->pan.load();
        const float theta = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        const int route = juce::jlimit (0, numTracks - 1, t->mixerTrack.load());
        auto& dst = mixerTracks[(size_t) route]->buffer;
        dst.addFrom (0, 0, mixBuffer, 0, 0, num, v * std::cos (theta));
        dst.addFrom (1, 0, mixBuffer, juce::jmin (1, mixBuffer.getNumChannels() - 1), 0, num, v * std::sin (theta));
    }

    auto subView = [num] (juce::AudioBuffer<float>& b)
    {
        return juce::AudioBuffer<float> (b.getArrayOfWritePointers(), b.getNumChannels(), 0, num);
    };

    // --- inserts -> master ---
    bool anyTrackSolo = false;
    for (int ti = 1; ti < numTracks; ++ti)
        if (mixerTracks[(size_t) ti]->solo.load()) { anyTrackSolo = true; break; }

    MixerTrack& master = *mixerTracks[0];
    for (int ti = 1; ti < numTracks; ++ti)
    {
        MixerTrack& mt = *mixerTracks[(size_t) ti];
        { auto sub = subView (mt.buffer); for (auto& fx : mt.effects) fx->process (sub); }
        mt.peakL.store (mt.buffer.getMagnitude (0, 0, num));
        mt.peakR.store (mt.buffer.getMagnitude (1, 0, num));

        const bool audible = ! mt.mute.load() && (! anyTrackSolo || mt.solo.load());
        if (! audible) continue;
        const float v = mt.volume.load();
        const float pan = mt.pan.load();
        const float theta = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        master.buffer.addFrom (0, 0, mt.buffer, 0, 0, num, v * std::cos (theta));
        master.buffer.addFrom (1, 0, mt.buffer, 1, 0, num, v * std::sin (theta));
    }

    // --- master -> output ---
    { auto sub = subView (master.buffer); for (auto& fx : master.effects) fx->process (sub); }
    master.peakL.store (master.buffer.getMagnitude (0, 0, num));
    master.peakR.store (master.buffer.getMagnitude (1, 0, num));
    const float mv = master.volume.load();
    if (out->getNumChannels() > 0) out->addFrom (0, start, master.buffer, 0, 0, num, mv);
    if (out->getNumChannels() > 1) out->addFrom (1, start, master.buffer, 1, 0, num, mv);
}

void MainComponent::releaseResources() {}

// ---------------------------------------------------------------------------
// Mixer
// ---------------------------------------------------------------------------
void MainComponent::setupMixer()
{
    const juce::ScopedLock sl (engineLock);
    mixerTracks.clear();
    mixerTracks.push_back (std::make_unique<MixerTrack> ("Master"));
    mixerTracks[0]->volume.store (0.9f);
    for (int i = 1; i <= 8; ++i)
        mixerTracks.push_back (std::make_unique<MixerTrack> ("Ins " + juce::String (i)));
}

std::unique_ptr<Effect> MainComponent::makeEffect (const juce::String& type)
{
    auto fx = EffectFactory::create (type);
    if (fx != nullptr) fx->prepare (currentSampleRate, currentBlockSize, 2);
    return fx;
}

namespace
{
    struct HideOnCloseWindow : public juce::DocumentWindow
    {
        HideOnCloseWindow()
            : DocumentWindow ("Mixer", Palette::bg, juce::DocumentWindow::closeButton)
        { setUsingNativeTitleBar (true); }
        void closeButtonPressed() override { setVisible (false); }
    };
}

void MainComponent::openMixer()
{
    if (mixerWindow == nullptr)
    {
        auto w = std::make_unique<HideOnCloseWindow>();
        w->setContentNonOwned (mixerView.get(), false);
        w->setResizable (true, false);
        w->setSize (900, 540);
        w->centreWithSize (900, 540);
        mixerWindow = std::move (w);
    }
    mixerWindow->setVisible (true);
    mixerWindow->toFront (true);
}

// ---------------------------------------------------------------------------
// GUI
// ---------------------------------------------------------------------------
void MainComponent::timerCallback()
{
    const double beats = transport.getPlayheadBeats();
    const int bar  = (int) (beats / 4.0) + 1;
    const int beat = (int) std::fmod (beats, 4.0) + 1;
    const int tick = (int) (std::fmod (beats, 1.0) * 100.0);
    posLabel.setText (juce::String::formatted ("%d . %d . %02d", bar, beat, tick),
                      juce::dontSendNotification);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Palette::bg);

    g.setColour (Palette::panel);
    g.fillRect (toolbarBounds);
    g.setColour (Palette::line);
    g.fillRect (toolbarBounds.getX(), toolbarBounds.getBottom() - 1, toolbarBounds.getWidth(), 1);

    // Wordmark.
    g.setColour (Palette::accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, (float) toolbarBounds.getCentreY() - 9.0f, 4.0f, 18.0f), 2.0f);
    g.setColour (Palette::textBright);
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)).withExtraKerningFactor (0.15f));
    g.drawText ("GLOOPY", 20, toolbarBounds.getY(), 92, toolbarBounds.getHeight(),
                juce::Justification::centredLeft, false);

    // Transport cluster inset.
    if (! transportBounds.isEmpty())
    {
        g.setColour (Palette::inset);
        g.fillRoundedRectangle (transportBounds.toFloat(), 5.0f);
    }

    // Tempo / position display.
    if (! displayBounds.isEmpty())
    {
        g.setColour (Palette::inset);
        g.fillRoundedRectangle (displayBounds.toFloat(), 4.0f);
        g.setColour (Palette::line);
        g.drawRoundedRectangle (displayBounds.toFloat().reduced (0.5f), 4.0f, 1.0f);
        const int half = displayBounds.getWidth() / 2;
        g.setColour (Palette::line);
        g.fillRect (displayBounds.getX() + half, displayBounds.getY() + 5, 1, displayBounds.getHeight() - 10);
        g.setColour (Palette::textDim);
        g.setFont (Palette::sectionFont());
        g.drawText ("TEMPO", displayBounds.getX() + 8, displayBounds.getY() + 3, half - 12, 12,
                    juce::Justification::centredLeft, false);
        g.drawText ("POSITION", displayBounds.getX() + half + 8, displayBounds.getY() + 3, half - 12, 12,
                    juce::Justification::centredLeft, false);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    toolbarBounds = area.removeFromTop (56);
    auto bar = toolbarBounds.reduced (8, 9);
    bar.removeFromLeft (104);   // wordmark

    fileButton.setBounds (bar.removeFromLeft (52)); bar.removeFromLeft (12);

    // Transport cluster.
    transportBounds = bar.removeFromLeft (128).expanded (0, 1);
    {
        auto t = transportBounds.reduced (4, 2);
        const int bw = (t.getWidth() - 8) / 3;
        playButton  .setBounds (t.removeFromLeft (bw)); t.removeFromLeft (4);
        stopButton  .setBounds (t.removeFromLeft (bw)); t.removeFromLeft (4);
        recordButton.setBounds (t.removeFromLeft (bw));
    }
    bar.removeFromLeft (12);

    displayBounds = bar.removeFromLeft (206).expanded (0, 1);
    {
        auto d = displayBounds.reduced (6, 0).withTrimmedTop (15);
        bpmSlider.setBounds (d.removeFromLeft (92).reduced (2, 3));
        posLabel .setBounds (d.reduced (2, 3));
    }
    bar.removeFromLeft (14);

    addSynthBtn  .setBounds (bar.removeFromLeft (68)); bar.removeFromLeft (6);
    loadSampleBtn.setBounds (bar.removeFromLeft (78));
    mixerButton  .setBounds (bar.removeFromRight (58));

    // Arrangement | divider | editor.
    Component* comps[] = { &arrangeViewport, dividerBar.get(), &editorPanel };
    verticalLayout.layOutComponents (comps, 3, area.getX(), area.getY(),
                                     area.getWidth(), area.getHeight(), true, true);

    if (arrangeView)
        arrangeView->setSize (arrangeViewport.getMaximumVisibleWidth(),
                              juce::jmax (arrangeView->preferredHeight(), arrangeViewport.getHeight()));
}

// ===========================================================================
// Project save / load
// ===========================================================================
void MainComponent::showFileMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "New Project");
    menu.addItem (2, "Open...");
    menu.addItem (3, "Save", currentProjectFile != juce::File());
    menu.addItem (4, "Save As...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fileButton),
        [this] (int result)
        {
            if (result == 1) newProject();
            else if (result == 2)
            {
                fileChooser = std::make_unique<juce::FileChooser> ("Open project", juce::File(), "*.gloopy");
                fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) openProject (fc.getResult()); });
            }
            else if (result == 3) saveProject (currentProjectFile);
            else if (result == 4)
            {
                fileChooser = std::make_unique<juce::FileChooser> ("Save project", juce::File(), "*.gloopy");
                fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                            | juce::FileBrowserComponent::warnAboutOverwriting,
                    [this] (const juce::FileChooser& fc)
                    { auto f = fc.getResult(); if (f != juce::File()) saveProject (f.withFileExtension ("gloopy")); });
            }
        });
}

void MainComponent::newProject()
{
    {
        const juce::ScopedLock sl (engineLock);
        transport.setPlaying (false);
        tracks.clear();
    }
    setupMixer();
    setupDefaultProject();
    currentProjectFile = juce::File();
    refreshUiAfterLoad();
}

juce::ValueTree MainComponent::toValueTree()
{
    const juce::ScopedLock sl (engineLock);
    juce::ValueTree root ("GLOOPY");
    root.setProperty ("version", 2, nullptr);
    root.setProperty ("bpm", transport.getBpm(), nullptr);

    juce::ValueTree trks ("TRACKS");
    for (auto& t : tracks)
    {
        juce::ValueTree tr ("TRACK");
        tr.setProperty ("name", t->name, nullptr);
        tr.setProperty ("colour", (int) t->colour.getARGB(), nullptr);
        tr.setProperty ("pitch", t->defaultPitch, nullptr);
        tr.setProperty ("vol", t->volume.load(), nullptr);
        tr.setProperty ("pan", t->pan.load(), nullptr);
        tr.setProperty ("mute", t->mute.load(), nullptr);
        tr.setProperty ("solo", t->solo.load(), nullptr);
        tr.setProperty ("mixerTrack", t->mixerTrack.load(), nullptr);
        tr.setProperty ("gen", t->generator->typeName(), nullptr);

        if (auto* sg = dynamic_cast<SynthGenerator*> (t->generator.get()))
        {
            juce::ValueTree s ("SYNTH");
            auto& p = sg->engine.params;
            s.setProperty ("wave", p.waveform.load(), nullptr);
            s.setProperty ("attack", p.attack.load(), nullptr);
            s.setProperty ("decay", p.decay.load(), nullptr);
            s.setProperty ("sustain", p.sustain.load(), nullptr);
            s.setProperty ("release", p.release.load(), nullptr);
            s.setProperty ("gain", p.gain.load(), nullptr);
            tr.addChild (s, -1, nullptr);
        }
        else if (auto* sm = dynamic_cast<Sampler*> (t->generator.get()))
        {
            juce::ValueTree s ("SAMPLE");
            const auto& buf = sm->getSampleBuffer();
            s.setProperty ("rate", sm->getSourceRate(), nullptr);
            s.setProperty ("channels", buf.getNumChannels(), nullptr);
            s.setProperty ("frames", buf.getNumSamples(), nullptr);
            s.setProperty ("root", sm->getRootNote(), nullptr);
            s.setProperty ("sname", sm->getName(), nullptr);
            juce::MemoryBlock mb ((size_t) buf.getNumChannels() * (size_t) buf.getNumSamples() * sizeof (float));
            auto* dst = (float*) mb.getData();
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                    *dst++ = buf.getSample (ch, i);
            s.setProperty ("data", juce::Base64::toBase64 (mb.getData(), mb.getSize()), nullptr);
            tr.addChild (s, -1, nullptr);
        }

        for (auto& c : t->clips)
        {
            juce::ValueTree cl ("CLIP");
            cl.setProperty ("name", c.name, nullptr);
            cl.setProperty ("start", c.startBeat, nullptr);
            cl.setProperty ("len", c.lengthBeats, nullptr);
            cl.setProperty ("content", c.contentLenBeats, nullptr);
            cl.setProperty ("looped", c.looped, nullptr);
            for (auto& n : c.notes)
            {
                juce::ValueTree nt ("NOTE");
                nt.setProperty ("pitch", n.pitch, nullptr);
                nt.setProperty ("start", n.startBeat, nullptr);
                nt.setProperty ("nlen", n.lengthBeats, nullptr);
                nt.setProperty ("vel", n.velocity, nullptr);
                cl.addChild (nt, -1, nullptr);
            }
            tr.addChild (cl, -1, nullptr);
        }
        trks.addChild (tr, -1, nullptr);
    }
    root.addChild (trks, -1, nullptr);

    juce::ValueTree mx ("MIXER");
    for (auto& mt : mixerTracks)
    {
        juce::ValueTree t ("MTRACK");
        t.setProperty ("name", mt->name, nullptr);
        t.setProperty ("vol", mt->volume.load(), nullptr);
        t.setProperty ("pan", mt->pan.load(), nullptr);
        t.setProperty ("mute", mt->mute.load(), nullptr);
        t.setProperty ("solo", mt->solo.load(), nullptr);
        for (auto& fx : mt->effects)
        {
            juce::ValueTree f ("FX");
            f.setProperty ("type", fx->name(), nullptr);
            f.setProperty ("bypass", fx->bypassed.load(), nullptr);
            for (auto& pr : fx->parameters())
            {
                juce::ValueTree pv ("PARAM");
                pv.setProperty ("name", pr.name, nullptr);
                pv.setProperty ("value", pr.get(), nullptr);
                f.addChild (pv, -1, nullptr);
            }
            t.addChild (f, -1, nullptr);
        }
        mx.addChild (t, -1, nullptr);
    }
    root.addChild (mx, -1, nullptr);
    return root;
}

void MainComponent::saveProject (const juce::File& file)
{
    if (file == juce::File()) return;
    if (auto xml = toValueTree().createXml())
    {
        xml->writeTo (file);
        currentProjectFile = file;
    }
}

void MainComponent::openProject (const juce::File& file)
{
    if (auto xml = juce::parseXML (file))
    {
        loadFromTree (juce::ValueTree::fromXml (*xml));
        refreshUiAfterLoad();
    }
    currentProjectFile = file;
}

void MainComponent::loadFromTree (const juce::ValueTree& root)
{
    if (! root.hasType ("GLOOPY")) return;

    const juce::ScopedLock sl (engineLock);
    transport.setPlaying (false);
    tracks.clear();
    mixerTracks.clear();

    auto trks = root.getChildWithName ("TRACKS");
    for (int i = 0; i < trks.getNumChildren(); ++i)
    {
        auto tr = trks.getChild (i);
        const juce::String genType = tr.getProperty ("gen", "Synth").toString();
        std::unique_ptr<Generator> gen;

        if (genType == "Synth")
        {
            auto sg = std::make_unique<SynthGenerator>();
            auto s = tr.getChildWithName ("SYNTH");
            auto& p = sg->engine.params;
            p.waveform.store ((int) s.getProperty ("wave", 1));
            p.attack.store  ((float) (double) s.getProperty ("attack", 0.01));
            p.decay.store   ((float) (double) s.getProperty ("decay", 0.15));
            p.sustain.store ((float) (double) s.getProperty ("sustain", 0.7));
            p.release.store ((float) (double) s.getProperty ("release", 0.25));
            p.gain.store    ((float) (double) s.getProperty ("gain", 0.25));
            gen = std::move (sg);
        }
        else
        {
            auto sm = std::make_unique<Sampler>();
            auto s = tr.getChildWithName ("SAMPLE");
            const int nch = juce::jmax (1, (int) s.getProperty ("channels", 1));
            const int fr  = juce::jmax (0, (int) s.getProperty ("frames", 0));
            juce::AudioBuffer<float> buf (nch, fr);
            buf.clear();
            juce::MemoryOutputStream os;
            juce::Base64::convertFromBase64 (os, s.getProperty ("data", "").toString());
            const auto mb = os.getMemoryBlock();
            const auto* src = (const float*) mb.getData();
            const size_t count = mb.getSize() / sizeof (float);
            size_t idx = 0;
            for (int ch = 0; ch < nch; ++ch)
                for (int j = 0; j < fr; ++j)
                    if (idx < count) buf.setSample (ch, j, src[idx++]);
            sm->setSample (std::move (buf), (double) s.getProperty ("rate", 44100.0),
                           s.getProperty ("sname", "sample").toString());
            sm->setRootNote ((int) s.getProperty ("root", 60));
            gen = std::move (sm);
        }
        gen->prepare (currentSampleRate, currentBlockSize);

        auto t = std::make_unique<Track> (tr.getProperty ("name", "Track").toString(),
                    std::move (gen), (int) tr.getProperty ("pitch", 60),
                    juce::Colour ((juce::uint32) (int) tr.getProperty ("colour", (int) 0xff4a90d9)));
        t->volume.store ((float) (double) tr.getProperty ("vol", 0.8));
        t->pan.store    ((float) (double) tr.getProperty ("pan", 0.0));
        t->mute.store   ((bool) tr.getProperty ("mute", false));
        t->solo.store   ((bool) tr.getProperty ("solo", false));
        t->mixerTrack.store ((int) tr.getProperty ("mixerTrack", 0));

        for (int ci = 0; ci < tr.getNumChildren(); ++ci)
        {
            auto cl = tr.getChild (ci);
            if (! cl.hasType ("CLIP")) continue;
            Clip c;
            c.name = cl.getProperty ("name", "").toString();
            c.startBeat = (double) cl.getProperty ("start", 0.0);
            c.lengthBeats = (double) cl.getProperty ("len", 4.0);
            c.contentLenBeats = (double) cl.getProperty ("content", 4.0);
            c.looped = (bool) cl.getProperty ("looped", true);
            for (int n = 0; n < cl.getNumChildren(); ++n)
            {
                auto nt = cl.getChild (n);
                c.notes.push_back ({ (int) nt.getProperty ("pitch", 60),
                                     (double) nt.getProperty ("start", 0.0),
                                     (double) nt.getProperty ("nlen", 0.25),
                                     (float) (double) nt.getProperty ("vel", 0.85) });
            }
            t->clips.push_back (std::move (c));
        }
        tracks.push_back (std::move (t));
    }

    auto mx = root.getChildWithName ("MIXER");
    if (mx.getNumChildren() > 0)
    {
        for (int i = 0; i < mx.getNumChildren(); ++i)
        {
            auto tv = mx.getChild (i);
            auto mt = std::make_unique<MixerTrack> (tv.getProperty ("name", "Track").toString());
            mt->volume.store ((float) (double) tv.getProperty ("vol", 0.8));
            mt->pan.store    ((float) (double) tv.getProperty ("pan", 0.0));
            mt->mute.store   ((bool) tv.getProperty ("mute", false));
            mt->solo.store   ((bool) tv.getProperty ("solo", false));
            mt->buffer.setSize (2, juce::jmax (16, currentBlockSize));
            for (int f = 0; f < tv.getNumChildren(); ++f)
            {
                auto ft = tv.getChild (f);
                auto fx = makeEffect (ft.getProperty ("type", "Gain").toString());
                if (fx == nullptr) continue;
                fx->bypassed.store ((bool) ft.getProperty ("bypass", false));
                auto params = fx->parameters();
                for (int pv = 0; pv < ft.getNumChildren(); ++pv)
                {
                    const juce::String nm = ft.getChild (pv).getProperty ("name", "").toString();
                    const float val = (float) (double) ft.getChild (pv).getProperty ("value", 0.0);
                    for (auto& pr : params) if (pr.name == nm) { pr.set (val); break; }
                }
                mt->effects.push_back (std::move (fx));
            }
            mixerTracks.push_back (std::move (mt));
        }
    }
    if (mixerTracks.empty())
    {
        mixerTracks.push_back (std::make_unique<MixerTrack> ("Master"));
        for (int i = 1; i <= 8; ++i)
            mixerTracks.push_back (std::make_unique<MixerTrack> ("Ins " + juce::String (i)));
    }

    transport.setBpm ((double) root.getProperty ("bpm", 128.0));
}

void MainComponent::refreshUiAfterLoad()
{
    selTrack = selClip = -1;
    editorPanel.roll.setEnabledEditing (false);
    editorPanel.roll.loadNotes ({});
    editorPanel.title.setText ("EDITOR", juce::dontSendNotification);

    if (arrangeView) { arrangeView->setSelection (-1, -1); arrangeView->rebuild(); }
    if (mixerView) mixerView->rebuild();

    transport.requestReset();
    bpmSlider.setValue (transport.getBpm(), juce::dontSendNotification);
    playButton.setToggleState (false, juce::dontSendNotification);
    playButton.setIcon (IconButton::Play);
    resized();
}
