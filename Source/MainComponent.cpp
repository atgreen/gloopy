// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#include "MainComponent.h"
#include "NoteScheduler.h"
#include "Sampler.h"
#include "SfizzGenerator.h"
#include "SynthGenerator.h"
#include "DrumSynth.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <iostream>

MainComponent::MainComponent (bool headless)
    : headlessCli (headless)
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
        if (playing) clearClipIndicators();
        transport.setPlaying (playing);
        playButton.setIcon (playing ? IconButton::Pause : IconButton::Play);
    };

    addAndMakeVisible (stopButton);
    stopButton.onClick = [this]
    {
        if (recording.load()) { finalizeRecording(); recordButton.setToggleState (false, juce::dontSendNotification); }
        transport.setPlaying (false);
        transport.requestReset();
        playButton.setToggleState (false, juce::dontSendNotification);
        playButton.setIcon (IconButton::Play);
    };

    addAndMakeVisible (recordButton);
    recordButton.setTooltip ("Record MIDI into the selected instrument track");
    recordButton.onClick = [this]
    {
        if (recording.load()) { finalizeRecording(); transport.setPlaying (false); playButton.setToggleState (false, juce::dontSendNotification); playButton.setIcon (IconButton::Play); }
        else                  { startRecording(); playButton.setToggleState (true, juce::dontSendNotification); playButton.setIcon (IconButton::Pause); }
        recordButton.setToggleState (recording.load(), juce::dontSendNotification);
    };

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

    addAndMakeVisible (addSfzBtn);
    addSfzBtn.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Load an SFZ instrument", juce::File(), "*.sfz");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (! file.existsAsFile()) return;
                auto sfz = std::make_unique<SfizzGenerator>();
                sfz->prepare (currentSampleRate, currentBlockSize);
                juce::String err;
                if (sfz->loadSfz (file, err))
                    addTrack (std::make_unique<Track> (sfz->getName(),
                                  std::move (sfz), 60, paletteColour ((int) tracks.size())));
                else
                    std::cout << "[sfz] " << err << std::endl;
            });
    };

    addAndMakeVisible (addAudioBtn);
    addAudioBtn.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Import audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (! file.existsAsFile()) return;
                std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
                if (reader == nullptr || reader->lengthInSamples <= 0) return;

                auto buf = std::make_shared<juce::AudioBuffer<float>> (
                    (int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read (buf.get(), 0, (int) reader->lengthInSamples, 0, true, true);

                const double durationSec = (double) reader->lengthInSamples / reader->sampleRate;
                Clip c;
                c.type = ClipType::Audio;
                c.name = file.getFileNameWithoutExtension();
                c.startBeat = 0.0;
                c.lengthBeats = juce::jmax (0.25, durationSec * transport.getBpm() / 60.0);
                c.audio = buf;
                c.audioSourceRate = reader->sampleRate;
                c.peaks = std::make_shared<std::vector<float>> (buildPeaks (*buf));

                auto track = std::make_unique<Track> (c.name, nullptr, 60,
                                 paletteColour ((int) tracks.size()), TrackType::Audio);
                track->clips.push_back (std::move (c));
                addTrack (std::move (track));
            });
    };

    addAndMakeVisible (addPluginBtn);
    addPluginBtn.onClick = [this] { showAddPluginMenu(); };

    addAndMakeVisible (loopButton);
    loopButton.setClickingTogglesState (true);
    loopButton.setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
    loopButton.onClick = [this] { transport.setLoopEnabled (loopButton.getToggleState()); };

    addAndMakeVisible (mixerButton);
    mixerButton.onClick = [this] { openMixer(); };

    // ---- arrange view ----
    arrangeView = std::make_unique<ArrangeView> (tracks, transport, engineLock);
    arrangeView->onClipSelected = [this] (int t, int c) { selectClip (t, c); };
    arrangeView->onChanged      = [this] { if (arrangeView) arrangeView->repaint(); };
    arrangeView->onLoopChanged  = [this]
    {
        loopButton.setToggleState (transport.isLoopEnabled(), juce::dontSendNotification);
    };
    arrangeView->onOpenTrackEditor = [this] (int i)
    {
        if (! juce::isPositiveAndBelow (i, (int) tracks.size())) return;
        if (auto* g = tracks[(size_t) i]->generator.get())
            openPluginEditor (g->getPluginInstance(), tracks[(size_t) i]->name);
    };
    arrangeViewport.setViewedComponent (arrangeView.get(), false);
    arrangeViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (arrangeViewport);

    // ---- clip editor ----
    addAndMakeVisible (editorPanel);
    editorPanel.roll.setShowPlayhead (false);
    editorPanel.roll.setEnabledEditing (false);
    editorPanel.steps.setEnabledEditing (false);
    editorPanel.title.setText ("EDITOR", juce::dontSendNotification);
    editorPanel.roll.onNotesChanged  = [this] { writeBackEditor(); };
    editorPanel.steps.onNotesChanged = [this] { writeBackEditor(); };
    editorPanel.pianoBtn.onClick = [this] { setEditorMode (0); };
    editorPanel.stepBtn.onClick  = [this] { setEditorMode (1); };
    setEditorMode (editorMode);

    verticalLayout.setItemLayout (0, 120.0, -0.85, -0.60);   // arrangement
    verticalLayout.setItemLayout (1, 6.0, 6.0, 6.0);         // divider
    verticalLayout.setItemLayout (2, 90.0, -0.85, -0.40);    // editor
    dividerBar = std::make_unique<juce::StretchableLayoutResizerBar> (&verticalLayout, 1, false);
    addAndMakeVisible (*dividerBar);

    setupMixer();
    mixerView = std::make_unique<MixerView> (mixerTracks, engineLock,
                    [this] (const juce::String& t) { return makeEffect (t); });
    mixerView->ensurePlugins        = [this] { scanPlugins(); };
    mixerView->getEffectPlugins     = [this] { return pluginHost.plugins (false); };
    mixerView->makePluginEffect     = [this] (const juce::PluginDescription& d) { return makePluginEffect (d); };
    mixerView->onOpenPluginEditor   = [this] (juce::AudioProcessor* p, const juce::String& n) { openPluginEditor (p, n); };
    mixerView->onBeforeStructuralChange = [this] { closeAllPluginWindows(); };

    setupDefaultProject();

    // Headless CLI tools (inspect/validate/pack) load a project, print, and exit —
    // no control ports, no audio device. Skip all of that so they run cleanly
    // alongside a live instance and keep stdout clean for JSON.
    if (headlessCli)
    {
        setWantsKeyboardFocus (true);
        return;
    }

    // ---- control API: OSC real-time lane ----
    {
        OscControl::Hooks h;
        h.resolveTrack = [this] (int id) { return resolveTrack (id); };
        h.setSynthParam = [this] (int id, const juce::String& n, float v) -> bool
        {
            // Real-time (OSC) thread: hold the engineLock try-lock so a structural
            // edit can't erase the Track while we dereference its generator. Drop
            // the update if the lock is contended (audio mid-block or edit running).
            const juce::ScopedTryLock stl (engineLock);
            if (! stl.isLocked()) return false;
            return applySynthParam (resolveTrack (id), n, v);
        };
        h.mixerTracks  = &mixerTracks;
        h.engineLock   = &engineLock;
        h.transport    = &transport;
        h.log          = [] (const juce::String& s) { std::cout << "[osc] " << s << std::endl; };
        osc = std::make_unique<OscControl> (h);
        const int oscPort = 9000;
        if (osc->start (oscPort))
            std::cout << "[osc] listening on udp:" << oscPort << std::endl;
        else
            std::cout << "[osc] FAILED to bind udp:" << oscPort << std::endl;
    }

    // ---- control API: gRPC command surface ----
    {
        grpc = std::make_unique<GrpcServer> (*this);
        const int grpcPort = 50051;
        if (grpc->start (grpcPort))
            std::cout << "[grpc] listening on 127.0.0.1:" << grpcPort << std::endl;
        else
            std::cout << "[grpc] FAILED to start on 127.0.0.1:" << grpcPort << std::endl;
    }

    setWantsKeyboardFocus (true);   // catch Ctrl+Z / Ctrl+Shift+Z (bubbles up from children)
    setSize (1180, 820);
    setAudioChannels (2, 2);
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        std::cout << "[audio] device='" << dev->getName() << "' inputs="
                  << dev->getActiveInputChannels().countNumberOfSetBits() << " outputs="
                  << dev->getActiveOutputChannels().countNumberOfSetBits()
                  << " rate=" << dev->getCurrentSampleRate() << std::endl;
    else
        std::cout << "[audio] NO audio device open (headless?)" << std::endl;
    // Self-test seam: GLOOPY_REC_TEST_TONE_HZ injects a tone in place of the mic.
    recordTestToneHz.store (juce::SystemStats::getEnvironmentVariable ("GLOOPY_REC_TEST_TONE_HZ", "0").getDoubleValue());
    setupMidiInputs();
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    teardownMidiInputs();        // stop MIDI callbacks before tracks/audio go away
    grpc.reset();                // stop gRPC (and its message-thread callbacks) first
    osc.reset();                 // stop OSC before tracks/mixer are destroyed
    pluginWindows.clear();       // delete plugin editors before their processors
    mixerWindow = nullptr;
    notesWindow = nullptr;
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
    pushUndoSnapshot();
    if (track->generator) track->generator->prepare (currentSampleRate, currentBlockSize);
    track->liveMidi.reset (currentSampleRate);
    {
        const juce::ScopedLock sl (engineLock);
        track->mixerTrack.store (juce::jmin ((int) tracks.size() + 1, (int) mixerTracks.size() - 1));
        tracks.push_back (std::move (track));
    }
    refreshTrackIds();
    if (! undoSuppressed && ! tracks.empty()) emitChange ("track_added", tracks.back()->id);
    if (arrangeView) arrangeView->rebuild();
    resized();
}

void MainComponent::refreshTrackIds()
{
    int firstInst = -1;
    {
        const juce::ScopedLock sl (idMapLock);
        idMap.clear();
        for (auto& t : tracks)
        {
            if (t->id < 0) t->id = nextTrackId++;
            idMap[t->id] = t.get();
            if (firstInst < 0 && t->generator != nullptr) firstInst = t->id;
        }
    }
    firstInstrumentId.store (firstInst);   // fallback target for live MIDI input
    // Log the id -> name map (control-API discovery until gRPC GetState exists).
    juce::String s = "[osc] tracks:";
    for (auto& t : tracks) s << "  " << t->id << "=" << t->name;
    std::cout << s << std::endl;
}

Track* MainComponent::resolveTrack (int id)
{
    const juce::ScopedLock sl (idMapLock);
    auto it = idMap.find (id);
    return it != idMap.end() ? it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Live MIDI input
// ---------------------------------------------------------------------------
void MainComponent::setupMidiInputs()
{
    openAvailableMidiInputs();
    // ...plus a virtual port so a keyboard/software can connect at any time.
    virtualMidiIn = juce::MidiInput::createNewDevice ("Gloopy MIDI In", this);
    if (virtualMidiIn != nullptr)
    {
        virtualMidiIn->start();
        std::cout << "[midi] virtual input 'Gloopy MIDI In' ready" << std::endl;
    }
    std::cout << "[midi] played notes go to the selected instrument track" << std::endl;
    // Hot-plug: open inputs that appear after startup.
    midiListConnection = juce::MidiDeviceListConnection::make ([this]
    {
        juce::MessageManager::callAsync ([this] { openAvailableMidiInputs(); });
    });
}

void MainComponent::openAvailableMidiInputs()
{
    for (const auto& d : juce::MidiInput::getAvailableDevices())
    {
        if (openMidiInputs.contains (d.identifier)) continue;   // already open
        deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
        deviceManager.addMidiInputDeviceCallback (d.identifier, this);
        openMidiInputs.add (d.identifier);
        std::cout << "[midi] input open: " << d.name << std::endl;
    }
}


void MainComponent::teardownMidiInputs()
{
    midiListConnection = {};   // stop hot-plug callbacks
    for (const auto& id : openMidiInputs)
        deviceManager.removeMidiInputDeviceCallback (id, this);
    openMidiInputs.clear();
    if (virtualMidiIn != nullptr) { virtualMidiIn->stop(); virtualMidiIn.reset(); }
}

void MainComponent::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    // Continuous controllers drive mapped parameters (controller mapping / MIDI-learn).
    if (m.isController())
        apiSetController ("cc:" + juce::String (m.getControllerNumber()), m.getControllerValue() / 127.0f);

    // Route to the selected instrument track (or the first instrument if none is
    // selected) via its lock-free collector — the same path the OSC live lane uses.
    int id = midiInputTarget.load();
    if (id < 0) id = firstInstrumentId.load();
    if (Track* t = resolveTrack (id))
        if (t->generator != nullptr)
            t->liveMidi.addMessageToQueue (m);
}

void MainComponent::startRecording()
{
    // MIDI capture into the selected instrument track (if any) ...
    int target = midiInputTarget.load();
    if (target < 0) target = firstInstrumentId.load();
    if (resolveTrack (target) != nullptr)
    {
        recordTrackId.store (target);
        recordStartSample = transport.getPlayheadSamples();
        recordWrite.store (0);
        recording.store (true);
    }
    startAudioRecording();              // ... and audio capture for any armed audio track
    clearClipIndicators();
    transport.setPlaying (true);
}

void MainComponent::finalizeRecording()
{
    stopAudioRecording();               // finalize audio takes independently of MIDI
    if (! recording.exchange (false)) return;
    const int count = juce::jmin (recordWrite.load(), (int) recordBuffer.size());
    Track* t = resolveTrack (recordTrackId.load());
    if (count == 0 || t == nullptr) return;

    const double spb = juce::jmax (1.0, transport.samplesPerBeat());
    const auto toBeat = [&] (juce::int64 s) { return juce::jmax (0.0, (double) (s - recordStartSample) / spb); };

    struct Pending { bool on = false; double startBeat = 0.0; float vel = 0.8f; };
    std::array<Pending, 128> pend;
    std::vector<Note> notes;
    for (int i = 0; i < count; ++i)
    {
        const auto& m = recordBuffer[(size_t) i].msg;
        const juce::int64 s = recordBuffer[(size_t) i].sample;
        if (m.isNoteOn())
            pend[(size_t) m.getNoteNumber()] = { true, toBeat (s), m.getFloatVelocity() };
        else if (m.isNoteOff() && pend[(size_t) m.getNoteNumber()].on)
        {
            auto& p = pend[(size_t) m.getNoteNumber()];
            notes.push_back ({ m.getNoteNumber(), p.startBeat, juce::jmax (0.05, toBeat (s) - p.startBeat), p.vel });
            p.on = false;
        }
    }
    const double stopBeat = toBeat (transport.getPlayheadSamples());
    for (int n = 0; n < 128; ++n)
        if (pend[(size_t) n].on)
            notes.push_back ({ n, pend[(size_t) n].startBeat, juce::jmax (0.05, stopBeat - pend[(size_t) n].startBeat), pend[(size_t) n].vel });
    if (notes.empty()) return;

    pushUndoSnapshot();
    double maxEnd = 0.0;
    for (auto& n : notes) maxEnd = juce::jmax (maxEnd, n.startBeat + n.lengthBeats);
    Clip c;
    c.name = "Recording";
    c.startBeat = (double) recordStartSample / spb;
    c.lengthBeats = juce::jmax (1.0, maxEnd);
    c.contentLenBeats = c.lengthBeats;
    c.looped = false;
    c.notes = std::move (notes);
    { const juce::ScopedLock sl (engineLock); t->clips.push_back (std::move (c)); }
    if (arrangeView) arrangeView->rebuild();
}

// ===========================================================================
// gRPC control API  (transport = atomic/direct; structural = message thread)
// ===========================================================================
void MainComponent::apiPlay()  { clearClipIndicators(); transport.setPlaying (true); }
void MainComponent::apiStop()  { if (recording.load()) finalizeRecording(); transport.setPlaying (false); transport.requestReset(); }
void MainComponent::apiStartRecording() { callOnMessageThread ([&] { startRecording(); return true; }); }
void MainComponent::apiStopRecording()  { callOnMessageThread ([&] { finalizeRecording(); transport.setPlaying (false); return true; }); }
void MainComponent::apiSetTempo (double bpm) { transport.setBpm (juce::jlimit (20.0, 400.0, bpm)); }
void MainComponent::apiSetSwing (double s)   { transport.setSwing (s); }
void MainComponent::apiSeek (double beats)   { transport.requestSeek (juce::jmax (0.0, beats)); }
void MainComponent::apiSetLoop (bool enabled, double startBeat, double endBeat)
{
    transport.setLoopRegion (juce::jmax (0.0, startBeat), juce::jmax (startBeat + 0.25, endBeat));
    transport.setLoopEnabled (enabled);
    if (loopButton.getToggleState() != enabled)
        juce::MessageManager::callAsync ([this, enabled] { loopButton.setToggleState (enabled, juce::dontSendNotification); });
}

MainComponent::TransportSnap MainComponent::apiGetTransport()
{
    return { transport.isPlaying(), transport.getBpm(), transport.getPlayheadBeats() };
}

int MainComponent::apiAddSynthTrack (const juce::String& name, int wave, float a, float d, float s, float r, float g)
{
    return callOnMessageThread ([&] () -> int
    {
        auto sg = std::make_unique<SynthGenerator>();
        auto& p = sg->engine.params;
        p.waveform.store (juce::jlimit (0, 3, wave));
        p.attack.store (a); p.decay.store (d); p.sustain.store (s); p.release.store (r); p.gain.store (g);
        auto t = std::make_unique<Track> (name.isNotEmpty() ? name : "Synth",
                                          std::move (sg), 60, paletteColour ((int) tracks.size()));
        Track* raw = t.get();
        addTrack (std::move (t));
        return raw->id;
    });
}

bool MainComponent::apiSetTrackParams (int id, bool hasVol, float vol, bool hasPan, float pan,
                                       bool hasMute, bool mute, bool hasSolo, bool solo,
                                       bool hasName, const juce::String& name)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (id);
        if (t == nullptr) return false;
        if (hasVol)  t->volume.store (juce::jlimit (0.0f, 1.0f, vol));
        if (hasPan)  t->pan.store (juce::jlimit (-1.0f, 1.0f, pan));
        if (hasMute) t->mute.store (mute);
        if (hasSolo) t->solo.store (solo);
        if (hasName) { const juce::ScopedLock sl (engineLock); t->name = name; }
        if (arrangeView) arrangeView->repaint();
        return true;
    });
}

bool MainComponent::apiSetSynthParam (int trackId, const juce::String& name, float value)
{
    // Hop to the message thread: although the synth params themselves are atomic,
    // resolveTrack hands back a raw Track* whose lifetime is only stable there —
    // a concurrent RemoveTrack/load (message thread, engineLock) could otherwise
    // free the Track while we dereference t->generator. (The OSC hook takes the
    // engineLock try-lock instead, to stay real-time safe.)
    return callOnMessageThread ([&] { return applySynthParam (resolveTrack (trackId), name, value); });
}

bool MainComponent::applySynthParam (Track* t, const juce::String& name, float value)
{
    {
        if (t == nullptr) return false;
        auto* sg = dynamic_cast<SynthGenerator*> (t->generator.get());
        if (sg == nullptr) return false;
        auto& p = sg->engine.params;
        const auto n = name.toLowerCase();

        // Oscillators
        if      (n == "wave")       p.waveform.store (juce::jlimit (0, 3, (int) value));
        else if (n == "osc2wave")   p.osc2Wave.store (juce::jlimit (0, 3, (int) value));
        else if (n == "osc2detune") p.osc2Detune.store (juce::jlimit (-1200.0f, 1200.0f, value));
        else if (n == "oscmix")     p.oscMix.store (juce::jlimit (0.0f, 1.0f, value));
        else if (n == "sub")        p.subLevel.store (juce::jlimit (0.0f, 1.0f, value));
        // Amp envelope
        else if (n == "attack")     p.attack.store  (juce::jmax (0.0f, value));
        else if (n == "decay")      p.decay.store   (juce::jmax (0.0f, value));
        else if (n == "sustain")    p.sustain.store (juce::jlimit (0.0f, 1.0f, value));
        else if (n == "release")    p.release.store (juce::jmax (0.0f, value));
        else if (n == "gain")       p.gain.store    (juce::jlimit (0.0f, 4.0f, value));
        // Filter + its envelope
        else if (n == "ftype")      p.filterType.store (juce::jlimit (0, 2, (int) value));
        else if (n == "cutoff")     p.cutoff.store    (juce::jlimit (20.0f, 20000.0f, value));
        else if (n == "reso")       p.resonance.store (juce::jlimit (0.5f, 20.0f, value));
        else if (n == "fenvamt")    p.filterEnvAmt.store (juce::jlimit (0.0f, 8.0f, value));
        else if (n == "fattack")    p.fAttack.store  (juce::jmax (0.0f, value));
        else if (n == "fdecay")     p.fDecay.store   (juce::jmax (0.0f, value));
        else if (n == "fsustain")   p.fSustain.store (juce::jlimit (0.0f, 1.0f, value));
        else if (n == "frelease")   p.fRelease.store (juce::jmax (0.0f, value));
        // LFO
        else if (n == "lfotarget")  p.lfoTarget.store (juce::jlimit (0, 2, (int) value));
        else if (n == "lforate")    p.lfoRate.store  (juce::jlimit (0.01f, 40.0f, value));
        else if (n == "lfodepth")   p.lfoDepth.store (juce::jlimit (0.0f, 1.0f, value));
        else return false;
        return true;
    }
}

std::vector<MainComponent::TrackSnap> MainComponent::apiListTracks()
{
    return callOnMessageThread ([&]
    {
        std::vector<TrackSnap> out;
        for (auto& t : tracks)
            out.push_back ({ t->id, t->name,
                             t->type == TrackType::Instrument ? juce::String ("instrument") : juce::String ("audio"),
                             t->volume.load(), t->pan.load(), t->mute.load(), (int) t->clips.size() });
        return out;
    });
}

int MainComponent::apiAddClip (int trackId, double start, double len, double content, bool looped,
                               const std::vector<Note>& notes, const juce::String& name)
{
    return callOnMessageThread ([&] () -> int
    {
        pushUndoSnapshot();
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return -1;
        Clip c;
        c.name = name; c.startBeat = start; c.lengthBeats = len;
        c.contentLenBeats = content > 0.0 ? content : len;
        c.looped = looped;
        c.notes = notes;
        int idx;
        {
            const juce::ScopedLock sl (engineLock);
            t->clips.push_back (std::move (c));
            idx = (int) t->clips.size() - 1;
        }
        emitChange ("clip_changed", trackId);
        if (arrangeView) arrangeView->repaint();
        return idx;
    });
}

std::vector<MainComponent::InsertSnap> MainComponent::apiListInserts()
{
    return callOnMessageThread ([&]
    {
        std::vector<InsertSnap> out;
        for (int i = 0; i < (int) mixerTracks.size(); ++i)
        {
            auto& mt = *mixerTracks[(size_t) i];
            InsertSnap s { i, mt.name, mt.volume.load(), mt.pan.load(), mt.mute.load(), mt.solo.load(), mt.isBus, {}, {} };
            for (int j = 0; j < (int) mt.effects.size(); ++j)
                s.effects.push_back ({ j, mt.effects[(size_t) j]->name(), mt.effects[(size_t) j]->bypassed.load() });
            for (auto& sd : mt.sends) s.sends.push_back ({ sd.bus, sd.level });
            out.push_back (std::move (s));
        }
        return out;
    });
}

bool MainComponent::apiSetInsertParams (int index, bool hasVol, float vol, bool hasPan, float pan,
                                        bool hasMute, bool mute, bool hasSolo, bool solo)
{
    return callOnMessageThread ([&] () -> bool
    {
        const juce::ScopedLock sl (engineLock);
        if (! juce::isPositiveAndBelow (index, (int) mixerTracks.size())) return false;
        auto& mt = *mixerTracks[(size_t) index];
        if (hasVol)  mt.volume.store (juce::jlimit (0.0f, 1.0f, vol));
        if (hasPan)  mt.pan.store (juce::jlimit (-1.0f, 1.0f, pan));
        if (hasMute) mt.mute.store (mute);
        if (hasSolo) mt.solo.store (solo);
        if (mixerView) mixerView->repaint();
        return true;
    });
}

int MainComponent::apiAddEffect (int insert, int type)
{
    return callOnMessageThread ([&] () -> int
    {
        pushUndoSnapshot();
        static const char* names[] = { "Gain", "Filter", "Delay", "Reverb", "Limiter", "Bitcrusher", "Compressor", "EQ", "Waveshaper" };
        if (type < 0 || type >= (int) (sizeof (names) / sizeof (names[0]))) return -1;
        auto fx = makeEffect (names[type]);
        if (fx == nullptr) return -1;
        int slot = -1;
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (insert, (int) mixerTracks.size())) return -1;
            mixerTracks[(size_t) insert]->effects.push_back (std::move (fx));
            slot = (int) mixerTracks[(size_t) insert]->effects.size() - 1;
        }
        emitChange ("effect_changed", -1, insert);
        if (mixerView) mixerView->rebuild();
        return slot;
    });
}

bool MainComponent::apiRemoveEffect (int insert, int slot)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        closeAllPluginWindows();
        bool ok = false;
        {
            const juce::ScopedLock sl (engineLock);
            if (juce::isPositiveAndBelow (insert, (int) mixerTracks.size()))
            {
                auto& fx = mixerTracks[(size_t) insert]->effects;
                if (juce::isPositiveAndBelow (slot, (int) fx.size())) { fx.erase (fx.begin() + slot); ok = true; }
            }
        }
        if (ok) emitChange ("effect_changed", -1, insert);
        if (mixerView) mixerView->rebuild();
        return ok;
    });
}

bool MainComponent::apiSetEffectParam (int insert, int slot, const juce::String& name, float value)
{
    return callOnMessageThread ([&] () -> bool
    {
        const juce::ScopedLock sl (engineLock);
        if (! juce::isPositiveAndBelow (insert, (int) mixerTracks.size())) return false;
        auto& fx = mixerTracks[(size_t) insert]->effects;
        if (! juce::isPositiveAndBelow (slot, (int) fx.size())) return false;
        for (auto& p : fx[(size_t) slot]->parameters())
            if (p.name.equalsIgnoreCase (name)) { p.set (value); return true; }
        return false;
    });
}

bool MainComponent::apiSetEffectBypass (int insert, int slot, bool bypassed)
{
    return callOnMessageThread ([&] () -> bool
    {
        const juce::ScopedLock sl (engineLock);
        if (! juce::isPositiveAndBelow (insert, (int) mixerTracks.size())) return false;
        auto& fx = mixerTracks[(size_t) insert]->effects;
        if (! juce::isPositiveAndBelow (slot, (int) fx.size())) return false;
        fx[(size_t) slot]->bypassed.store (bypassed);
        return true;
    });
}

std::vector<MainComponent::ParamSnap> MainComponent::apiGetEffectParams (int insert, int slot)
{
    return callOnMessageThread ([&]
    {
        std::vector<ParamSnap> out;
        const juce::ScopedLock sl (engineLock);
        if (juce::isPositiveAndBelow (insert, (int) mixerTracks.size()))
        {
            auto& fx = mixerTracks[(size_t) insert]->effects;
            if (juce::isPositiveAndBelow (slot, (int) fx.size()))
                for (auto& p : fx[(size_t) slot]->parameters())
                    out.push_back ({ p.name, p.get(), p.minValue, p.maxValue });
        }
        return out;
    });
}

bool MainComponent::apiSnapshotMeters (std::vector<float>& L, std::vector<float>& R, std::vector<char>& clip)
{
    const juce::ScopedTryLock stl (engineLock);
    if (! stl.isLocked()) return false;
    L.clear(); R.clear(); clip.clear();
    for (auto& mt : mixerTracks)
    {
        L.push_back (mt->peakL.load());
        R.push_back (mt->peakR.load());
        clip.push_back (mt->clipped.load() ? 1 : 0);
    }
    return true;
}

void MainComponent::clearClipIndicators()
{
    for (auto& mt : mixerTracks) mt->clipped.store (false);
}

// --- structural change events (one queue per active Subscribe consumer) ---
void MainComponent::emitChange (const juce::String& kind, int trackId, int insert)
{
    std::lock_guard<std::mutex> lk (changeSinksLock);
    for (auto& [id, s] : changeSinks)
    {
        std::lock_guard<std::mutex> sl (s->m);
        s->pending.push_back ({ kind, trackId, insert });
    }
}

int MainComponent::apiAddChangeSink()
{
    std::lock_guard<std::mutex> lk (changeSinksLock);
    const int id = nextSinkId++;
    changeSinks[id] = std::make_shared<ChangeSink>();
    return id;
}

void MainComponent::apiPollChanges (int sinkId, std::vector<ChangeSnap>& out)
{
    std::shared_ptr<ChangeSink> s;
    { std::lock_guard<std::mutex> lk (changeSinksLock);
      auto it = changeSinks.find (sinkId); if (it != changeSinks.end()) s = it->second; }
    if (s == nullptr) return;
    std::lock_guard<std::mutex> sl (s->m);
    out.swap (s->pending);
}

void MainComponent::apiRemoveChangeSink (int sinkId)
{
    std::lock_guard<std::mutex> lk (changeSinksLock);
    changeSinks.erase (sinkId);
}

// --- parameter automation ---
static float interpAuto (const std::vector<MainComponent::AutoPointSnap>& p, double beat)
{
    if (p.empty()) return 0.0f;
    if (beat <= p.front().beat) return p.front().value;
    if (beat >= p.back().beat)  return p.back().value;
    for (size_t i = 1; i < p.size(); ++i)
        if (beat <= p[i].beat)
        {
            const double t = (beat - p[i-1].beat) / juce::jmax (1e-9, p[i].beat - p[i-1].beat);
            return (float) (p[i-1].value + t * (p[i].value - p[i-1].value));
        }
    return p.back().value;
}

void MainComponent::evaluateAutomation (double beat)
{
    // Called from renderBlock while holding engineLock — iterate the locked vectors.
    for (auto& lane : automationLanes)
    {
        if (lane.points.empty()) continue;
        const float v = interpAuto (lane.points, beat);
        const bool insertOk = juce::isPositiveAndBelow (lane.id, (int) mixerTracks.size());
        switch (lane.type)
        {
            case 0: for (auto& t : tracks) if (t->id == lane.id) { t->volume.store (juce::jlimit (0.0f, 1.0f, v)); break; } break;
            case 1: for (auto& t : tracks) if (t->id == lane.id) { t->pan.store (juce::jlimit (-1.0f, 1.0f, v)); break; } break;
            case 2: if (insertOk) mixerTracks[(size_t) lane.id]->volume.store (juce::jlimit (0.0f, 1.0f, v)); break;
            case 3: if (insertOk) mixerTracks[(size_t) lane.id]->pan.store (juce::jlimit (-1.0f, 1.0f, v)); break;
            case 4:
                if (insertOk)
                {
                    auto& fx = mixerTracks[(size_t) lane.id]->effects;
                    if (juce::isPositiveAndBelow (lane.slot, (int) fx.size()))
                        for (auto& pr : fx[(size_t) lane.slot]->parameters())
                            if (pr.name.equalsIgnoreCase (lane.param)) { pr.set (v); break; }
                }
                break;
            default: break;
        }
    }
}

void MainComponent::apiSetAutomation (int type, int id, int slot, const juce::String& param,
                                      const std::vector<AutoPointSnap>& points)
{
    callOnMessageThread ([&]
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        automationLanes.erase (std::remove_if (automationLanes.begin(), automationLanes.end(),
            [&] (const AutoLaneSnap& l) { return l.type == type && l.id == id && l.slot == slot
                                                 && l.param.equalsIgnoreCase (param); }),
            automationLanes.end());
        if (! points.empty())
        {
            AutoLaneSnap lane { type, id, slot, param, points };
            std::sort (lane.points.begin(), lane.points.end(),
                       [] (const AutoPointSnap& a, const AutoPointSnap& b) { return a.beat < b.beat; });
            automationLanes.push_back (std::move (lane));
        }
        return true;
    });
}

std::vector<MainComponent::AutoLaneSnap> MainComponent::apiGetAutomation()
{
    return callOnMessageThread ([&] { const juce::ScopedLock sl (engineLock); return automationLanes; });
}

void MainComponent::apiNewProject()
{
    callOnMessageThread ([&] () -> bool { newProject(); return true; });
}

bool MainComponent::apiLoadProject (const juce::String& path)
{
    return callOnMessageThread ([&] () -> bool
    {
        auto f = juce::File::isAbsolutePath (path) ? juce::File (path)
                    : juce::File::getCurrentWorkingDirectory().getChildFile (path);
        // Accept a composition directory / gloopy.toml / .zip as well as a .gloopy file.
        if (f.isDirectory() && f.getChildFile ("gloopy.toml").existsAsFile())
            return loadComposition (f);
        if (f.getFileName() == "gloopy.toml" && f.existsAsFile())
            return loadComposition (f.getParentDirectory());
        if (f.existsAsFile() && f.hasFileExtension ("zip"))
            return loadComposition (f);
        if (! f.existsAsFile()) return false;
        openProject (f);
        return true;
    });
}

bool MainComponent::apiSaveComposition (const juce::String& path)
{
    return callOnMessageThread ([&] () -> bool
    {
        auto dir = juce::File::isAbsolutePath (path) ? juce::File (path)
                     : juce::File::getCurrentWorkingDirectory().getChildFile (path);
        return saveComposition (dir);
    });
}

bool MainComponent::apiLoadComposition (const juce::String& path)
{
    return callOnMessageThread ([&] () -> bool
    {
        auto dir = juce::File::isAbsolutePath (path) ? juce::File (path)
                     : juce::File::getCurrentWorkingDirectory().getChildFile (path);
        if (dir.getFileName() == "gloopy.toml") dir = dir.getParentDirectory();
        return loadComposition (dir);
    });
}

bool MainComponent::apiSaveProject (const juce::String& path)
{
    return callOnMessageThread ([&] () -> bool
    {
        auto f = juce::File::isAbsolutePath (path) ? juce::File (path)
                    : juce::File::getCurrentWorkingDirectory().getChildFile (path);
        saveProject (f.withFileExtension ("gloopy"));
        return true;
    });
}

// ── track & clip management ────────────────────────────────────────────────
bool MainComponent::apiRemoveTrack (int id)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        closeAllPluginWindows();
        bool ok = false;
        {
            const juce::ScopedLock sl (engineLock);
            for (size_t i = 0; i < tracks.size(); ++i)
                if (tracks[i]->id == id) { tracks.erase (tracks.begin() + (long) i); ok = true; break; }
        }
        if (! ok) return false;
        refreshTrackIds();
        emitChange ("track_removed", id);
        if (arrangeView) arrangeView->rebuild();
        selectClip (-1, -1);
        resized();
        return true;
    });
}

int MainComponent::apiAddAudioTrack (const juce::String& name)
{
    return callOnMessageThread ([&] () -> int
    {
        auto t = std::make_unique<Track> (name.isNotEmpty() ? name : "Audio", nullptr, 60,
                                          paletteColour ((int) tracks.size()), TrackType::Audio);
        Track* raw = t.get();
        addTrack (std::move (t));
        return raw->id;
    });
}

int MainComponent::apiAddSamplerTrack (const juce::String& name, const juce::String& path, int rootNote)
{
    return callOnMessageThread ([&] () -> int
    {
        juce::File f (path);
        if (! f.existsAsFile()) return -1;
        auto sampler = std::make_unique<Sampler>();
        sampler->prepare (currentSampleRate, currentBlockSize);
        if (! sampler->loadFile (f, formatManager)) return -1;
        const int root = rootNote > 0 ? rootNote : 60;
        sampler->setRootNote (root);
        auto t = std::make_unique<Track> (name.isNotEmpty() ? name : f.getFileNameWithoutExtension(),
                                          std::move (sampler), root, paletteColour ((int) tracks.size()));
        Track* raw = t.get();
        addTrack (std::move (t));
        return raw->id;
    });
}

int MainComponent::apiAddSfzTrack (const juce::String& name, const juce::String& path)
{
    return callOnMessageThread ([&] () -> int
    {
        pushUndoSnapshot();
        juce::File f = resolveSamplePath (path);
        auto sfz = std::make_unique<SfizzGenerator>();
        sfz->prepare (currentSampleRate, currentBlockSize);
        juce::String err;
        if (! sfz->loadSfz (f, err))
        {
            std::cout << "[sfz] load failed: " << err << std::endl;
            return -1;
        }
        auto t = std::make_unique<Track> (name.isNotEmpty() ? name : sfz->getName(),
                                          std::move (sfz), 60, paletteColour ((int) tracks.size()));
        Track* raw = t.get();
        addTrack (std::move (t));
        return raw->id;
    });
}

int MainComponent::apiAddPluginTrack (const juce::String& identifier)
{
    return callOnMessageThread ([&] () -> int
    {
        scanPlugins();
        auto desc = pluginHost.knownList.getTypeForIdentifierString (identifier);
        if (desc == nullptr) return -1;
        juce::String err;
        auto inst = pluginHost.create (*desc, currentSampleRate, currentBlockSize, err);
        if (inst == nullptr) return -1;
        auto gen = std::make_unique<PluginInstrument> (std::move (inst));
        auto t = std::make_unique<Track> (desc->name, std::move (gen), 60,
                                          paletteColour ((int) tracks.size()));
        Track* raw = t.get();
        addTrack (std::move (t));
        return raw->id;
    });
}

bool MainComponent::apiRemoveClip (int trackId, int index)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        bool ok = false;
        {
            const juce::ScopedLock sl (engineLock);
            if (juce::isPositiveAndBelow (index, (int) t->clips.size()))
                { t->clips.erase (t->clips.begin() + index); ok = true; }
        }
        if (ok) { emitChange ("clip_changed", trackId); if (arrangeView) arrangeView->rebuild(); selectClip (-1, -1); }
        return ok;
    });
}

bool MainComponent::apiMoveClip (int trackId, int index, double startBeat, bool hasToTrack, int toTrackId)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        Track* src = resolveTrack (trackId);
        Track* dst = hasToTrack ? resolveTrack (toTrackId) : src;
        if (src == nullptr || dst == nullptr) return false;
        bool ok = false, moved = false;
        {
            const juce::ScopedLock sl (engineLock);
            if (juce::isPositiveAndBelow (index, (int) src->clips.size()))
            {
                if (dst == src)
                    src->clips[(size_t) index].startBeat = juce::jmax (0.0, startBeat);
                else
                {
                    Clip c = src->clips[(size_t) index];
                    c.startBeat = juce::jmax (0.0, startBeat);
                    src->clips.erase (src->clips.begin() + index);
                    dst->clips.push_back (std::move (c));
                    moved = true;
                }
                ok = true;
            }
        }
        if (ok && arrangeView) arrangeView->rebuild();
        if (moved) selectClip (-1, -1);
        return ok;
    });
}

int MainComponent::apiAddAudioClip (int trackId, double startBeat, const juce::String& path, float gain)
{
    return callOnMessageThread ([&] () -> int
    {
        pushUndoSnapshot();
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return -1;
        juce::File f (path);
        if (! f.existsAsFile()) return -1;
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
        if (reader == nullptr || reader->lengthInSamples <= 0) return -1;
        auto buf = std::make_shared<juce::AudioBuffer<float>> (
            (int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (buf.get(), 0, (int) reader->lengthInSamples, 0, true, true);
        const double durationSec = (double) reader->lengthInSamples / reader->sampleRate;
        Clip c;
        c.type = ClipType::Audio;
        c.name = f.getFileNameWithoutExtension();
        c.startBeat = juce::jmax (0.0, startBeat);
        c.lengthBeats = juce::jmax (0.25, durationSec * transport.getBpm() / 60.0);
        c.audio = buf;
        c.audioSourceRate = reader->sampleRate;
        c.audioGain = gain > 0.0f ? gain : 1.0f;
        c.peaks = std::make_shared<std::vector<float>> (buildPeaks (*buf));
        int idx;
        {
            const juce::ScopedLock sl (engineLock);
            t->clips.push_back (std::move (c));
            idx = (int) t->clips.size() - 1;
        }
        if (arrangeView) arrangeView->rebuild();
        return idx;
    });
}

// ── plugins ─────────────────────────────────────────────────────────────────
std::vector<MainComponent::PluginSnap> MainComponent::apiScanPlugins (bool force)
{
    return callOnMessageThread ([&] { scanPlugins (force); return apiListPlugins(); });
}

std::vector<MainComponent::PluginSnap> MainComponent::apiListPlugins()
{
    return callOnMessageThread ([&]
    {
        scanPlugins();   // load cache on first use so a bare ListPlugins returns entries
        std::vector<PluginSnap> out;
        for (const auto& d : pluginHost.knownList.getTypes())
            out.push_back ({ d.name, d.pluginFormatName, d.isInstrument, d.createIdentifierString(),
                             d.manufacturerName, d.category, d.version, d.numInputChannels, d.numOutputChannels });
        return out;
    });
}

int MainComponent::apiAddPluginEffect (int insert, const juce::String& identifier)
{
    return callOnMessageThread ([&] () -> int
    {
        if (! juce::isPositiveAndBelow (insert, (int) mixerTracks.size())) return -1;
        pushUndoSnapshot();
        scanPlugins();
        auto desc = pluginHost.knownList.getTypeForIdentifierString (identifier);
        if (desc == nullptr) return -1;
        auto fx = makePluginEffect (*desc);
        if (fx == nullptr) return -1;
        int slot;
        {
            const juce::ScopedLock sl (engineLock);
            mixerTracks[(size_t) insert]->effects.push_back (std::move (fx));
            slot = (int) mixerTracks[(size_t) insert]->effects.size() - 1;
        }
        emitChange ("effect_changed", -1, insert);
        if (mixerView) mixerView->rebuild();
        return slot;
    });
}

bool MainComponent::apiOpenPluginEditor (int trackId)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (trackId);
        if (t == nullptr || t->generator == nullptr) return false;
        auto* proc = t->generator->getPluginInstance();
        if (proc == nullptr) return false;
        openPluginEditor (proc, t->name);
        return true;
    });
}

bool MainComponent::apiRenderToFile (const juce::String& path, double tailSeconds,
                                     double startBeat, double endBeat, bool hasTrack, int trackId)
{
    // Offline bounce. Runs on the calling (gRPC) thread and holds the engine lock
    // for the whole render, so the live audio callback is locked out (it outputs
    // silence) and the transport / mixer buffers are ours exclusively. Faster than
    // real time — we pump blocks in a tight loop rather than waiting on the device.
    // Optional [startBeat, endBeat) range and a single soloed track (stem export).
    juce::File f = juce::File::isAbsolutePath (path) ? juce::File (path)
                     : juce::File::getCurrentWorkingDirectory().getChildFile (path);
    f = f.withFileExtension ("wav");
    f.deleteFile();

    const juce::ScopedLock sl (engineLock);

    const bool        wasPlaying = transport.isPlaying();
    const juce::int64 savedHead  = transport.getPlayheadSamples();

    // Solo one track for a stem, remembering every track's solo to restore later.
    std::vector<bool> savedSolo;
    Track* soloT = hasTrack ? resolveTrack (trackId) : nullptr;
    if (soloT != nullptr)
        for (auto& t : tracks) { savedSolo.push_back (t->solo.load()); t->solo.store (t.get() == soloT); }

    auto restore = [&]
    {
        transport.setPlaying (false); transport.setPlayheadSamples (savedHead);
        transport.setPlaying (wasPlaying);
        if (soloT != nullptr)
            for (size_t i = 0; i < tracks.size() && i < savedSolo.size(); ++i) tracks[i]->solo.store (savedSolo[i]);
        for (auto& t : tracks) if (t->generator) { t->generator->allNotesOff(); t->generator->setFreewheeling (false); }
    };

    // Offline bounce runs faster than real time; put streaming generators (sfizz)
    // into synchronous-load mode so they don't drop to silence past their preload.
    for (auto& t : tracks) if (t->generator) t->generator->setFreewheeling (true);

    const double spb = juce::jmax (1.0, transport.samplesPerBeat());
    const juce::int64 startSample = (juce::int64) (juce::jmax (0.0, startBeat) * spb);

    // Start at the range beginning, playing, ignoring any live seek/reset or loop region.
    double dummy; transport.consumeSeek (dummy); transport.consumeReset();
    transport.setPlaying (true);
    transport.setPlayheadSamples (startSample);
    for (auto& t : tracks) if (t->generator) t->generator->allNotesOff();

    const int    block = juce::jmax (32, currentBlockSize);
    const double rate  = currentSampleRate;
    const double tail  = tailSeconds > 0.0 ? tailSeconds : 2.0;

    juce::WavAudioFormat fmt;
    auto os = f.createOutputStream();
    if (os == nullptr) { restore(); return false; }
    std::unique_ptr<juce::AudioFormatWriter> writer (fmt.createWriterFor (os.release(), rate, 2, 24, {}, 0));
    if (writer == nullptr) { restore(); return false; }

    juce::AudioBuffer<float> buf (2, block);
    const juce::int64 tailSamples = (juce::int64) (tail * rate);
    juce::int64 bodyLen = 0, target = 0, written = 0;
    const juce::int64 renderT0 = juce::Time::getHighResolutionTicks();

    for (;;)
    {
        buf.clear();
        const juce::int64 songLen = renderBlock (buf, 0, block, /*ignoreLoopWindow*/ true);
        if (target == 0)   // known after the first block
        {
            const juce::int64 endSample = endBeat > startBeat ? (juce::int64) (endBeat * spb) : songLen;
            bodyLen = juce::jmax ((juce::int64) 1, endSample - startSample);
            target  = bodyLen + tailSamples;
        }
        const int toWrite = (int) juce::jmin ((juce::int64) block, target - written);
        if (toWrite <= 0) break;
        writer->writeFromAudioSampleBuffer (buf, 0, toWrite);
        written += toWrite;
        if (written >= bodyLen) transport.setPlaying (false);   // stop triggering notes; let tails ring
        if (written >= target)  break;
    }

    writer.reset();   // flush + close the WAV

    const double wall = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - renderT0);
    if (wall > 1.0e-6) diagRenderSpeedX.store ((double) written / rate / wall, std::memory_order_relaxed);

    restore();
    return true;
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
    loadSelectedClipIntoEditor();
    // Arm the selected instrument track for live MIDI input.
    midiInputTarget.store (juce::isPositiveAndBelow (track, (int) tracks.size())
                             && tracks[(size_t) track]->generator != nullptr
                           ? tracks[(size_t) track]->id : -1);
}

void MainComponent::setEditorMode (int mode)
{
    editorMode = mode;
    editorPanel.pianoBtn.setToggleState (mode == 0, juce::dontSendNotification);
    editorPanel.stepBtn .setToggleState (mode == 1, juce::dontSendNotification);
    editorPanel.roll.setVisible  (mode == 0);
    editorPanel.steps.setVisible (mode == 1);
    loadSelectedClipIntoEditor();
}

void MainComponent::loadSelectedClipIntoEditor()
{
    bool valid = false, isAudio = false;
    std::vector<Note> notes;
    double contentLen = 4.0;
    int pitch = 60;
    juce::String trackName;
    {
        const juce::ScopedLock sl (engineLock);
        if (juce::isPositiveAndBelow (selTrack, (int) tracks.size())
              && juce::isPositiveAndBelow (selClip, (int) tracks[(size_t) selTrack]->clips.size()))
        {
            const auto& c = tracks[(size_t) selTrack]->clips[(size_t) selClip];
            trackName = tracks[(size_t) selTrack]->name;
            if (c.isAudio())
                isAudio = true;
            else
            {
                notes = c.notes;
                contentLen = c.looped ? c.contentLenBeats : c.lengthBeats;
                pitch = tracks[(size_t) selTrack]->defaultPitch;
                valid = true;
            }
        }
    }

    editorPanel.roll.setLength (contentLen);
    editorPanel.roll.loadNotes (notes);
    editorPanel.roll.setEnabledEditing (valid);
    editorPanel.steps.setContent (contentLen, pitch);
    editorPanel.steps.loadNotes (notes);
    editorPanel.steps.setEnabledEditing (valid);

    if (valid)
        editorPanel.title.setText ("EDITOR   \xe2\x80\xa2   " + trackName.toUpperCase() + "  \xe2\x80\xa2  CLIP",
                                   juce::dontSendNotification);
    else if (isAudio)
        editorPanel.title.setText ("EDITOR   \xe2\x80\xa2   AUDIO CLIP (no MIDI)", juce::dontSendNotification);
    else
        editorPanel.title.setText ("EDITOR", juce::dontSendNotification);
}

void MainComponent::writeBackEditor()
{
    auto notes = (editorMode == 0) ? editorPanel.roll.getNotes() : editorPanel.steps.getNotes();
    {
        const juce::ScopedLock sl (engineLock);
        if (juce::isPositiveAndBelow (selTrack, (int) tracks.size())
              && juce::isPositiveAndBelow (selClip, (int) tracks[(size_t) selTrack]->clips.size()))
        {
            auto& c = tracks[(size_t) selTrack]->clips[(size_t) selClip];
            if (! c.isAudio())
                c.notes = notes;
        }
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
    if (recordBuffer.empty()) recordBuffer.resize (1 << 18);   // ~260k events, preallocated

    const juce::ScopedLock sl (engineLock);
    mixBuffer.setSize (2, juce::jmax (16, samplesPerBlockExpected));
    for (auto& t : tracks)
    {
        if (t->generator) t->generator->prepare (sampleRate, samplesPerBlockExpected);
        t->liveMidi.reset (sampleRate);   // live OSC notes
    }
    for (auto& mt : mixerTracks)
    {
        mt->buffer.setSize (2, juce::jmax (16, samplesPerBlockExpected));
        for (auto& fx : mt->effects)
            fx->prepare (sampleRate, samplesPerBlockExpected, 2);
    }
}

juce::int64 MainComponent::renderBlock (juce::AudioBuffer<float>& outBuf, int start, int num, bool ignoreLoopWindow)
{
    auto* out = &outBuf;

    if (transport.consumeReset())
    {
        transport.setPlayheadSamples (0);
        for (auto& t : tracks) if (t->generator) t->generator->allNotesOff();
    }

    const bool        playing = transport.isPlaying();
    const double      spb     = transport.samplesPerBeat();
    const juce::int64 blockStartPlayhead = transport.getPlayheadSamples();   // for MIDI capture

    if (playing && ! automationLanes.empty())
        evaluateAutomation ((double) blockStartPlayhead / juce::jmax (1.0, spb));

    // LFO modulation is driven off the playhead time so a render is deterministic.
    if (! modulations.empty())
        evaluateModulation ((double) blockStartPlayhead / juce::jmax (1.0, currentSampleRate));

    // Seek (from dragging the playhead) — applies whether playing or stopped.
    double seekBeats = 0.0;
    if (transport.consumeSeek (seekBeats))
    {
        transport.setPlayheadSamples (juce::jmax ((juce::int64) 0, (juce::int64) std::llround (seekBeats * spb)));
        for (auto& t : tracks) if (t->generator) t->generator->allNotesOff();
    }

    // Song length = furthest clip end (min 1 bar).
    double songBeats = 4.0;
    for (auto& t : tracks)
        for (auto& c : t->clips)
            songBeats = juce::jmax (songBeats, c.endBeat());
    juce::int64 loopLen = (juce::int64) std::llround (songBeats * spb);
    if (loopLen < 1) loopLen = 1;

    // Playback window: the loop region if enabled, else the whole song.
    juce::int64 winStart = 0, winEnd = loopLen;
    if (transport.isLoopEnabled() && ! ignoreLoopWindow)
    {
        winStart = juce::jlimit ((juce::int64) 0, loopLen,
                                 (juce::int64) std::llround (transport.getLoopStartBeats() * spb));
        winEnd   = (juce::int64) std::llround (transport.getLoopEndBeats() * spb);
        if (winEnd - winStart < 1) winEnd = winStart + 1;
    }

    // Split the block into playback-window segments.
    struct Seg { juce::int64 loopStart; int chunk; int tsOffset; bool wrap; };
    std::array<Seg, 16> segs;
    int nseg = 0;
    if (playing)
    {
        juce::int64 ph = transport.getPlayheadSamples();
        int rem = num, local = 0;
        while (rem > 0 && nseg < (int) segs.size())
        {
            if (ph >= winEnd)
            {
                if (ignoreLoopWindow) break;   // offline bounce: play through once, never
                ph = winStart;                 // wrap to the top and retrigger the opening
            }
            const juce::int64 dist = winEnd - ph;
            if (dist < 1) break;
            const int chunk = (int) juce::jmin ((juce::int64) rem, dist);
            const bool wrap = (ph + chunk >= winEnd) && ! ignoreLoopWindow;
            segs[(size_t) nseg++] = { ph, chunk, local, wrap };
            ph += chunk;
            if (ph >= winEnd && ! ignoreLoopWindow) ph = winStart;
            rem -= chunk; local += chunk;
        }
        transport.setPlayheadSamples (ph);
    }

    const double deviceRate = currentSampleRate;

    // Collect a MIDI clip's notes over a song-sample window (content loops to fill).
    const double swing = transport.getSwing();
    auto collectClip = [spb, swing] (juce::MidiBuffer& midi, const Clip& clip,
                              juce::int64 songStart, int chunk, int tsOffset)
    {
        if (clip.type != ClipType::Midi) return;
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
                              tsOffset + (int) (winLo - songStart), spb, swing);
        }
    };

    // Render an audio clip's samples over a song-sample window (natural speed).
    auto renderAudioClip = [spb, deviceRate] (juce::AudioBuffer<float>& buffer, const Clip& clip,
                                              juce::int64 songStart, int chunk, int tsOffset)
    {
        if (! clip.isAudio() || clip.audio == nullptr || clip.muted) return;   // muted = inactive take
        const auto& ab = *clip.audio;
        const int frames = ab.getNumSamples();
        if (frames <= 0) return;

        const int nchSrc = ab.getNumChannels();
        const double ratio = clip.audioSourceRate / deviceRate;
        const juce::int64 clipStart = (juce::int64) std::llround (clip.startBeat * spb);
        const juce::int64 clipEnd   = (juce::int64) std::llround (clip.endBeat() * spb);

        for (int i = 0; i < chunk; ++i)
        {
            const juce::int64 songPos = songStart + i;
            if (songPos < clipStart || songPos >= clipEnd) continue;
            const double readPos = (double) (songPos - clipStart) * ratio;
            if (readPos >= frames - 1) continue;

            const int r0 = (int) readPos;
            const float fr = (float) (readPos - r0);
            const float l = ab.getSample (0, r0) * (1.0f - fr) + ab.getSample (0, r0 + 1) * fr;
            const float r = nchSrc > 1
                ? ab.getSample (1, r0) * (1.0f - fr) + ab.getSample (1, r0 + 1) * fr : l;
            buffer.addSample (0, tsOffset + i, l * clip.audioGain);
            buffer.addSample (1, tsOffset + i, r * clip.audioGain);
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
        mixBuffer.clear();

        if (t->generator != nullptr)   // instrument track
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
            // Merge live OSC notes (played whether or not the transport is running).
            juce::MidiBuffer live;
            t->liveMidi.removeNextBlockOfMessages (live, num);
            midi.addEvents (live, 0, num, 0);

            // Capture played input into the record buffer for the armed track.
            if (recording.load() && t->id == recordTrackId.load())
                for (const auto meta : live)
                {
                    const int idx = recordWrite.fetch_add (1, std::memory_order_relaxed);
                    if (idx < (int) recordBuffer.size())
                        recordBuffer[(size_t) idx] = { blockStartPlayhead + meta.samplePosition, meta.getMessage() };
                }

            t->generator->render (mixBuffer, midi, 0, num);
        }
        else if (playing)              // audio track
        {
            for (int s = 0; s < nseg; ++s)
                for (auto& c : t->clips)
                    renderAudioClip (mixBuffer, c, segs[(size_t) s].loopStart,
                                     segs[(size_t) s].chunk, segs[(size_t) s].tsOffset);
        }

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
        const float mpL = mt.buffer.getMagnitude (0, 0, num), mpR = mt.buffer.getMagnitude (1, 0, num);
        mt.peakL.store (mpL); mt.peakR.store (mpR);
        if (mpL >= 1.0f || mpR >= 1.0f) mt.clipped.store (true);

        // Aux sends: tap this insert's post-effects signal into its target buses.
        // Independent of mute/solo (a pre-fader-style aux). Buses have higher indices
        // (apiAddBus appends), so their buffers accumulate before they're processed.
        for (auto& sd : mt.sends)
            if (sd.level > 0.0f && sd.bus > 0 && sd.bus < numTracks && sd.bus != ti)
            {
                auto& bus = mixerTracks[(size_t) sd.bus]->buffer;
                bus.addFrom (0, 0, mt.buffer, 0, 0, num, sd.level);
                bus.addFrom (1, 0, mt.buffer, 1, 0, num, sd.level);
            }

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
    const float mpL = master.buffer.getMagnitude (0, 0, num), mpR = master.buffer.getMagnitude (1, 0, num);
    master.peakL.store (mpL); master.peakR.store (mpR);
    if (mpL >= 1.0f || mpR >= 1.0f) master.clipped.store (true);
    const float mv = master.volume.load();
    if (out->getNumChannels() > 0) out->addFrom (0, start, master.buffer, 0, 0, num, mv);
    if (out->getNumChannels() > 1) out->addFrom (1, start, master.buffer, 1, 0, num, mv);

    return loopLen;
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    captureRecordingInput (info);   // read input BEFORE it's cleared by the output pass
    info.clearActiveBufferRegion();
    auto* out = info.buffer;
    const int start = info.startSample;
    const int num   = info.numSamples;

    const juce::ScopedTryLock stl (engineLock);
    if (! stl.isLocked())
    {
        diagDropouts.fetch_add (1, std::memory_order_relaxed);   // block skipped -> silence
        return;
    }

    const bool live = ! renderMode.load();
    const juce::int64 t0 = live ? juce::Time::getHighResolutionTicks() : 0;
    const juce::int64 loopLen = renderBlock (*out, start, num, renderMode.load());
    if (live)
    {
        const double us = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1.0e6;
        diagLastCallbackUs.store (us, std::memory_order_relaxed);
        if (us > diagMaxCallbackUs.load (std::memory_order_relaxed)) diagMaxCallbackUs.store (us, std::memory_order_relaxed);
    }

    if (! renderMode.load())
        addMonitoring (info);   // dry input -> output for armed+monitor tracks

    // --- offline render capture (headless --render mode) ---
    if (renderMode.load())
    {
        if (renderWriter == nullptr)
        {
            renderSongLen = loopLen;
            renderTarget  = loopLen + (juce::int64) (2.0 * currentSampleRate);  // + reverb/delay tail
            renderWritten = 0;
            juce::WavAudioFormat fmt;
            if (auto os = renderFile.createOutputStream())
                renderWriter.reset (fmt.createWriterFor (os.release(), currentSampleRate, 2, 24, {}, 0));
        }
        if (renderWriter != nullptr && renderWritten < renderTarget)
        {
            const int toWrite = (int) juce::jmin ((juce::int64) num, renderTarget - renderWritten);
            renderWriter->writeFromAudioSampleBuffer (*out, start, toWrite);
            renderWritten += toWrite;
            if (renderWritten >= renderSongLen) transport.setPlaying (false);   // let tails ring
            if (renderWritten >= renderTarget)
            {
                renderWriter.reset();
                renderMode = false;
                renderFinished = true;
            }
        }
    }
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

void MainComponent::openNotes()
{
    if (notesWindow == nullptr)
    {
        notesEditor.setMultiLine (true);
        notesEditor.setReturnKeyStartsNewLine (true);
        notesEditor.setScrollbarsShown (true);
        notesEditor.setFont (juce::Font (juce::FontOptions (14.0f)));
        notesEditor.setText (projectNotes, juce::dontSendNotification);
        notesEditor.onTextChange = [this] { projectNotes = notesEditor.getText(); };

        auto w = std::make_unique<HideOnCloseWindow>();
        w->setName ("Project Notes");
        w->setContentNonOwned (&notesEditor, false);
        w->setResizable (true, false);
        w->setSize (520, 460);
        w->centreWithSize (520, 460);
        notesWindow = std::move (w);
    }
    notesEditor.setText (projectNotes, juce::dontSendNotification);
    notesWindow->setVisible (true);
    notesWindow->toFront (true);
}

void MainComponent::beginRenderMode (const juce::File& out)
{
    renderFile = out;
    renderFile.deleteFile();
    transport.requestReset();
    transport.setPlaying (true);
    renderMode = true;
}

// ===========================================================================
// Plugins
// ===========================================================================
void MainComponent::scanPlugins (bool force)
{
    if (! force && pluginsScanned) return;
    pluginsScanned = true;
    if (! force && pluginHost.loadCache()) return;   // instant restore from cache
    pluginHost.scanAll();                             // slow full scan, then caches
}

juce::PluginDescription MainComponent::resolvePluginDescription (const juce::PluginDescription& saved)
{
    // A saved description's fileOrIdentifier can point at a binary the format
    // manager won't load directly (e.g. a VST3 bundle's inner .so). Match it
    // against the freshly scanned list, whose paths are known-good.
    scanPlugins();
    const juce::PluginDescription* fallback = nullptr;
    for (const auto& kd : pluginHost.knownList.getTypes())
    {
        if (kd.pluginFormatName != saved.pluginFormatName || kd.name != saved.name) continue;
        if (kd.uniqueId == saved.uniqueId) return kd;   // exact match
        if (fallback == nullptr) fallback = &kd;        // same name+format
    }
    return fallback != nullptr ? *fallback : saved;
}

void MainComponent::showAddPluginMenu()
{
    scanPlugins();
    const auto instruments = pluginHost.plugins (true);

    juce::PopupMenu menu;
    if (instruments.isEmpty())
        menu.addItem (1, "No instrument plugins found", false);
    else
        for (int i = 0; i < instruments.size(); ++i)
            menu.addItem (100 + i, instruments[i].name + "  (" + instruments[i].pluginFormatName + ")");
    menu.addSeparator();
    menu.addItem (2, "Rescan plugins");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addPluginBtn),
        [this, instruments] (int r)
        {
            if (r == 2) { scanPlugins (true); }
            else if (r >= 100 && r - 100 < instruments.size())
                createInstrumentTrack (instruments[r - 100]);
        });
}

void MainComponent::createInstrumentTrack (const juce::PluginDescription& desc)
{
    juce::String err;
    auto inst = pluginHost.create (desc, currentSampleRate, currentBlockSize, err);
    if (inst == nullptr) return;
    auto gen = std::make_unique<PluginInstrument> (std::move (inst));
    addTrack (std::make_unique<Track> (desc.name, std::move (gen), 60,
                                       paletteColour ((int) tracks.size())));
}

std::unique_ptr<Effect> MainComponent::makePluginEffect (const juce::PluginDescription& desc)
{
    juce::String err;
    auto inst = pluginHost.create (desc, currentSampleRate, currentBlockSize, err);
    if (inst == nullptr) return nullptr;
    auto fx = std::make_unique<PluginEffect> (std::move (inst));
    fx->prepare (currentSampleRate, currentBlockSize, 2);
    return fx;
}

void MainComponent::openPluginEditor (juce::AudioProcessor* p, const juce::String& title)
{
    if (p == nullptr) return;

    // A window that hosts a plugin editor and tears it down safely: a native
    // editor (from createEditorIfNeeded) must be told to the processor via
    // editorBeingDeleted; a generic editor we simply own.
    struct EditorWindow : public juce::DocumentWindow
    {
        EditorWindow (const juce::String& n)
            : DocumentWindow (n, Palette::bg, DocumentWindow::closeButton) { setUsingNativeTitleBar (true); }
        ~EditorWindow() override
        {
            clearContentComponent();
            if (editor != nullptr)
            {
                if (nativeEditor && plugin != nullptr) plugin->editorBeingDeleted (editor);
                delete editor;
            }
        }
        void closeButtonPressed() override { if (onClose) onClose(); }

        juce::AudioProcessor*       plugin { nullptr };
        juce::AudioProcessorEditor* editor { nullptr };
        bool nativeEditor { false };
        std::function<void()> onClose;
    };

    juce::AudioProcessorEditor* editor = nullptr;
    bool nativeEditor = false;
    if (p->hasEditor())
    {
        editor = p->createEditorIfNeeded();     // the plugin's own UI, if any
        nativeEditor = (editor != nullptr);
    }
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (*p);   // fallback: generic knobs

    auto* w = new EditorWindow (title);
    w->plugin = p;
    w->editor = editor;
    w->nativeEditor = nativeEditor;
    w->setContentNonOwned (editor, true);
    w->setResizable (! nativeEditor || editor->isResizable(), false);
    w->centreWithSize (juce::jmax (380, editor->getWidth()),
                       juce::jlimit (260, 760, editor->getHeight() > 0 ? editor->getHeight() : 480));
    w->onClose = [this, w] { juce::MessageManager::callAsync ([this, w] { pluginWindows.removeObject (w); }); };
    w->setVisible (true);
    pluginWindows.add (w);
}

void MainComponent::closeAllPluginWindows()
{
    pluginWindows.clear();
}

// ---------------------------------------------------------------------------
// GUI
// ---------------------------------------------------------------------------
void MainComponent::timerCallback()
{
    if (audioRecActive.load() && loopRecRotate.exchange (false))
        rotateLoopTakes();   // loop recording: finalize the pass, start a new take

    if (renderFinished.load())
    {
        renderFinished = false;
        if (auto* app = juce::JUCEApplication::getInstance())
            app->systemRequestedQuit();
        return;
    }

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

    addSynthBtn  .setBounds (bar.removeFromLeft (64)); bar.removeFromLeft (5);
    loadSampleBtn.setBounds (bar.removeFromLeft (74)); bar.removeFromLeft (5);
    addSfzBtn    .setBounds (bar.removeFromLeft (56)); bar.removeFromLeft (5);
    addAudioBtn  .setBounds (bar.removeFromLeft (68)); bar.removeFromLeft (5);
    addPluginBtn .setBounds (bar.removeFromLeft (72));
    mixerButton  .setBounds (bar.removeFromRight (58)); bar.removeFromRight (6);
    loopButton   .setBounds (bar.removeFromRight (54));

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
// Open a .gloopy file, a composition directory (or its gloopy.toml), or a .zip.
void MainComponent::openAny (const juce::File& f)
{
    if (f.isDirectory() && f.getChildFile ("gloopy.toml").existsAsFile())      loadComposition (f);
    else if (f.getFileName() == "gloopy.toml" && f.existsAsFile())             loadComposition (f.getParentDirectory());
    else if (f.existsAsFile() && f.hasFileExtension ("zip"))                   loadComposition (f);
    else if (f.existsAsFile())                                                 openProject (f);
}

void MainComponent::showFileMenu()
{
    juce::PopupMenu menu;
    const bool haveProject = currentProjectFile != juce::File();
    const bool isComposition = currentProjectFile.getFileName() == "gloopy.toml";
    menu.addItem (1, "New Project");
    menu.addItem (2, "Open...");                         // .gloopy or .zip
    menu.addItem (6, "Open Composition Folder...");
    menu.addSeparator();
    menu.addItem (3, "Save", haveProject);
    menu.addItem (4, "Save As .gloopy...");
    menu.addItem (7, "Save As Composition...");
    menu.addSeparator();
    menu.addItem (8, "Project Notes...");
    menu.addItem (5, "Rescan Plugins");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fileButton),
        [this, isComposition] (int result)
        {
            if (result == 8) { openNotes(); return; }
            if (result == 1) newProject();
            else if (result == 2)
            {
                fileChooser = std::make_unique<juce::FileChooser> ("Open project", juce::File(), "*.gloopy;*.zip");
                fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [this] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) openAny (fc.getResult()); });
            }
            else if (result == 6)
            {
                fileChooser = std::make_unique<juce::FileChooser> ("Open composition folder", juce::File());
                fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                    [this] (const juce::FileChooser& fc) { if (fc.getResult().isDirectory()) openAny (fc.getResult()); });
            }
            else if (result == 3)   // Save — same format the project was opened as
            {
                if (isComposition) saveComposition (currentProjectFile.getParentDirectory());
                else               saveProject (currentProjectFile);
            }
            else if (result == 4)
            {
                fileChooser = std::make_unique<juce::FileChooser> ("Save as .gloopy", juce::File(), "*.gloopy");
                fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                            | juce::FileBrowserComponent::warnAboutOverwriting,
                    [this] (const juce::FileChooser& fc)
                    { auto f = fc.getResult(); if (f != juce::File()) saveProject (f.withFileExtension ("gloopy")); });
            }
            else if (result == 7)
            {
                fileChooser = std::make_unique<juce::FileChooser> ("Save as composition folder", juce::File());
                fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectDirectories,
                    [this] (const juce::FileChooser& fc)
                    { auto d = fc.getResult(); if (d != juce::File()) saveComposition (d); });
            }
            else if (result == 5) scanPlugins (true);
        });
}

void MainComponent::newProject()
{
    closeAllPluginWindows();
    {
        const juce::ScopedLock sl (engineLock);
        transport.setPlaying (false);
        tracks.clear();
    }
    nextTrackId = 0;
    projectNotes.clear();
    if (notesWindow != nullptr) notesEditor.setText ({}, juce::dontSendNotification);
    setupMixer();
    undoSuppressed = true;
    setupDefaultProject();
    undoSuppressed = false;
    undoStack.clear(); redoStack.clear();
    currentProjectFile = juce::File();
    refreshUiAfterLoad();
}

// --- snapshot-based undo/redo (reuses the save/load serialization) ---
void MainComponent::pushUndoSnapshot()
{
    if (undoSuppressed) return;
    undoStack.push_back (toValueTree());
    while (undoStack.size() > 32) undoStack.erase (undoStack.begin());   // bounded
    redoStack.clear();
}

void MainComponent::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (toValueTree());
    auto prev = undoStack.back(); undoStack.pop_back();
    undoSuppressed = true;  loadFromTree (prev);  refreshUiAfterLoad();  undoSuppressed = false;
}

void MainComponent::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (toValueTree());
    auto next = redoStack.back(); redoStack.pop_back();
    undoSuppressed = true;  loadFromTree (next);  refreshUiAfterLoad();  undoSuppressed = false;
}

void MainComponent::apiUndo() { callOnMessageThread ([&] { undo(); return true; }); }
void MainComponent::apiRedo() { callOnMessageThread ([&] { redo(); return true; }); }

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    using MK = juce::ModifierKeys;
    if (key == juce::KeyPress ('z', MK::commandModifier, 0))                      { undo(); return true; }
    if (key == juce::KeyPress ('z', MK::commandModifier | MK::shiftModifier, 0))  { redo(); return true; }
    if (key == juce::KeyPress ('y', MK::commandModifier, 0))                      { redo(); return true; }
    return false;
}

juce::ValueTree MainComponent::toValueTree()
{
    const juce::ScopedLock sl (engineLock);
    juce::ValueTree root ("GLOOPY");
    root.setProperty ("version", 2, nullptr);
    root.setProperty ("bpm", transport.getBpm(), nullptr);
    root.setProperty ("swing", transport.getSwing(), nullptr);
    root.setProperty ("notes", projectNotes, nullptr);
    root.setProperty ("scaleRoot", scaleRoot, nullptr);
    root.setProperty ("scaleName", scaleName, nullptr);
    { juce::StringArray iv; for (int i : scaleIntervals) iv.add (juce::String (i));
      root.setProperty ("scaleIntervals", iv.joinIntoString (","), nullptr); }

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
        tr.setProperty ("type", (int) t->type, nullptr);
        if (t->generator) tr.setProperty ("gen", t->generator->typeName(), nullptr);

        if (auto* sg = dynamic_cast<SynthGenerator*> (t->generator.get()))
        {
            juce::ValueTree s ("SYNTH");
            writeSynthParams (s, sg->engine.params);
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
        else if (auto* sf = dynamic_cast<SfizzGenerator*> (t->generator.get()))
        {
            // Store the .sfz path in portable (relative-to-sample-root) form when
            // possible — sfizz re-parses, and resolveSamplePath resolves it on load.
            juce::ValueTree s ("SFZ");
            s.setProperty ("path", portableSamplePath (sf->getSfzPath()), nullptr);
            tr.addChild (s, -1, nullptr);
        }
        else if (auto* proc = t->generator ? t->generator->getPluginInstance() : nullptr)
        {
            juce::ValueTree pv ("PLUGIN");
            if (auto* inst = dynamic_cast<juce::AudioPluginInstance*> (proc))
            {
                juce::PluginDescription d;
                inst->fillInPluginDescription (d);
                if (auto xml = d.createXml()) pv.setProperty ("pdesc", xml->toString(), nullptr);
            }
            juce::MemoryBlock st; proc->getStateInformation (st);
            pv.setProperty ("pstate", st.toBase64Encoding(), nullptr);
            tr.addChild (pv, -1, nullptr);
        }

        for (auto& c : t->clips)
        {
            juce::ValueTree cl ("CLIP");
            cl.setProperty ("ctype", (int) c.type, nullptr);
            cl.setProperty ("name", c.name, nullptr);
            cl.setProperty ("start", c.startBeat, nullptr);
            cl.setProperty ("len", c.lengthBeats, nullptr);
            cl.setProperty ("content", c.contentLenBeats, nullptr);
            cl.setProperty ("looped", c.looped, nullptr);
            if (c.muted) cl.setProperty ("muted", true, nullptr);
            if (c.isAudio() && c.audioFile.isNotEmpty())
            {
                // Referenced audio (recorded take / import) — store the path, not the blob.
                cl.setProperty ("afile", c.audioFile, nullptr);
                cl.setProperty ("again", c.audioGain, nullptr);
                if (c.takeId.isNotEmpty()) cl.setProperty ("take", c.takeId, nullptr);
            }
            else if (c.isAudio() && c.audio != nullptr)
            {
                const auto& ab = *c.audio;
                cl.setProperty ("arate", c.audioSourceRate, nullptr);
                cl.setProperty ("again", c.audioGain, nullptr);
                cl.setProperty ("achannels", ab.getNumChannels(), nullptr);
                cl.setProperty ("aframes", ab.getNumSamples(), nullptr);
                juce::MemoryBlock mb ((size_t) ab.getNumChannels() * (size_t) ab.getNumSamples() * sizeof (float));
                auto* dst = (float*) mb.getData();
                for (int ch = 0; ch < ab.getNumChannels(); ++ch)
                    for (int i = 0; i < ab.getNumSamples(); ++i)
                        *dst++ = ab.getSample (ch, i);
                cl.setProperty ("adata", juce::Base64::toBase64 (mb.getData(), mb.getSize()), nullptr);
            }
            else
            {
                for (auto& n : c.notes)
                {
                    juce::ValueTree nt ("NOTE");
                    nt.setProperty ("pitch", n.pitch, nullptr);
                    nt.setProperty ("start", n.startBeat, nullptr);
                    nt.setProperty ("nlen", n.lengthBeats, nullptr);
                    nt.setProperty ("vel", n.velocity, nullptr);
                    cl.addChild (nt, -1, nullptr);
                }
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
        if (mt->isBus) t.setProperty ("bus", true, nullptr);
        for (auto& sd : mt->sends)
        {
            juce::ValueTree sv ("SEND");
            sv.setProperty ("to", sd.bus, nullptr); sv.setProperty ("level", sd.level, nullptr);
            t.addChild (sv, -1, nullptr);
        }
        for (auto& fx : mt->effects)
        {
            juce::ValueTree f ("FX");
            f.setProperty ("bypass", fx->bypassed.load(), nullptr);
            if (auto* proc = fx->getPluginInstance())
            {
                f.setProperty ("type", "Plugin", nullptr);
                if (auto* inst = dynamic_cast<juce::AudioPluginInstance*> (proc))
                {
                    juce::PluginDescription d;
                    inst->fillInPluginDescription (d);
                    if (auto xml = d.createXml()) f.setProperty ("pdesc", xml->toString(), nullptr);
                }
                juce::MemoryBlock st; proc->getStateInformation (st);
                f.setProperty ("pstate", st.toBase64Encoding(), nullptr);
            }
            else
            {
                f.setProperty ("type", fx->name(), nullptr);
                for (auto& pr : fx->parameters())
                {
                    juce::ValueTree pv ("PARAM");
                    pv.setProperty ("name", pr.name, nullptr);
                    pv.setProperty ("value", pr.get(), nullptr);
                    f.addChild (pv, -1, nullptr);
                }
            }
            t.addChild (f, -1, nullptr);
        }
        mx.addChild (t, -1, nullptr);
    }
    root.addChild (mx, -1, nullptr);

    juce::ValueTree au ("AUTOMATION");
    for (auto& lane : automationLanes)
    {
        juce::ValueTree l ("LANE");
        l.setProperty ("type", lane.type, nullptr); l.setProperty ("id", lane.id, nullptr);
        l.setProperty ("slot", lane.slot, nullptr); l.setProperty ("param", lane.param, nullptr);
        for (auto& p : lane.points)
        {
            juce::ValueTree pt ("PT");
            pt.setProperty ("beat", p.beat, nullptr); pt.setProperty ("value", p.value, nullptr);
            l.addChild (pt, -1, nullptr);
        }
        au.addChild (l, -1, nullptr);
    }
    root.addChild (au, -1, nullptr);

    juce::ValueTree locs ("LOCATIONS");
    for (auto& l : locations)
    {
        juce::ValueTree lv ("LOC");
        lv.setProperty ("name", l.name, nullptr);   lv.setProperty ("kind", l.kind, nullptr);
        lv.setProperty ("start", l.startBeat, nullptr); lv.setProperty ("end", l.endBeat, nullptr);
        locs.addChild (lv, -1, nullptr);
    }
    root.addChild (locs, -1, nullptr);

    juce::ValueTree exps ("EXPORTS");
    for (auto& p : exportProfiles)
    {
        juce::ValueTree ev ("EXPORT");
        ev.setProperty ("name", p.name, nullptr);       ev.setProperty ("target", p.target, nullptr);
        ev.setProperty ("range", p.rangeName, nullptr);  ev.setProperty ("format", p.format, nullptr);
        ev.setProperty ("track", p.trackId, nullptr);    ev.setProperty ("tail", p.tailSeconds, nullptr);
        exps.addChild (ev, -1, nullptr);
    }
    root.addChild (exps, -1, nullptr);

    juce::ValueTree scs ("SCENES");
    for (auto& sc : mixerScenes)
    {
        juce::ValueTree sv ("SCENE");
        sv.setProperty ("name", sc.name, nullptr);
        for (auto& in : sc.inserts)
        {
            juce::ValueTree iv ("INSERT");
            iv.setProperty ("vol", in.volume, nullptr);  iv.setProperty ("pan", in.pan, nullptr);
            iv.setProperty ("mute", in.mute, nullptr);   iv.setProperty ("solo", in.solo, nullptr);
            juce::String bp; for (auto b : in.bypass) bp << (b ? '1' : '0');
            iv.setProperty ("bypass", bp, nullptr);
            sv.addChild (iv, -1, nullptr);
        }
        scs.addChild (sv, -1, nullptr);
    }
    root.addChild (scs, -1, nullptr);

    juce::ValueTree mods ("MODS");
    for (auto& m : modulations)
    {
        juce::ValueTree mv ("MOD");
        mv.setProperty ("target", m.target, nullptr); mv.setProperty ("rate", m.rate, nullptr);
        mv.setProperty ("depth", m.depth, nullptr);   mv.setProperty ("center", m.center, nullptr);
        mv.setProperty ("shape", m.shape, nullptr);
        mods.addChild (mv, -1, nullptr);
    }
    root.addChild (mods, -1, nullptr);

    juce::ValueTree tm ("TEMPOMAP");
    for (auto& mk : tempoMap)
    {
        juce::ValueTree v ("TM");
        v.setProperty ("beat", mk.beat, nullptr); v.setProperty ("bpm", mk.bpm, nullptr);
        tm.addChild (v, -1, nullptr);
    }
    root.addChild (tm, -1, nullptr);

    juce::ValueTree ctl ("CONTROLLERS");
    for (auto& m : controllerMaps)
    {
        juce::ValueTree v ("CTRL");
        v.setProperty ("source", m.source, nullptr); v.setProperty ("target", m.target, nullptr);
        v.setProperty ("lo", m.lo, nullptr);         v.setProperty ("hi", m.hi, nullptr);
        ctl.addChild (v, -1, nullptr);
    }
    root.addChild (ctl, -1, nullptr);
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
    currentProjectFile = file;   // set first so sample-path resolution can use the project dir
    if (auto xml = juce::parseXML (file))
    {
        undoSuppressed = true;
        loadFromTree (juce::ValueTree::fromXml (*xml));
        undoSuppressed = false;
        undoStack.clear(); redoStack.clear();
        refreshUiAfterLoad();
    }
}

// Resolve a stored sample/SFZ reference to an actual file. Handles absolute
// paths (backward compatible), ~ expansion, and portable relative paths — which
// are searched against $GLOOPY_SAMPLE_PATH (colon-separated), then the project's
// own directory, then ~/sfz. First existing hit wins.
// Sample search roots, highest priority first: $GLOOPY_SAMPLE_PATH entries, the
// current project's directory, then ~/sfz. Shared by resolve (load) and portable
// (save) so the two round-trip.
juce::StringArray MainComponent::sampleSearchRoots() const
{
    juce::StringArray roots;
    const auto env = juce::SystemStats::getEnvironmentVariable ("GLOOPY_SAMPLE_PATH", {});
    if (env.isNotEmpty())
        roots.addTokens (env, ":", "");
    if (currentProjectFile.existsAsFile())
        roots.add (currentProjectFile.getParentDirectory().getFullPathName());
    roots.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("sfz").getFullPathName());
    return roots;
}

juce::File MainComponent::resolveSamplePath (const juce::String& stored) const
{
    if (stored.isEmpty()) return {};

    juce::String s = stored;
    if (s.startsWithChar ('~'))
        s = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName()
              + s.substring (1);

    if (juce::File::isAbsolutePath (s))
        return juce::File (s);

    for (auto& r : sampleSearchRoots())
    {
        if (r.isEmpty()) continue;
        auto cand = juce::File (r).getChildFile (s);
        if (cand.existsAsFile()) return cand;
    }
    return juce::File (s);   // fallback: return as-is so the error names the path
}

// Inverse of resolveSamplePath: express an absolute sample path relative to a
// known search root when possible, so saved projects stay portable (a path added
// via gRPC as "VPO/x.sfz" resolves to absolute at load, but must save relative
// again). Falls back to the absolute path when the file is under no known root.
juce::String MainComponent::portableSamplePath (const juce::String& absolute) const
{
    if (absolute.isEmpty() || ! juce::File::isAbsolutePath (absolute))
        return absolute;   // already relative / empty — keep as-is
    const juce::File file (absolute);
    for (auto& r : sampleSearchRoots())
    {
        if (r.isEmpty()) continue;
        const juce::File rootDir (r);
        if (file.isAChildOf (rootDir))
            return file.getRelativePathFrom (rootDir);
    }
    return absolute;
}

void MainComponent::loadFromTree (const juce::ValueTree& root)
{
    if (! root.hasType ("GLOOPY")) return;

    closeAllPluginWindows();
    const juce::ScopedLock sl (engineLock);
    transport.setPlaying (false);
    tracks.clear();
    mixerTracks.clear();
    locations.clear();
    exportProfiles.clear();
    mixerScenes.clear();
    modulations.clear();
    tempoMap.clear();
    controllerMaps.clear();
    automationLanes.clear();
    nextTrackId = 0;

    auto trks = root.getChildWithName ("TRACKS");
    for (int i = 0; i < trks.getNumChildren(); ++i)
    {
        auto tr = trks.getChild (i);
        const int ttype = (int) tr.getProperty ("type", (int) TrackType::Instrument);
        const juce::String genType = tr.getProperty ("gen", "Synth").toString();
        std::unique_ptr<Generator> gen;

        if (ttype == (int) TrackType::Audio)
        {
            gen = nullptr;   // audio track has no generator
        }
        else if (genType == "Plugin")
        {
            auto pv = tr.getChildWithName ("PLUGIN");
            juce::PluginDescription d;
            if (auto xml = juce::parseXML (pv.getProperty ("pdesc").toString())) d.loadFromXml (*xml);
            d = resolvePluginDescription (d);
            juce::String err;
            if (auto inst = pluginHost.create (d, currentSampleRate, currentBlockSize, err))
            {
                juce::MemoryBlock st; st.fromBase64Encoding (pv.getProperty ("pstate").toString());
                if (st.getSize() > 0) inst->setStateInformation (st.getData(), (int) st.getSize());
                gen = std::make_unique<PluginInstrument> (std::move (inst));
            }
            else
                std::cout << "[load] plugin '" << d.name << "' create failed: " << err << std::endl;
        }
        else if (genType == "Synth")
        {
            auto sg = std::make_unique<SynthGenerator>();
            readSynthParams (tr.getChildWithName ("SYNTH"), sg->engine.params);
            gen = std::move (sg);
        }
        else if (genType == "Sfz")
        {
            auto s = tr.getChildWithName ("SFZ");
            auto sf = std::make_unique<SfizzGenerator>();
            sf->prepare (currentSampleRate, currentBlockSize);
            juce::String err;
            if (sf->loadSfz (resolveSamplePath (s.getProperty ("path", "").toString()), err))
                gen = std::move (sf);
            else
                std::cout << "[load] SFZ load failed: " << err << std::endl;
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
        if (gen) gen->prepare (currentSampleRate, currentBlockSize);

        auto t = std::make_unique<Track> (tr.getProperty ("name", "Track").toString(),
                    std::move (gen), (int) tr.getProperty ("pitch", 60),
                    juce::Colour ((juce::uint32) (int) tr.getProperty ("colour", (int) 0xff4a90d9)),
                    (TrackType) ttype);
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
            c.type = (ClipType) (int) cl.getProperty ("ctype", (int) ClipType::Midi);
            c.name = cl.getProperty ("name", "").toString();
            c.startBeat = (double) cl.getProperty ("start", 0.0);
            c.lengthBeats = (double) cl.getProperty ("len", 4.0);
            c.contentLenBeats = (double) cl.getProperty ("content", 4.0);
            c.looped = (bool) cl.getProperty ("looped", true);
            c.muted  = (bool) cl.getProperty ("muted", false);

            if (c.isAudio() && cl.hasProperty ("afile"))
            {
                // Referenced audio: resolve the path and load the file for playback.
                c.audioFile = cl.getProperty ("afile").toString();
                c.takeId    = cl.getProperty ("take", "").toString();
                c.audioGain = (float) (double) cl.getProperty ("again", 1.0);
                const auto f = resolveSamplePath (c.audioFile);
                if (std::unique_ptr<juce::AudioFormatReader> r (formatManager.createReaderFor (f)); r != nullptr)
                {
                    auto buf = std::make_shared<juce::AudioBuffer<float>> ((int) r->numChannels, (int) r->lengthInSamples);
                    r->read (buf.get(), 0, (int) r->lengthInSamples, 0, true, true);
                    c.audio = buf;
                    c.audioSourceRate = r->sampleRate;
                    c.peaks = std::make_shared<std::vector<float>> (buildPeaks (*buf));
                }
                else
                    std::cout << "[load] missing referenced audio: " << f.getFullPathName() << std::endl;
            }
            else if (c.isAudio())
            {
                const int nch = juce::jmax (1, (int) cl.getProperty ("achannels", 1));
                const int fr  = juce::jmax (0, (int) cl.getProperty ("aframes", 0));
                auto buf = std::make_shared<juce::AudioBuffer<float>> (nch, fr);
                buf->clear();
                juce::MemoryOutputStream os;
                juce::Base64::convertFromBase64 (os, cl.getProperty ("adata", "").toString());
                const auto mb = os.getMemoryBlock();
                const auto* src = (const float*) mb.getData();
                const size_t count = mb.getSize() / sizeof (float);
                size_t idx = 0;
                for (int ch = 0; ch < nch; ++ch)
                    for (int j = 0; j < fr; ++j)
                        if (idx < count) buf->setSample (ch, j, src[idx++]);
                c.audio = buf;
                c.audioSourceRate = (double) cl.getProperty ("arate", 44100.0);
                c.audioGain = (float) (double) cl.getProperty ("again", 1.0);
                c.peaks = std::make_shared<std::vector<float>> (buildPeaks (*buf));
            }
            else
            {
                for (int n = 0; n < cl.getNumChildren(); ++n)
                {
                    auto nt = cl.getChild (n);
                    c.notes.push_back ({ (int) nt.getProperty ("pitch", 60),
                                         (double) nt.getProperty ("start", 0.0),
                                         (double) nt.getProperty ("nlen", 0.25),
                                         (float) (double) nt.getProperty ("vel", 0.85) });
                }
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
            mt->isBus = (bool) tv.getProperty ("bus", false);
            mt->buffer.setSize (2, juce::jmax (16, currentBlockSize));
            for (int f = 0; f < tv.getNumChildren(); ++f)
            {
                auto ft = tv.getChild (f);
                if (ft.hasType ("SEND"))   // aux send, not an effect
                {
                    mt->sends.push_back ({ (int) ft.getProperty ("to", 0),
                                           (float) (double) ft.getProperty ("level", 0.0) });
                    continue;
                }
                const juce::String ftype = ft.getProperty ("type", "Gain").toString();
                std::unique_ptr<Effect> fx;

                if (ftype == "Plugin")
                {
                    juce::PluginDescription d;
                    if (auto xml = juce::parseXML (ft.getProperty ("pdesc").toString())) d.loadFromXml (*xml);
                    fx = makePluginEffect (resolvePluginDescription (d));
                    if (fx != nullptr)
                    {
                        juce::MemoryBlock st; st.fromBase64Encoding (ft.getProperty ("pstate").toString());
                        if (st.getSize() > 0 && fx->getPluginInstance() != nullptr)
                            fx->getPluginInstance()->setStateInformation (st.getData(), (int) st.getSize());
                    }
                }
                else
                {
                    fx = makeEffect (ftype);
                    if (fx != nullptr)
                    {
                        auto params = fx->parameters();
                        for (int pv = 0; pv < ft.getNumChildren(); ++pv)
                        {
                            const juce::String nm = ft.getChild (pv).getProperty ("name", "").toString();
                            const float val = (float) (double) ft.getChild (pv).getProperty ("value", 0.0);
                            for (auto& pr : params) if (pr.name == nm) { pr.set (val); break; }
                        }
                    }
                }
                if (fx == nullptr) continue;
                fx->bypassed.store ((bool) ft.getProperty ("bypass", false));
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

    auto au = root.getChildWithName ("AUTOMATION");
    for (int i = 0; i < au.getNumChildren(); ++i)
    {
        auto l = au.getChild (i);
        AutoLaneSnap lane { (int) l.getProperty ("type"), (int) l.getProperty ("id"),
                            (int) l.getProperty ("slot"), l.getProperty ("param").toString(), {} };
        for (int j = 0; j < l.getNumChildren(); ++j)
        {
            auto pt = l.getChild (j);
            lane.points.push_back ({ (double) pt.getProperty ("beat"), (float) pt.getProperty ("value") });
        }
        automationLanes.push_back (std::move (lane));
    }

    auto locs = root.getChildWithName ("LOCATIONS");
    for (int i = 0; i < locs.getNumChildren(); ++i)
    {
        auto l = locs.getChild (i);
        locations.push_back ({ l.getProperty ("name").toString(), l.getProperty ("kind").toString(),
                               (double) l.getProperty ("start"), (double) l.getProperty ("end") });
    }

    auto exps = root.getChildWithName ("EXPORTS");
    for (int i = 0; i < exps.getNumChildren(); ++i)
    {
        auto e = exps.getChild (i);
        exportProfiles.push_back ({ e.getProperty ("name").toString(), e.getProperty ("target").toString(),
                                    e.getProperty ("range").toString(), e.getProperty ("format").toString(),
                                    (int) e.getProperty ("track"), (double) e.getProperty ("tail") });
    }

    auto scs = root.getChildWithName ("SCENES");
    for (int i = 0; i < scs.getNumChildren(); ++i)
    {
        auto sv = scs.getChild (i);
        MixerScene sc;
        sc.name = sv.getProperty ("name").toString();
        for (int j = 0; j < sv.getNumChildren(); ++j)
        {
            auto iv = sv.getChild (j);
            MixerScene::Insert in;
            in.volume = (float) (double) iv.getProperty ("vol", 0.8);
            in.pan    = (float) (double) iv.getProperty ("pan", 0.0);
            in.mute   = (bool) iv.getProperty ("mute", false);
            in.solo   = (bool) iv.getProperty ("solo", false);
            const auto bp = iv.getProperty ("bypass").toString();
            for (int k = 0; k < bp.length(); ++k) in.bypass.push_back (bp[k] == '1' ? 1 : 0);
            sc.inserts.push_back (std::move (in));
        }
        mixerScenes.push_back (std::move (sc));
    }

    auto mods = root.getChildWithName ("MODS");
    for (int i = 0; i < mods.getNumChildren(); ++i)
    {
        auto mv = mods.getChild (i);
        modulations.push_back ({ mv.getProperty ("target").toString(),
                                 (float) (double) mv.getProperty ("rate", 1.0),
                                 (float) (double) mv.getProperty ("depth", 0.0),
                                 (float) (double) mv.getProperty ("center", 0.0),
                                 (int) mv.getProperty ("shape", 0) });
    }

    auto tm = root.getChildWithName ("TEMPOMAP");
    for (int i = 0; i < tm.getNumChildren(); ++i)
    {
        auto v = tm.getChild (i);
        tempoMap.push_back ({ (double) v.getProperty ("beat", 0.0), (double) v.getProperty ("bpm", 120.0) });
    }

    auto ctl = root.getChildWithName ("CONTROLLERS");
    for (int i = 0; i < ctl.getNumChildren(); ++i)
    {
        auto v = ctl.getChild (i);
        controllerMaps.push_back ({ v.getProperty ("source").toString(), v.getProperty ("target").toString(),
                                    (float) (double) v.getProperty ("lo", 0.0), (float) (double) v.getProperty ("hi", 1.0) });
    }

    transport.setBpm ((double) root.getProperty ("bpm", 128.0));
    transport.setSwing ((double) root.getProperty ("swing", 0.5));

    projectNotes = root.getProperty ("notes", "").toString();
    if (notesWindow != nullptr) notesEditor.setText (projectNotes, juce::dontSendNotification);
    scaleRoot = (int) root.getProperty ("scaleRoot", 0);
    scaleName = root.getProperty ("scaleName", "chromatic").toString();
    if (root.hasProperty ("scaleIntervals"))
    {
        scaleIntervals.clear();
        for (auto& s : juce::StringArray::fromTokens (root.getProperty ("scaleIntervals").toString(), ",", ""))
            if (s.trim().isNotEmpty()) scaleIntervals.push_back (s.getIntValue());
        if (scaleIntervals.empty()) scaleIntervals = { 0,1,2,3,4,5,6,7,8,9,10,11 };
    }
}

void MainComponent::refreshUiAfterLoad()
{
    for (auto& t : tracks) t->liveMidi.reset (currentSampleRate);
    refreshTrackIds();

    selTrack = selClip = -1;
    editorPanel.roll.setEnabledEditing (false);
    editorPanel.roll.loadNotes ({});
    editorPanel.steps.setEnabledEditing (false);
    editorPanel.steps.loadNotes ({});
    editorPanel.title.setText ("EDITOR", juce::dontSendNotification);

    if (arrangeView) { arrangeView->setSelection (-1, -1); arrangeView->rebuild(); }
    if (mixerView) mixerView->rebuild();
    if (! undoSuppressed) emitChange ("project_loaded");

    transport.requestReset();
    bpmSlider.setValue (transport.getBpm(), juce::dontSendNotification);
    playButton.setToggleState (false, juce::dontSendNotification);
    playButton.setIcon (IconButton::Play);
    resized();
}
