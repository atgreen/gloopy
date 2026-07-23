#include "MainComponent.h"
#include "NoteScheduler.h"
#include "Sampler.h"
#include "SynthGenerator.h"
#include "DrumSynth.h"
#include <array>
#include <cmath>

MainComponent::MainComponent()
{
    formatManager.registerBasicFormats();

    // Start with one 16-step (4-beat) pattern.
    {
        auto p = std::make_unique<Pattern> (16);
        p->name = "Pattern 1";
        p->colour = paletteColour (0);
        patterns.push_back (std::move (p));
    }
    transport.setLoopBeats (curPattern().getLengthBeats());
    transport.setBpm (128.0);

    // ---- transport / toolbar ----
    addAndMakeVisible (playButton);
    playButton.setClickingTogglesState (true);
    playButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2e7d32));
    playButton.onClick = [this]
    {
        transport.setPlaying (playButton.getToggleState());
        playButton.setButtonText (playButton.getToggleState() ? "Pause" : "Play");
    };

    addAndMakeVisible (stopButton);
    stopButton.onClick = [this]
    {
        transport.setPlaying (false);
        transport.requestReset();
        playButton.setToggleState (false, juce::dontSendNotification);
        playButton.setButtonText ("Play");
    };

    addAndMakeVisible (addSynthBtn);
    addSynthBtn.onClick = [this]
    {
        addChannel (std::make_unique<Channel> ("Synth",
                        std::make_unique<SynthGenerator>(), 48, juce::Colours::skyblue));
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
                if (! file.existsAsFile())
                    return;

                auto sampler = std::make_unique<Sampler>();
                sampler->prepare (currentSampleRate, currentBlockSize);
                if (sampler->loadFile (file, formatManager))
                    addChannel (std::make_unique<Channel> (file.getFileNameWithoutExtension(),
                                    std::move (sampler), 60, juce::Colours::mediumturquoise));
            });
    };

    addAndMakeVisible (clearButton);
    clearButton.onClick = [this]
    {
        {
            const juce::ScopedLock sl (engineLock);
            for (int i = 0; i < curPattern().getChannelCount(); ++i)
                curPattern().clearChannel (i);
        }
        if (rackView) rackView->repaint();
        if (pianoRollChannel >= 0)
        {
            const juce::ScopedLock sl (engineLock);
            pianoPanel.roll.loadNotes (curPattern().lane (pianoRollChannel));
        }
    };

    addAndMakeVisible (bpmLabel);
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (bpmSlider);
    bpmSlider.setSliderStyle (juce::Slider::IncDecButtons);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 54, 22);
    bpmSlider.setRange (40.0, 260.0, 1.0);
    bpmSlider.setValue (transport.getBpm(), juce::dontSendNotification);
    bpmSlider.onValueChange = [this] { transport.setBpm (bpmSlider.getValue()); };

    addAndMakeVisible (posLabel);
    posLabel.setJustificationType (juce::Justification::centredLeft);
    posLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    posLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb0b0c0));

    // ---- pattern selector ----
    addAndMakeVisible (patternBox);
    patternBox.onChange = [this]
    {
        const int idx = patternBox.getSelectedId() - 1;
        if (idx >= 0) selectPattern (idx);
    };
    addAndMakeVisible (addPatternBtn);
    addPatternBtn.onClick = [this] { addPattern(); };

    addAndMakeVisible (modeButton);
    modeButton.setClickingTogglesState (true);
    modeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff9a6b16));
    modeButton.onClick = [this] { toggleSongMode(); };

    addAndMakeVisible (mixerButton);
    mixerButton.onClick = [this] { openMixer(); };

    // ---- channel rack ----
    rackView = std::make_unique<ChannelRackView> (channels,
                    [this]() -> Pattern* { return patterns.empty() ? nullptr
                                                  : patterns[(size_t) currentPatternIndex].get(); },
                    transport, engineLock);
    rackView->onOpenPianoRoll = [this] (int ch) { openPianoRollFor (ch); };
    rackView->onPatternEdited = [this] (int ch)
    {
        if (ch == pianoRollChannel)
        {
            const juce::ScopedLock sl (engineLock);
            pianoPanel.roll.loadNotes (curPattern().lane (ch));
        }
    };
    rackViewport.setViewedComponent (rackView.get(), false);
    rackViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (rackViewport);

    // ---- piano roll panel ----
    addAndMakeVisible (pianoPanel);
    pianoPanel.roll.setEnabledEditing (false);
    pianoPanel.title.setText ("Piano Roll", juce::dontSendNotification);
    pianoPanel.roll.onNotesChanged = [this] { writeBackPianoRoll(); };

    // ---- playlist ----
    playlistView = std::make_unique<PlaylistView> (clips, patterns, transport, engineLock,
                        [this] { return currentPatternIndex; });
    playlistView->onChanged = [this] { if (playlistView) playlistView->repaint(); };
    addAndMakeVisible (*playlistView);

    // ---- mixer (hosted in a separate window, opened on demand) ----
    setupMixer();
    mixerView = std::make_unique<MixerView> (mixerTracks, engineLock,
                    [this] (const juce::String& t) { return makeEffect (t); });

    // Vertical split: rack | piano roll | playlist.
    verticalLayout.setItemLayout (0, 70.0, -0.8, -0.30);   // rack
    verticalLayout.setItemLayout (1, 6.0, 6.0, 6.0);       // divider
    verticalLayout.setItemLayout (2, 70.0, -0.8, -0.28);   // piano panel
    verticalLayout.setItemLayout (3, 6.0, 6.0, 6.0);       // divider
    verticalLayout.setItemLayout (4, 70.0, -0.8, -0.36);   // playlist
    dividerBar  = std::make_unique<juce::StretchableLayoutResizerBar> (&verticalLayout, 1, false);
    dividerBar2 = std::make_unique<juce::StretchableLayoutResizerBar> (&verticalLayout, 3, false);
    addAndMakeVisible (*dividerBar);
    addAndMakeVisible (*dividerBar2);

    setupDefaultProject();
    refreshPatternBox();

    setSize (1100, 860);
    setAudioChannels (0, 2);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    mixerWindow = nullptr;   // release window before its non-owned content (mixerView)
    shutdownAudio();
}

Pattern& MainComponent::curPattern()
{
    jassert (! patterns.empty());
    return *patterns[(size_t) juce::jlimit (0, (int) patterns.size() - 1, currentPatternIndex)];
}

void MainComponent::addChannel (std::unique_ptr<Channel> channel)
{
    channel->generator->prepare (currentSampleRate, currentBlockSize);
    {
        const juce::ScopedLock sl (engineLock);
        channels.push_back (std::move (channel));
        for (auto& p : patterns)
            p->setChannelCount ((int) channels.size());
    }
    if (rackView) rackView->rebuild();
    resized();
}

void MainComponent::setupDefaultProject()
{
    auto makeSamplerChannel = [this] (const juce::String& name,
                                      juce::AudioBuffer<float> buf, juce::Colour col)
    {
        auto sampler = std::make_unique<Sampler>();
        sampler->prepare (currentSampleRate, currentBlockSize);
        sampler->setSample (std::move (buf), DrumSynth::kRate, name);
        addChannel (std::make_unique<Channel> (name, std::move (sampler), 60, col));
    };

    makeSamplerChannel ("Kick",  DrumSynth::makeKick(),  juce::Colours::orangered);
    makeSamplerChannel ("Snare", DrumSynth::makeSnare(), juce::Colours::gold);
    makeSamplerChannel ("Hat",   DrumSynth::makeHat(),   juce::Colours::aquamarine);
    makeSamplerChannel ("Clap",  DrumSynth::makeClap(),  juce::Colours::violet);

    auto bass = std::make_unique<SynthGenerator>();
    bass->engine.params.waveform.store (1);
    bass->engine.params.release.store (0.15f);
    addChannel (std::make_unique<Channel> ("Bass", std::move (bass), 36, juce::Colours::skyblue));

    // Route each default channel to its own mixer insert (1..5).
    for (int i = 0; i < (int) channels.size(); ++i)
        channels[(size_t) i]->mixerTrack.store (juce::jmin (i + 1, (int) mixerTracks.size() - 1));

    // Seed a demo groove in Pattern 1.
    const juce::ScopedLock sl (engineLock);
    auto step = [this] (int ch, std::initializer_list<int> steps)
    {
        for (int s : steps)
            curPattern().toggleStep (ch, s, channels[(size_t) ch]->defaultPitch, 0.85f);
    };
    step (0, { 0, 4, 8, 12 });
    step (1, { 4, 12 });
    step (2, { 0, 2, 4, 6, 8, 10, 12, 14 });
    step (3, { 8 });
    step (4, { 0, 3, 6, 8, 11, 14 });

    // Seed a short arrangement so Song mode plays something immediately.
    clips.push_back ({ 0, 0, 0.0, 8.0 });   // Pattern 1, track 1, two bars
}

juce::Colour MainComponent::paletteColour (int index) const
{
    static const juce::uint32 palette[] = {
        0xff4a90d9, 0xffe0663a, 0xff56b26b, 0xffb266d9,
        0xffd9a441, 0xff3ab0c4, 0xffd94a7a, 0xff8bbf3a
    };
    return juce::Colour (palette[(size_t) (index % (int) juce::numElementsInArray (palette))]);
}

void MainComponent::toggleSongMode()
{
    const bool song = modeButton.getToggleState();
    transport.setPlayMode (song ? Transport::SongMode : Transport::PatternMode);
    transport.requestReset();
    modeButton.setButtonText (song ? "Song" : "Pattern");
    if (rackView) rackView->repaint();
    if (playlistView) playlistView->repaint();
}

void MainComponent::setupMixer()
{
    mixerTracks.clear();
    mixerTracks.push_back (std::make_unique<MixerTrack> ("Master"));
    mixerTracks[0]->volume.store (0.9f);
    for (int i = 1; i <= 8; ++i)
        mixerTracks.push_back (std::make_unique<MixerTrack> ("Ins " + juce::String (i)));
}

std::unique_ptr<Effect> MainComponent::makeEffect (const juce::String& type)
{
    auto fx = EffectFactory::create (type);
    if (fx != nullptr)
        fx->prepare (currentSampleRate, currentBlockSize, 2);
    return fx;
}

// A mixer window that just hides itself when closed (content is non-owned).
namespace
{
    struct HideOnCloseWindow : public juce::DocumentWindow
    {
        HideOnCloseWindow()
            : DocumentWindow ("Mixer", juce::Colour (0xff17171b),
                              juce::DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
        }
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
// Patterns
// ---------------------------------------------------------------------------
void MainComponent::addPattern()
{
    {
        const juce::ScopedLock sl (engineLock);
        auto p = std::make_unique<Pattern> (16);
        p->name = "Pattern " + juce::String (patterns.size() + 1);
        p->colour = paletteColour ((int) patterns.size());
        p->setChannelCount ((int) channels.size());
        patterns.push_back (std::move (p));
        currentPatternIndex = (int) patterns.size() - 1;
    }
    refreshPatternBox();
    if (rackView) rackView->repaint();
    if (pianoRollChannel >= 0)
        openPianoRollFor (pianoRollChannel);   // reload roll for new (empty) pattern
}

void MainComponent::selectPattern (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) patterns.size()))
        return;
    {
        const juce::ScopedLock sl (engineLock);
        currentPatternIndex = index;
    }
    if (rackView) rackView->repaint();
    if (pianoRollChannel >= 0)
        openPianoRollFor (pianoRollChannel);
}

void MainComponent::refreshPatternBox()
{
    patternBox.clear (juce::dontSendNotification);
    for (int i = 0; i < (int) patterns.size(); ++i)
        patternBox.addItem (patterns[(size_t) i]->name, i + 1);
    patternBox.setSelectedId (currentPatternIndex + 1, juce::dontSendNotification);
}

// ---------------------------------------------------------------------------
// Piano roll wiring
// ---------------------------------------------------------------------------
void MainComponent::openPianoRollFor (int channel)
{
    if (! juce::isPositiveAndBelow (channel, (int) channels.size()))
        return;

    pianoRollChannel = channel;
    pianoPanel.title.setText ("Piano Roll  —  " + channels[(size_t) channel]->name,
                              juce::dontSendNotification);
    pianoPanel.roll.setEnabledEditing (true);
    {
        const juce::ScopedLock sl (engineLock);
        pianoPanel.roll.loadNotes (curPattern().lane (channel));
    }
    if (rackView) rackView->setSelectedChannel (channel);
}

void MainComponent::writeBackPianoRoll()
{
    if (pianoRollChannel < 0)
        return;
    {
        const juce::ScopedLock sl (engineLock);
        curPattern().setLane (pianoRollChannel, pianoPanel.roll.getNotes());
    }
    if (rackView) rackView->repaint();
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
    for (auto& c : channels)
        c->generator->prepare (sampleRate, samplesPerBlockExpected);
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
        for (auto& c : channels)
            c->generator->allNotesOff();
    }

    Pattern& pat = curPattern();
    const bool song = transport.isSongMode();

    struct Seg { juce::int64 loopStart; int chunk; int tsOffset; bool wrap; };
    std::array<Seg, 16> segs;
    int nseg = 0;

    const bool   playing = transport.isPlaying();
    const double spb     = transport.samplesPerBeat();

    // Loop length depends on mode: one pattern, or the whole song.
    juce::int64 loopLen;
    if (song)
    {
        double songBeats = pat.getLengthBeats();
        for (const auto& c : clips)
            songBeats = juce::jmax (songBeats, c.startBeat + c.lengthBeats);
        loopLen = (juce::int64) std::llround (songBeats * spb);
    }
    else
    {
        loopLen = (juce::int64) std::llround (pat.getLengthBeats() * spb);
    }
    if (loopLen < 1) loopLen = 1;

    // Collects note events for one channel over a song-sample window, expanding
    // each overlapping clip's pattern (which repeats to fill the clip).
    auto collectSong = [this, spb] (int ci, juce::MidiBuffer& midi,
                                    juce::int64 songStart, int chunk, int tsOffset)
    {
        const juce::int64 songEnd = songStart + chunk;
        for (const auto& c : clips)
        {
            if (! juce::isPositiveAndBelow (c.patternIndex, (int) patterns.size()))
                continue;
            const Pattern& P = *patterns[(size_t) c.patternIndex];
            if (ci >= P.getChannelCount())
                continue;

            const juce::int64 clipStart = (juce::int64) std::llround (c.startBeat * spb);
            const juce::int64 clipEnd   = (juce::int64) std::llround ((c.startBeat + c.lengthBeats) * spb);
            const juce::int64 patLen    = (juce::int64) std::llround (P.getLengthBeats() * spb);
            if (patLen < 1) continue;

            const juce::int64 lo = juce::jmax (songStart, clipStart);
            const juce::int64 hi = juce::jmin (songEnd, clipEnd);
            if (lo >= hi) continue;

            juce::int64 repStart = clipStart + ((lo - clipStart) / patLen) * patLen;
            for (; repStart < hi; repStart += patLen)
            {
                const juce::int64 winLo = juce::jmax (lo, repStart);
                const juce::int64 winHi = juce::jmin (hi, repStart + patLen);
                if (winLo >= winHi) continue;

                collectNotes (P.lane (ci), midi,
                              winLo - repStart, (int) (winHi - winLo),
                              tsOffset + (int) (winLo - songStart), spb);
            }
        }
    };

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

    bool anySolo = false;
    for (auto& c : channels)
        if (c->solo.load()) { anySolo = true; break; }

    if (mixBuffer.getNumSamples() < num)
        mixBuffer.setSize (2, num, false, false, true);
    for (auto& mt : mixerTracks)
    {
        if (mt->buffer.getNumSamples() < num)
            mt->buffer.setSize (2, num, false, false, true);
        mt->buffer.clear();
    }

    const int numTracks = (int) mixerTracks.size();

    // --- render each channel and route it to its mixer insert ---
    for (int ci = 0; ci < (int) channels.size(); ++ci)
    {
        Channel* ch = channels[(size_t) ci].get();

        juce::MidiBuffer midi;
        if (playing)
        {
            for (int s = 0; s < nseg; ++s)
            {
                if (song)
                    collectSong (ci, midi, segs[(size_t) s].loopStart,
                                 segs[(size_t) s].chunk, segs[(size_t) s].tsOffset);
                else if (ci < pat.getChannelCount())
                    collectNotes (pat.lane (ci), midi,
                                  segs[(size_t) s].loopStart, segs[(size_t) s].chunk,
                                  segs[(size_t) s].tsOffset, spb);

                if (segs[(size_t) s].wrap)
                    midi.addEvent (juce::MidiMessage::allNotesOff (1),
                                   segs[(size_t) s].tsOffset + segs[(size_t) s].chunk);
            }
        }

        mixBuffer.clear();
        ch->generator->render (mixBuffer, midi, 0, num);

        const bool audible = ! ch->mute.load() && (! anySolo || ch->solo.load());
        if (! audible)
            continue;

        const float v   = ch->volume.load();
        const float pan = ch->pan.load();
        const float theta = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        const float lg = v * std::cos (theta);
        const float rg = v * std::sin (theta);
        const int   route = juce::jlimit (0, numTracks - 1, ch->mixerTrack.load());
        auto& dst = mixerTracks[(size_t) route]->buffer;
        dst.addFrom (0, 0, mixBuffer, 0, 0, num, lg);
        dst.addFrom (1, 0, mixBuffer, juce::jmin (1, mixBuffer.getNumChannels() - 1), 0, num, rg);
    }

    // A view of the first `num` samples so stateful effects advance correctly.
    auto subView = [num] (juce::AudioBuffer<float>& b)
    {
        return juce::AudioBuffer<float> (b.getArrayOfWritePointers(), b.getNumChannels(), 0, num);
    };

    // --- inserts (1..N): effect chain, then sum into master ---
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
        if (! audible)
            continue;

        const float v   = mt.volume.load();
        const float pan = mt.pan.load();
        const float theta = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        master.buffer.addFrom (0, 0, mt.buffer, 0, 0, num, v * std::cos (theta));
        master.buffer.addFrom (1, 0, mt.buffer, 1, 0, num, v * std::sin (theta));
    }

    // --- master chain + output ---
    { auto sub = subView (master.buffer); for (auto& fx : master.effects) fx->process (sub); }
    master.peakL.store (master.buffer.getMagnitude (0, 0, num));
    master.peakR.store (master.buffer.getMagnitude (1, 0, num));

    const float mv = master.volume.load();
    if (out->getNumChannels() > 0) out->addFrom (0, start, master.buffer, 0, 0, num, mv);
    if (out->getNumChannels() > 1) out->addFrom (1, start, master.buffer, 1, 0, num, mv);
}

void MainComponent::releaseResources() {}

// ---------------------------------------------------------------------------
// GUI
// ---------------------------------------------------------------------------
void MainComponent::timerCallback()
{
    const double beats = transport.getPlayheadBeats();
    const int steps = juce::jmax (1, curPattern().getLengthSteps());
    const int bar  = (int) (beats / 4.0) + 1;
    const int beat = (int) std::fmod (beats, 4.0) + 1;
    const int step = ((int) std::floor (beats / curPattern().stepDurationBeats()) % steps) + 1;
    posLabel.setText (juce::String::formatted ("%d . %d   step %02d", bar, beat, step),
                      juce::dontSendNotification);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141417));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto bar = area.removeFromTop (48).reduced (8, 8);
    playButton   .setBounds (bar.removeFromLeft (68)); bar.removeFromLeft (4);
    stopButton   .setBounds (bar.removeFromLeft (56)); bar.removeFromLeft (6);
    modeButton   .setBounds (bar.removeFromLeft (72)); bar.removeFromLeft (12);
    bpmLabel     .setBounds (bar.removeFromLeft (36));
    bpmSlider    .setBounds (bar.removeFromLeft (108)); bar.removeFromLeft (8);
    posLabel     .setBounds (bar.removeFromLeft (132)); bar.removeFromLeft (10);
    patternBox   .setBounds (bar.removeFromLeft (124));
    addPatternBtn.setBounds (bar.removeFromLeft (54)); bar.removeFromLeft (8);
    clearButton  .setBounds (bar.removeFromRight (54)); bar.removeFromRight (8);
    mixerButton  .setBounds (bar.removeFromRight (60)); bar.removeFromRight (8);
    loadSampleBtn.setBounds (bar.removeFromRight (78)); bar.removeFromRight (6);
    addSynthBtn  .setBounds (bar.removeFromRight (70));

    // Vertical split: rack | piano roll | playlist.
    Component* comps[] = { &rackViewport, dividerBar.get(), &pianoPanel,
                           dividerBar2.get(), playlistView.get() };
    verticalLayout.layOutComponents (comps, 5,
                                     area.getX(), area.getY(),
                                     area.getWidth(), area.getHeight(),
                                     true, true);

    if (rackView)
        rackView->setSize (rackViewport.getMaximumVisibleWidth(),
                           juce::jmax (rackView->preferredHeight(),
                                       rackViewport.getHeight()));
}
