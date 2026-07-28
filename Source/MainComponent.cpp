// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#include "MainComponent.h"
#include "NoteScheduler.h"
#include "FadeShape.h"
#include "Sampler.h"
#include "SfizzGenerator.h"
#include "SurgeGenerator.h"
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
        if (sessionCapture.load()) { finalizeSessionCapture(); recordButton.setToggleState (false, juce::dontSendNotification); }
        transport.setPlaying (false);
        transport.requestReset();
        playButton.setToggleState (false, juce::dontSendNotification);
        playButton.setIcon (IconButton::Play);
    };

    addAndMakeVisible (recordButton);
    recordButton.setTooltip ("Record MIDI into the selected instrument track");
    recordButton.onClick = [this]
    {
        if (viewMode == ViewMode::Session)   // in Session view, Record captures the jam to the arrangement
        {
            if (sessionCapture.load()) { finalizeSessionCapture(); playButton.setToggleState (transport.isPlaying(), juce::dontSendNotification); }
            else                       { startSessionCapture();   playButton.setToggleState (true, juce::dontSendNotification); playButton.setIcon (IconButton::Pause); }
            recordButton.setToggleState (sessionCapture.load(), juce::dontSendNotification);
            return;
        }
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
    // "+ Synth" -> a small menu: Surge XT (Gloopy's featured instrument — the hosted plugin's
    // real, editable editor via the Track/Mixer "Plugin UI" button) first as the default, plus
    // the built-in lightweight Basic synth. Other instrument types have their own toolbar buttons
    // (+ SFZ / + Sample / + Audio / + Plugin).
    auto addSurgePlugin = [this]
    {
        juce::String id;
        for (auto& p : apiListPlugins())
            if (p.isInstrument && p.name.containsIgnoreCase ("Surge XT"))
            { id = p.identifier; if (p.format == "LV2") break; }   // prefer LV2
        if (id.isEmpty())
        {
            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Surge XT",
                "The Surge XT plugin wasn't found. Install Surge XT (VST3/LV2), or build + bundle it "
                "with scripts/build-surge-plugin.sh, then Rescan Plugins.");
            return;
        }
        busyOverlay.show ("Adding Surge XT…");
        juce::MessageManager::callAsync ([this, id] { apiAddPluginTrack (id); busyOverlay.hide(); });
    };
    auto addBasicSynth = [this]
    {
        addTrack (std::make_unique<Track> ("Synth", std::make_unique<SynthGenerator>(), 48,
                      paletteColour ((int) tracks.size())));
    };
    addSynthBtn.onClick = [this, addSurgePlugin, addBasicSynth]
    {
        juce::PopupMenu m;
        m.addItem (1, "Surge XT  (full editor)");   // featured default — hosted plugin, real Surge UI
        m.addItem (2, "Basic synth");               // built-in lightweight synth
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addSynthBtn),
            [addSurgePlugin, addBasicSynth] (int r)
            {
                if      (r == 1) addSurgePlugin();
                else if (r == 2) addBasicSynth();
            });
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
                auto slot = std::make_shared<std::unique_ptr<Sampler>>();
                auto ok   = std::make_shared<bool> (false);
                const double sr = currentSampleRate; const int bs = currentBlockSize;
                runBackground ("Loading " + file.getFileNameWithoutExtension() + "…",
                    [this, slot, ok, file, sr, bs]
                    {
                        auto s = std::make_unique<Sampler>();
                        s->prepare (sr, bs);
                        *ok = s->loadFile (file, formatManager);
                        *slot = std::move (s);
                    },
                    [this, slot, ok, file]
                    {
                        if (*ok && *slot)
                            addTrack (std::make_unique<Track> (file.getFileNameWithoutExtension(),
                                          std::move (*slot), 60, paletteColour ((int) tracks.size())));
                    });
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
                // sfizz parsing + sample loading can take seconds — do it off the message
                // thread behind the busy overlay, then swap the ready generator in.
                auto slot = std::make_shared<std::unique_ptr<SfizzGenerator>>();
                auto ok   = std::make_shared<bool> (false);
                auto err  = std::make_shared<juce::String>();
                const double sr = currentSampleRate; const int bs = currentBlockSize;
                runBackground ("Loading " + file.getFileNameWithoutExtension() + "…",
                    [slot, ok, err, file, sr, bs]
                    {
                        auto g = std::make_unique<SfizzGenerator>();
                        g->prepare (sr, bs);
                        *ok = g->loadSfz (file, *err);
                        *slot = std::move (g);
                    },
                    [this, slot, ok, err]
                    {
                        if (*ok && *slot)
                            addTrack (std::make_unique<Track> ((*slot)->getName(),
                                          std::move (*slot), 60, paletteColour ((int) tracks.size())));
                        else
                            std::cout << "[sfz] " << *err << std::endl;
                    });
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
                if (file.existsAsFile())
                    apiImportAudio (file.getFullPathName());   // shared with the ImportAudio RPC
            });
    };

    addAndMakeVisible (addPluginBtn);
    addPluginBtn.onClick = [this] { showAddPluginMenu(); };

    addAndMakeVisible (loopButton);
    loopButton.setClickingTogglesState (true);
    loopButton.setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
    loopButton.onClick = [this] { transport.setLoopEnabled (loopButton.getToggleState()); };

    addAndMakeVisible (metroButton);
    metroButton.setClickingTogglesState (true);
    metroButton.setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
    metroButton.setTooltip ("Metronome: a click on each beat (accented on the downbeat)");
    metroButton.onClick = [this] { apiSetMetronome (metroButton.getToggleState()); };

    addAndMakeVisible (panicButton);
    panicButton.setTooltip ("Panic (Ctrl+.): send all-notes-off to every track (clears stuck/hanging notes)");
    panicButton.onClick = [this] { apiPanic(); };

    addAndMakeVisible (mixerButton);
    mixerButton.onClick = [this] { setViewMode (viewMode == ViewMode::Mixer ? ViewMode::Arrange : ViewMode::Mixer); };
    setWantsKeyboardFocus (true);   // so Tab (view switch) reliably reaches keyPressed
    // Grab focus once shown so the very first Tab works without clicking into the app first.
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<MainComponent> (this)] { if (sp) sp->grabKeyboardFocus(); });

    // Collapsible left browser: tabbed categories that seed / open projects on click.
    browser = std::make_unique<BrowserSidebar>();
    std::vector<BrowserSidebar::Category> cats {
        { "Templates",
          [this] { return apiListTemplates(); },
          [this] (const juce::String& name)
          {
              busyOverlay.show ("Loading " + name + "…");
              juce::MessageManager::callAsync ([this, name] { apiNewFromTemplate (name); busyOverlay.hide(); });
          } },
        { "Demos",
          [this] { return listDemos(); },
          [this] (const juce::String& name)
          {
              const auto f = demosDir().getChildFile (name);
              busyOverlay.show ("Opening " + name + "…");
              juce::MessageManager::callAsync ([this, f] { openAny (f); busyOverlay.hide(); });
          } },
        { "Plugins",
          [this]                                            // installed instrument plugins
          {
              browserPluginIds.clear();
              std::vector<juce::String> names;
              for (auto& p : apiListPlugins())
                  if (p.isInstrument)
                  {
                      const auto label = p.name + "  (" + p.format + ")";
                      browserPluginIds[label] = p.identifier;
                      names.push_back (label);
                  }
              return names;
          },
          [this] (const juce::String& label)
          {
              const auto it = browserPluginIds.find (label);
              if (it == browserPluginIds.end()) return;
              const auto id = it->second;
              busyOverlay.show ("Loading " + label + "…");
              juce::MessageManager::callAsync ([this, id] { apiAddPluginTrack (id); busyOverlay.hide(); });
          } },
        { "Samples",
          [this] { return listSamples(); },                 // audio files under samplesDir()
          [this] (const juce::String& name)
          {
              const auto f = samplesDir().getChildFile (name);
              busyOverlay.show ("Importing " + name + "…");
              juce::MessageManager::callAsync ([this, f] { apiImportAudio (f.getFullPathName()); busyOverlay.hide(); });
          } },
    };
   #ifdef GLOOPY_WITH_SURGE
    // Surge factory patches — click a preset to add a Surge track loaded with it.
    cats.push_back ({ "Presets",
        [this] { return listSurgePatches(); },
        [this] (const juce::String& label)
        {
            const auto it = browserSurgePatches.find (label);
            if (it != browserSurgePatches.end())
                addSurgeTrackAsync (it->second, label);   // runs off-thread behind the busy overlay
        } });
   #endif
    browser->setCategories (std::move (cats));
    addChildComponent (*browser);   // hidden until toggled
    browseButton.setClickingTogglesState (true);
    browseButton.setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
    browseButton.setTooltip ("Browser: templates, demos, plugins, samples & presets (toggle the left panel)");
    browseButton.onClick = [this]
    {
        browserVisible = browseButton.getToggleState();
        if (browserVisible) browser->refresh();
        resized();
    };
    addAndMakeVisible (browseButton);

    addAndMakeVisible (mapsButton);
    mapsButton.setTooltip ("Mappings: see and remove all MIDI/OSC controller maps and LFO routes");
    mapsButton.onClick = [this] { openMappings(); };
    mappingsView.onRemove = [this] (const juce::String& kind, const juce::String& key)
    {
        if (kind == "ctrl") apiRemoveControllerMap (key);
        else                apiRemoveModulation (key);
        openMappings();   // re-query + rebuild the list
    };

    // ---- project scale selector (drives snap-to-scale + piano-roll highlight) ----
    {
        const char* roots[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        for (int i = 0; i < 12; ++i) scaleRootBox.addItem (roots[i], i + 1);
        const char* names[] = { "chromatic","major","minor","harmonic-minor","melodic-minor",
                                "dorian","phrygian","lydian","mixolydian","locrian",
                                "pentatonic-major","pentatonic-minor","blues","whole-tone" };
        for (int i = 0; i < (int) (sizeof (names) / sizeof (names[0])); ++i)
            scaleNameBox.addItem (names[i], i + 1);
        scaleRootBox.setTooltip ("Project scale root");
        scaleNameBox.setTooltip ("Project scale — highlights in-scale rows and drives Snap to scale");
        scaleRootBox.onChange = [this] { applyScaleFromToolbar(); };
        scaleNameBox.onChange = [this] { applyScaleFromToolbar(); };
        addAndMakeVisible (scaleRootBox);
        addAndMakeVisible (scaleNameBox);
        refreshScaleToolbar();
    }

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
    arrangeView->getTempoMarkers = [this]
    {
        std::vector<std::pair<double, double>> out;
        for (auto& m : apiListTempoMarkers()) out.push_back ({ m.beat, m.bpm });
        return out;
    };
    arrangeView->getMarkers = [this]
    {
        std::vector<std::pair<juce::String, double>> out;
        for (auto& l : apiListLocations()) out.push_back ({ l.name, l.startBeat });
        return out;
    };
    arrangeView->getMidiActivity = [this] (int trackId) -> float
    {
        if (trackId < 0) return 0.0f;
        // Faint base on the current live-MIDI target — the track that will sound when you play.
        int target = midiInputTarget.load();
        if (target < 0) target = firstInstrumentId.load();
        float level = (trackId == target) ? 0.22f : 0.0f;
        // Brighten to a pulse on the track that just received a note, then fade out.
        if (trackId == midiActivityTrackId.load())
        {
            const double dt = juce::Time::getMillisecondCounterHiRes() - midiActivityMs.load();
            constexpr double fadeMs = 250.0;
            if (dt >= 0.0 && dt < fadeMs) level = juce::jmax (level, (float) (1.0 - dt / fadeMs));
        }
        return level;
    };
    arrangeView->onAddMarker = [this] (const juce::String& name, double beat)
    {
        apiAddLocation (name, "marker", beat, beat);
        if (arrangeView) arrangeView->repaint();
    };
    arrangeView->onRemoveMarker = [this] (const juce::String& name)
    {
        apiRemoveLocation (name);
        if (arrangeView) arrangeView->repaint();
    };
    arrangeView->onAddTempoMarker = [this] (double beat, double bpm)
    {
        apiAddTempoMarker (beat, bpm);
        if (arrangeView) arrangeView->repaint();
    };
    arrangeView->onRemoveTempoMarker = [this] (double beat)
    {
        apiRemoveTempoMarker (beat);
        if (arrangeView) arrangeView->repaint();
    };
    arrangeView->onSetTimeSignature = [this] (int num, int denom) { apiSetTimeSignature (num, denom); };
    arrangeView->getSwing   = [this] { return transport.getSwing(); };
    arrangeView->onSetSwing = [this] (double s) { apiSetSwing (s); };
    arrangeView->getMetronomeLevel   = [this] { return apiGetMetronomeLevel(); };
    arrangeView->onSetMetronomeLevel = [this] (float l) { apiSetMetronomeLevel (l); };
    arrangeView->getSamplerControls = [this] (int trackIdx) -> ArrangeView::SamplerCtl
    {
        if (! juce::isPositiveAndBelow (trackIdx, (int) tracks.size())) return {};
        const auto s = apiGetSamplerControls (tracks[(size_t) trackIdx]->id);
        return { s.ok, s.start, s.end, s.reverse, s.rootNote, s.fadeIn, s.fadeOut, s.loop, s.mono };
    };
    arrangeView->onSetSamplerControls = [this] (int trackIdx, float start, float end, bool reverse, int root, float fadeIn, float fadeOut, bool loop, bool mono)
    {
        if (! juce::isPositiveAndBelow (trackIdx, (int) tracks.size())) return;
        apiSetSamplerControls (tracks[(size_t) trackIdx]->id, start, end, reverse, root, fadeIn, fadeOut, loop, mono);
    };
    arrangeView->getPunchRange = [this] (double& in, double& out) -> bool
    {
        in  = punchInBeat.load();
        out = punchOutBeat.load();
        return punchEnabled.load();
    };
    arrangeView->onSetPunchRange = [this] (bool en, double in, double out)
    {
        apiSetPunchRange (en, in, out, countInBeats.load());
        if (arrangeView) arrangeView->repaint();
    };
    arrangeView->onArpMenu = [this] (int trackIdx)
    {
        if (! juce::isPositiveAndBelow (trackIdx, (int) tracks.size())) return;
        const int id = tracks[(size_t) trackIdx]->id;
        bool en=false, hold=false; double rate=0.25; int oct=1, mode=0; float gate=0.5f, swing=0.0f, prob=1.0f;
        apiGetTrackArp (id, en, rate, oct, gate, mode, swing, hold, prob);

        juce::PopupMenu m;
        m.addItem (1, "Arpeggiator", true, en);                       // toggle on/off
        m.addItem (2, "Hold", true, hold);                            // latch across rests / fill clip
        m.addSeparator();
        juce::PopupMenu rateM;
        const std::pair<const char*, double> rates[] = { {"1/4", 1.0}, {"1/8", 0.5}, {"1/16", 0.25}, {"1/32", 0.125} };
        for (int i = 0; i < 4; ++i) rateM.addItem (10 + i, rates[i].first, true, std::abs (rate - rates[i].second) < 1e-6);
        m.addSubMenu ("Rate", rateM);
        juce::PopupMenu octM;
        for (int o = 1; o <= 4; ++o) octM.addItem (20 + o, juce::String (o) + (o == 1 ? " octave" : " octaves"), true, oct == o);
        m.addSubMenu ("Octaves", octM);
        juce::PopupMenu modeM;
        const char* modes[] = { "Up", "Down", "Up-Down", "Random" };
        for (int i = 0; i < 4; ++i) modeM.addItem (30 + i, modes[i], true, mode == i);
        m.addSubMenu ("Mode", modeM);
        juce::PopupMenu swingM;
        const std::pair<const char*, float> swings[] = { {"Off", 0.0f}, {"Subtle", 0.2f}, {"Medium", 0.4f}, {"Strong", 0.6f} };
        for (int i = 0; i < 4; ++i) swingM.addItem (40 + i, swings[i].first, true, std::abs (swing - swings[i].second) < 0.01f);
        m.addSubMenu ("Swing", swingM);
        juce::PopupMenu chanceM;                                      // generative gate: chance each step fires
        const std::pair<const char*, float> chances[] = { {"100%", 1.0f}, {"75%", 0.75f}, {"50%", 0.5f}, {"25%", 0.25f} };
        for (int i = 0; i < 4; ++i) chanceM.addItem (50 + i, chances[i].first, true, std::abs (prob - chances[i].second) < 0.01f);
        m.addSubMenu ("Chance", chanceM);
        juce::PopupMenu gateM;                                        // note length as a fraction of the step
        const std::pair<const char*, float> gates[] = { {"25% (staccato)", 0.25f}, {"50%", 0.5f}, {"75%", 0.75f}, {"100% (legato)", 1.0f} };
        for (int i = 0; i < 4; ++i) gateM.addItem (60 + i, gates[i].first, true, std::abs (gate - gates[i].second) < 0.01f);
        m.addSubMenu ("Gate", gateM);

        m.showMenuAsync (juce::PopupMenu::Options(), [this, id, en, rate, oct, gate, mode, swing, hold, prob] (int r)
        {
            if (r == 0) return;
            bool nen = en, nhold = hold; double nrate = rate; int noct = oct, nmode = mode; float nswing = swing, nprob = prob, ngate = gate;
            if      (r == 1)                nen = ! en;
            else if (r == 2)                { nhold = ! hold; nen = true; }
            else if (r >= 10 && r <= 13)    { nrate = (const double[]){1.0,0.5,0.25,0.125}[r-10]; nen = true; }
            else if (r >= 21 && r <= 24)    { noct = r - 20; nen = true; }
            else if (r >= 30 && r <= 33)    { nmode = r - 30; nen = true; }
            else if (r >= 40 && r <= 43)    { nswing = (const float[]){0.0f,0.2f,0.4f,0.6f}[r-40]; nen = true; }
            else if (r >= 50 && r <= 53)    { nprob = (const float[]){1.0f,0.75f,0.5f,0.25f}[r-50]; nen = true; }
            else if (r >= 60 && r <= 63)    { ngate = (const float[]){0.25f,0.5f,0.75f,1.0f}[r-60]; nen = true; }
            apiSetTrackArp (id, nen, nrate, noct, ngate, nmode, nswing, nhold, nprob);
            if (arrangeView) arrangeView->rebuild();                   // refresh the ARP button lit-state
        });
    };
    arrangeView->onClipCommand = [this] (int trackIdx, int clip, const juce::String& cmd)
    {
        if (! juce::isPositiveAndBelow (trackIdx, (int) tracks.size())) return;
        const int id = tracks[(size_t) trackIdx]->id;          // map view index -> stable API id
        if      (cmd == "split")     apiSplitClip (id, clip, transport.getPlayheadBeats());
        else if (cmd.startsWith ("splitmarker:")) apiSplitClipAtMarker (id, clip, cmd.substring (12));
        else if (cmd == "duplicate") apiDuplicateClip (id, clip, -1.0);
        else if (cmd == "reverse")   apiReverseClip (id, clip);
        else if (cmd == "snapscale") apiSnapClipToScale (id, clip);
        else if (cmd == "normalize") apiNormalizeClip (id, clip, -1.0f);   // audio clip -> -1 dBFS
        else if (cmd == "croploop")  apiCropClip (id, clip, transport.getLoopStartBeats(), transport.getLoopEndBeats());
        else if (cmd == "consolidate") apiConsolidateClip (id, clip);
        else if (cmd == "bounce")    apiBounceClip (id, clip);
        else if (cmd == "slicetransients") apiSliceClipAtTransients (id, clip, 1.0f);
        else if (cmd == "mute")      apiSetClipMuted (id, clip, true);
        else if (cmd == "unmute")    apiSetClipMuted (id, clip, false);
        else if (cmd.startsWith ("repeat:")) apiRepeatClip (id, clip, cmd.substring (7).getIntValue());
        else if (cmd == "loopclip")  apiSetLoopToClip (id, clip);
        else if (cmd == "copynotes") juce::SystemClipboard::copyTextToClipboard (apiExportClipNotesJson (id, clip));
        else if (cmd.startsWith ("transpose:")) apiSetClipTranspose (id, clip, cmd.substring (10).getIntValue());
        else if (cmd.startsWith ("velscale:")) apiSetClipVelocity (id, clip, cmd.substring (9).getIntValue() / 100.0f);
        else if (cmd.startsWith ("prob:")) apiSetClipProbability (id, clip, cmd.substring (5).getIntValue() / 100.0f);
        else if (cmd.startsWith ("fadeshape:")) apiSetClipFadeShape (id, clip, cmd.substring (10).getIntValue());
        else if (cmd == "velramp:up")   apiRampClipVelocity (id, clip, 0.3f, 1.0f);   // crescendo
        else if (cmd == "velramp:down") apiRampClipVelocity (id, clip, 1.0f, 0.3f);   // decrescendo
        else if (cmd.startsWith ("timescale:")) apiScaleClipTime (id, clip, cmd.substring (10).getDoubleValue());
        else if (cmd.startsWith ("echo:"))   // "echo:<delayBeats>,<repeats>"
        {
            auto a = cmd.substring (5);
            apiEchoClip (id, clip, a.upToFirstOccurrenceOf (",", false, false).getDoubleValue(),
                         a.fromLastOccurrenceOf (",", false, false).getIntValue(), 0.6f);
        }
        else if (cmd == "invert")       apiInvertClip (id, clip);   // melodic inversion
        else if (cmd.startsWith ("ratchet:")) apiRatchetClip (id, clip, cmd.substring (8).getIntValue());
        else if (cmd.startsWith ("harmonize:")) apiHarmonizeClip (id, clip, cmd.substring (10).getIntValue());
        else if (cmd.startsWith ("swing:"))
        {
            auto a = cmd.substring (6);                          // "grid,amount"
            apiSwingClip (id, clip, a.upToFirstOccurrenceOf (",", false, false).getDoubleValue(),
                          (float) a.fromFirstOccurrenceOf (",", false, false).getDoubleValue());
        }
        else if (cmd.startsWith ("chordify:")) apiChordifyClip (id, clip, cmd.substring (9).getIntValue());
        else if (cmd.startsWith ("gate:")) apiGateClip (id, clip, cmd.substring (5).getDoubleValue());
        else if (cmd.startsWith ("clipcolour:")) apiSetClipColour (id, clip, cmd.substring (11));
        else if (cmd.startsWith ("flattenvel:")) apiFlattenClipVelocity (id, clip, (float) cmd.substring (11).getDoubleValue());
        else if (cmd.startsWith ("spliteq:")) apiSplitClipEqual (id, clip, cmd.substring (8).getIntValue());
        else if (cmd.startsWith ("quantize:"))
        {
            auto a = cmd.substring (9);                          // "grid,strength"
            apiQuantizeClip (id, clip, a.upToFirstOccurrenceOf (",", false, false).getDoubleValue(),
                             a.fromFirstOccurrenceOf (",", false, false).getDoubleValue());
        }
        else if (cmd == "delete")    apiRemoveClip (id, clip);
        else if (cmd == "cleanuptakes") apiCleanupTakes();
        else if (cmd == "promotetake")
        {
            juce::String takeId;
            { const juce::ScopedLock sl (engineLock);
              auto& cl = tracks[(size_t) trackIdx]->clips;
              if (juce::isPositiveAndBelow (clip, (int) cl.size())) takeId = cl[(size_t) clip].takeId; }
            if (takeId.isNotEmpty()) apiPromoteTake (takeId);
        }
        else if (cmd == "usetake")
        {
            // Comp selection: activate this take, mute its siblings (same anchor).
            pushUndoSnapshot();
            { const juce::ScopedLock sl (engineLock);
              auto& cl = tracks[(size_t) trackIdx]->clips;
              if (juce::isPositiveAndBelow (clip, (int) cl.size()))
              {
                  const double anchor = cl[(size_t) clip].startBeat;
                  for (auto& c : cl)
                      if (c.takeId.isNotEmpty() && std::abs (c.startBeat - anchor) < 1e-6)
                          c.muted = (&c != &cl[(size_t) clip]);
              } }
            emitChange ("clip_changed", id);
        }
        if (arrangeView) arrangeView->repaint();
    };

    arrangeView->onRenameClip = [this] (int trackIdx, int clip, const juce::String& name)
    {
        if (! juce::isPositiveAndBelow (trackIdx, (int) tracks.size())) return;
        apiRenameClip (tracks[(size_t) trackIdx]->id, clip, name);   // map view index -> stable API id
    };

    arrangeView->onPasteNotes = [this] (int trackIdx, double beat)
    {
        if (! juce::isPositiveAndBelow (trackIdx, (int) tracks.size())) return;
        const int id = tracks[(size_t) trackIdx]->id;
        if (apiImportClipNotesJson (id, beat, juce::SystemClipboard::getTextFromClipboard()) >= 0)
            if (arrangeView) arrangeView->repaint();
    };

    arrangeView->onRenameTrack = [this] (int trackIdx, const juce::String& name)
    {
        if (! juce::isPositiveAndBelow (trackIdx, (int) tracks.size())) return;
        apiRenameTrack (tracks[(size_t) trackIdx]->id, name);    // map view index -> stable API id
    };
    arrangeView->onRemoveTrack = [this] (int trackIdx)
    {
        if (! juce::isPositiveAndBelow (trackIdx, (int) tracks.size())) return;
        apiRemoveTrack (tracks[(size_t) trackIdx]->id);         // deletes the track + its mixer insert
    };

    arrangeView->onSetTrackColour = [this] (int trackIdx, const juce::String& hex)
    {
        if (! juce::isPositiveAndBelow (trackIdx, (int) tracks.size())) return;
        apiSetTrackColour (tracks[(size_t) trackIdx]->id, hex);  // map view index -> stable API id
    };

    arrangeView->onMoveTrack = [this] (int trackIdx, int delta)
    {
        if (! juce::isPositiveAndBelow (trackIdx, (int) tracks.size())) return;
        apiMoveTrack (tracks[(size_t) trackIdx]->id, delta);     // map view index -> stable API id
    };

    arrangeView->onSetTrackPolarity = [this] (int trackIdx, bool invert)
    {
        if (! juce::isPositiveAndBelow (trackIdx, (int) tracks.size())) return;
        apiSetTrackPolarity (tracks[(size_t) trackIdx]->id, invert);   // map view index -> stable API id
    };

    arrangeView->onDuplicateTrack = [this] (int trackIdx)
    {
        if (! juce::isPositiveAndBelow (trackIdx, (int) tracks.size())) return;
        apiDuplicateTrack (tracks[(size_t) trackIdx]->id);   // map view index -> stable API id
    };

    arrangeView->onExportTrack = [this] (int trackIdx)
    {
        if (! juce::isPositiveAndBelow (trackIdx, (int) tracks.size())) return;
        const int id = tracks[(size_t) trackIdx]->id;
        const auto suggested = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                   .getChildFile (juce::File::createLegalFileName (tracks[(size_t) trackIdx]->name) + ".wav");
        fileChooser = std::make_unique<juce::FileChooser> ("Export track", suggested, "*.wav;*.flac");
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, id] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                if (f.getFileExtension().isEmpty()) f = f.withFileExtension ("wav");
                const auto path = f.getFullPathName();
                auto ok = std::make_shared<bool> (false);
                runBackground ("Exporting track…",
                    [this, id, path, ok] { *ok = apiExportTrack (id, path); },
                    [this, f, ok]
                    {
                        if (! *ok)
                            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                "Export Track", "Could not write\n" + f.getFileName());
                    });
            });
    };

    arrangeView->onClipGain = [this] (int trackIdx, int clip, float db)
    {
        if (juce::isPositiveAndBelow (trackIdx, (int) tracks.size()))
            apiSetClipGain (tracks[(size_t) trackIdx]->id, clip, db);
    };

    arrangeView->onClipFades = [this] (int trackIdx, int clip, double fin, double fout)
    {
        if (juce::isPositiveAndBelow (trackIdx, (int) tracks.size()))
            apiSetClipFades (tracks[(size_t) trackIdx]->id, clip, fin, fout);
    };
    arrangeViewport.setViewedComponent (arrangeView.get(), false);
    arrangeViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (arrangeViewport);

    // ---- session view (clip-launch grid); Tab cycles Arrange -> Session -> Mixer ----
    sessionPane = std::make_unique<SessionPane> (tracks, mixerTracks, scenes, sessionLauncher, transport, engineLock);
    auto& grid  = sessionPane->grid();          // scrolling track grid (cells + mixer strips)
    auto& sceneCol = sessionPane->sceneColumn(); // frozen scene-launch + master column (pinned left)
    grid.onLaunchClip  = [this] (int ti, int s) { if (auto* t = trackByIndex (ti)) apiLaunchClip (t->id, s); };
    grid.onEmptyCell   = [this] (int ti, int s)
    {
        auto* t = trackByIndex (ti);
        if (t == nullptr) return;
        if (sessionRecTrack == ti && sessionRecScene == s) finalizeRecording();   // click the recording cell -> finish
        else if (t->recordArmed.load())                    startSessionRecord (ti, s);
        else                                               apiStopTrackClip (t->id);
    };
    grid.onArm            = [this] (int ti, bool a) { if (auto* t = trackByIndex (ti)) t->recordArmed.store (a); };
    grid.isArmed          = [this] (int ti) { auto* t = trackByIndex (ti); return t != nullptr && t->recordArmed.load(); };
    grid.getRecordingScene= [this] (int ti) { return sessionRecTrack == ti ? sessionRecScene : -1; };
    sceneCol.onLaunchScene = [this] (int s)         { apiLaunchScene (s); };
    sceneCol.onStopAll     = [this]                 { apiStopAllClips(); };
    sceneCol.onAddScene    = [this]                 { apiAddScene(); if (sessionPane) sessionPane->rebuild(); resized(); };
    sceneCol.onRemoveScene = [this] (int s)         { apiRemoveScene (s); if (sessionPane) sessionPane->rebuild(); resized(); };
    grid.onClearSlot   = [this] (int ti, int s) { if (auto* t = trackByIndex (ti)) { apiClearSessionSlot (t->id, s); if (sessionPane) sessionPane->rebuild(); } };
    grid.onNewClip     = [this] (int ti, int s)
    {
        if (auto* t = trackByIndex (ti))
        {
            Clip c; c.type = ClipType::Midi; c.name = "Clip"; c.lengthBeats = 4.0; c.contentLenBeats = 4.0; c.looped = true;
            apiSetSessionClip (t->id, s, c);
            if (sessionPane) sessionPane->rebuild();
            resized();
            selectSessionClip (ti, s);         // open the new clip in the editor to add notes
            setEditorMode (0);                 // piano roll
        }
    };
    grid.onEditClip    = [this] (int ti, int s) { selectSessionClip (ti, s); };
    grid.onCopyToArrangement = [this] (int ti, int s)
    {
        auto* t = trackByIndex (ti);
        if (t == nullptr) return;
        std::shared_ptr<Clip> src;
        { const juce::ScopedLock sl (engineLock); src = slotClip (t->sessionSlots, s); }
        if (src == nullptr) return;
        pushUndoSnapshot();
        Clip c = *src;                                   // copy notes / length / loop
        c.startBeat = juce::jmax (0.0, samplesToBeats (transport.getPlayheadSamples()).inBeats());   // at the playhead
        { const juce::ScopedLock sl (engineLock); t->clips.push_back (std::move (c)); }
        if (arrangeView) arrangeView->rebuild();
        setViewMode (ViewMode::Arrange);                 // reveal the result on the timeline
    };
    grid.onCopySelectedClip = [this] (int ti, int s)
    {
        auto* t = trackByIndex (ti);
        if (t == nullptr) return;
        Clip copy;
        {
            const juce::ScopedLock sl (engineLock);
            if (selTrack < 0 || selTrack >= (int) tracks.size()) return;
            auto& sc = tracks[(size_t) selTrack]->clips;
            if (selClip < 0 || selClip >= (int) sc.size()) return;
            copy = sc[(size_t) selClip];              // copy the selected arrangement clip
        }
        apiSetSessionClip (t->id, s, copy);
        if (sessionPane) sessionPane->rebuild();
        resized();
    };
    grid.getTrackLevels = [this] (int ti, float& l, float& r)
    {
        l = r = 0.0f;
        if (ti < 0 || ti >= (int) tracks.size() || mixerTracks.empty()) return;
        const int route = juce::jlimit (0, (int) mixerTracks.size() - 1, tracks[(size_t) ti]->mixerTrack.load());
        l = mixerTracks[(size_t) route]->peakL.load();
        r = mixerTracks[(size_t) route]->peakR.load();
    };
    grid.onOpenTrackFx = [this] (int) { setViewMode (ViewMode::Mixer); };   // add effects in the mixer view
    grid.getGroups = [this]
    {
        // A group = a bus that tracks route into, directly OR through nested sub-buses. Members are
        // the TRANSITIVE tracks (in track order); `output` is the parent bus (0 = master) so the
        // session view can nest group columns.
        std::vector<SessionView::GroupInfo> out;
        const juce::ScopedLock sl (engineLock);
        const int N = (int) mixerTracks.size();
        auto ancestry = [&] (int insert, std::vector<int>& anc)   // buses innermost -> outermost
        {
            int cur = insert;
            for (int guard = 0; guard < N && juce::isPositiveAndBelow (cur, N); ++guard)
            {
                const int o = mixerTracks[(size_t) cur]->output.load();
                if (o <= 0 || o >= N || o == cur) break;
                anc.push_back (o); cur = o;
            }
        };
        std::map<int, std::vector<int>> members;   // bus -> transitive track members, in track order
        for (int t = 0; t < (int) tracks.size(); ++t)
        {
            std::vector<int> anc; ancestry (tracks[(size_t) t]->mixerTrack.load(), anc);
            for (int b : anc) members[b].push_back (t);
        }
        std::set<int> sendFed;                     // buses that receive at least one aux send (returns)
        for (auto& mt : mixerTracks)
            for (auto& sd : mt->sends) sendFed.insert (sd.bus);
        for (int b = 1; b < N; ++b)
        {
            if (! mixerTracks[(size_t) b]->isBus) continue;
            auto it = members.find (b);
            const bool hasMembers = (it != members.end() && ! it->second.empty());
            const bool hasSends   = sendFed.count (b) > 0;
            if (! hasMembers && ! hasSends) continue;   // unrouted bus -> shows in the mixer only, not the session
            SessionView::GroupInfo g;
            g.busIndex = b; g.output = mixerTracks[(size_t) b]->output.load();
            g.name = mixerTracks[(size_t) b]->name; g.folded = mixerTracks[(size_t) b]->folded.load();
            g.isReturn = ! hasMembers;              // send-fed with no main-output members = a return
            if (hasMembers) g.members = it->second;
            g.colour = mixerTracks[(size_t) b]->colour.getARGB() != 0 ? mixerTracks[(size_t) b]->colour
                     : hasMembers ? tracks[(size_t) g.members.front()]->colour
                                  : juce::Colour (0xffe6a23c);   // returns default to the warm accent
            out.push_back (std::move (g));
        }
        return out;
    };
    grid.getBusLevels = [this] (int bus, float& l, float& r)
    {
        l = r = 0.0f;
        const juce::ScopedLock sl (engineLock);
        if (juce::isPositiveAndBelow (bus, (int) mixerTracks.size()))
        { l = mixerTracks[(size_t) bus]->peakL.load(); r = mixerTracks[(size_t) bus]->peakR.load(); }
    };
    grid.onSetGroupFolded = [this] (int bus, bool folded)
    {
        { const juce::ScopedLock sl (engineLock);
          if (juce::isPositiveAndBelow (bus, (int) mixerTracks.size())) mixerTracks[(size_t) bus]->folded.store (folded); }
        if (sessionPane) sessionPane->rebuild();
    };
    grid.onOpenBusFx = [this] (int) { setViewMode (ViewMode::Mixer); };   // edit the group's effects in the mixer
    grid.onOpenDeviceWindow = [this] (int bus) { openDeviceWindow (bus); };   // floating device chain for the group
    grid.onSetGroupColour = [this] (int bus, juce::Colour col)
    {
        { const juce::ScopedLock sl (engineLock);
          if (juce::isPositiveAndBelow (bus, (int) mixerTracks.size())) mixerTracks[(size_t) bus]->colour = col; }
        if (sessionPane) sessionPane->rebuild();
        if (mixerView)   mixerView->rebuild();
    };
    grid.onUngroup = [this] (int bus)
    {
        closeAllPluginWindows();
        apiUngroup (bus);
        if (mixerView)   mixerView->rebuild();
        if (sessionPane) sessionPane->rebuild();
    };
    grid.onGroupTracks = [this] (const std::vector<int>& trackIdx)   // session Cmd+G: map tracks -> their inserts, group
    {
        closeAllPluginWindows();
        std::vector<int> inserts;
        {
            const juce::ScopedLock sl (engineLock);
            for (int t : trackIdx)
                if (juce::isPositiveAndBelow (t, (int) tracks.size()))
                {
                    const int ins = tracks[(size_t) t]->mixerTrack.load();
                    if (ins > 0 && juce::isPositiveAndBelow (ins, (int) mixerTracks.size())
                        && std::find (inserts.begin(), inserts.end(), ins) == inserts.end())
                        inserts.push_back (ins);
                }
        }
        if (inserts.size() < 2) return;
        const int bus = apiGroupInserts (inserts, "Group");
        if (bus > 0) apiGatherGroup (bus);
        if (mixerView)   mixerView->rebuild();
        if (sessionPane) sessionPane->rebuild();
    };
    sceneCol.getQuantumBeats   = [this] { return apiGetLaunchQuantumBeats(); };
    sceneCol.onSetQuantumBeats = [this] (double b) { apiSetLaunchQuantumBeats (b); };
    sceneCol.getMasterVolume   = [this] { return mixerTracks.empty() ? 0.8f : mixerTracks[0]->volume.load(); };
    sceneCol.onSetMasterVolume = [this] (float v) { if (! mixerTracks.empty()) mixerTracks[0]->volume.store (v); };
    sceneCol.getMasterLevels   = [this] (float& l, float& r)
    { l = r = 0.0f; if (! mixerTracks.empty()) { l = mixerTracks[0]->peakL.load(); r = mixerTracks[0]->peakR.load(); } };
    addChildComponent (*sessionPane);                // hidden until Tab switches to Session

    // ---- clip editor ----
    addChildComponent (busyOverlay);   // shown only while a background task runs
    addAndMakeVisible (editorPanel);
    editorPanel.roll.setShowPlayhead (false);
    editorPanel.roll.setEnabledEditing (false);
    editorPanel.steps.setEnabledEditing (false);
    editorPanel.title.setText ("EDITOR", juce::dontSendNotification);
    editorPanel.roll.onNotesChanged  = [this] { writeBackEditor(); };
    editorPanel.roll.onAuditionOn    = [this] (int pitch, float vel) { apiAuditionNote (pitch, vel, true); };
    editorPanel.roll.onAuditionOff   = [this] (int pitch)            { apiAuditionNote (pitch, 0.0f, false); };
    editorPanel.steps.onNotesChanged = [this] { writeBackEditor(); };
    editorPanel.pianoBtn.onClick = [this] { setEditorMode (0); };
    editorPanel.stepBtn.onClick  = [this] { setEditorMode (1); };
    editorPanel.auditionBtn.onClick = [this]
    { editorPanel.roll.setAuditionEnabled (editorPanel.auditionBtn.getToggleState()); };
    editorPanel.devicesBtn.onClick = [this]
    { bottomMode = BottomMode::Devices; refreshDevicePanel(); resized(); };
    setEditorMode (editorMode);

    // Device panel — the selected track's effect chain, shown in the bottom area in place of the
    // clip editor (Ableton "Device View"). Hidden until the DEVICES button is pressed.
    addChildComponent (devicePanel);
    devicePanel.onShowClip = [this] { bottomMode = BottomMode::Clip; resized(); };
    devicePanel.getEffectTypes = []
    {
        return juce::StringArray { "Gain", "Filter", "Delay", "Reverb", "Limiter", "Bitcrusher",
                                   "Compressor", "EQ", "Waveshaper", "Stereo Widener", "Tremolo",
                                   "Chorus", "Flanger", "Phaser", "Auto-pan", "Noise Gate",
                                   "Auto-wah", "Ring Mod" };
    };
    devicePanel.getTitle = [this]
    {
        const juce::ScopedLock sl (engineLock);
        if (! juce::isPositiveAndBelow (deviceTrack, (int) mixerTracks.size())) return juce::String ("DEVICES");
        const juce::String bullet (juce::CharPointer_UTF8 ("\xe2\x80\xa2"));
        return "DEVICES   " + bullet + "   " + mixerTracks[(size_t) deviceTrack]->name.toUpperCase();
    };
    devicePanel.getChain = [this]
    {
        std::vector<std::pair<juce::String, bool>> out;
        const juce::ScopedLock sl (engineLock);
        if (juce::isPositiveAndBelow (deviceTrack, (int) mixerTracks.size()))
            for (auto& fx : mixerTracks[(size_t) deviceTrack]->effects)
                out.push_back ({ fx->name(), fx->bypassed.load() });
        return out;
    };
    devicePanel.getParams = [this] (int slot)
    {
        std::vector<DevicePanel::Param> out;
        for (auto& p : apiGetEffectParams (deviceTrack, slot))
            out.push_back ({ p.name, p.value, p.min, p.max });
        return out;
    };
    devicePanel.onAddEffect    = [this] (int type)                        { apiAddEffect (deviceTrack, type); };
    devicePanel.onRemoveEffect = [this] (int slot)                        { apiRemoveEffect (deviceTrack, slot); };
    devicePanel.onSetBypass    = [this] (int slot, bool b)                { apiSetEffectBypass (deviceTrack, slot, b); };
    devicePanel.onSetParam     = [this] (int slot, const juce::String& n, float v) { apiSetEffectParam (deviceTrack, slot, n, v); };

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
    mixerView->onMidiLearn          = [this] (const juce::String& target) { apiMidiLearn (target); };
    mixerView->onSetModulation      = [this] (const juce::String& target, float rate, float depth, int shape, float sync, float phase, bool uni, float slew)
    {
        ParamDesc d;
        const float center = apiGetParameter (target, d) ? d.value : 0.0f;   // LFO centres on the current value
        apiSetModulation (target, rate, depth, shape, center, sync, phase, uni, slew);
    };
    mixerView->onAddModulation      = [this] (const juce::String& target, float rate, float depth, int shape, float sync, float phase, bool uni, float slew)
    {
        ParamDesc d;
        const float center = apiGetParameter (target, d) ? d.value : 0.0f;   // each stacked LFO centres on the current value
        apiAddModulation (target, rate, depth, shape, center, sync, phase, uni, slew);
    };
    mixerView->onRemoveModulation   = [this] (const juce::String& target) { apiRemoveModulation (target); };
    // Automation authored on the same ParamModel id: drop a keyframe (current value @ playhead) / clear.
    mixerView->onAutomatePoint      = [this] (const juce::String& target)
    {
        ParamDesc d;
        const float value = apiGetParameter (target, d) ? d.value : 0.0f;
        apiAddAutomationPointById (target, transport.getPlayheadBeats(), value);
    };
    mixerView->onClearAutomation    = [this] (const juce::String& target) { apiSetAutomationById (target, {}); };
    mixerView->getAutomationStep    = [this] (const juce::String& target) { return apiGetAutomationStep (target); };
    mixerView->onSetAutomationStep  = [this] (const juce::String& target, bool step) { apiSetAutomationStep (target, step); };
    mixerView->getAutomationCurve   = [this] (const juce::String& target) { return apiGetAutomationCurve (target); };
    mixerView->onSetAutomationCurve = [this] (const juce::String& target, float curve) { apiSetAutomationCurve (target, curve); };
    // Controller mapping: surface the CC/OSC source bound to a target and clear it from the desktop.
    mixerView->onControllerSourceFor = [this] (const juce::String& target) -> juce::String
    {
        for (auto& m : apiListControllerMaps()) if (m.target == target) return m.source;
        return {};
    };
    mixerView->onRemoveControllerMap = [this] (const juce::String& target)
    {
        for (auto& m : apiListControllerMaps()) if (m.target == target) apiRemoveControllerMap (m.source);
    };

    // Control groups (VCA-lite): the mixer strip name menu drives the apiControlGroup* calls.
    mixerView->onListGroups  = [this]
    {
        std::vector<MixerView::GroupState> out;
        for (auto& g : apiListControlGroups()) out.push_back ({ g.name, g.gain, g.mute, g.solo });
        return out;
    };
    mixerView->onInsertGroup = [this] (int insert) -> juce::String
    {
        const juce::ScopedLock sl (engineLock);
        return juce::isPositiveAndBelow (insert, (int) mixerTracks.size()) ? mixerTracks[(size_t) insert]->group : juce::String();
    };
    mixerView->onAssignGroup = [this] (int insert, const juce::String& grp) { apiAssignInsertToGroup (insert, grp); };
    mixerView->onGroupGain   = [this] (const juce::String& grp, float gain) { apiDefineControlGroup (grp, gain); };
    mixerView->onGroupMute   = [this] (const juce::String& grp, bool mute)  { apiSetControlGroupMute (grp, mute); };
    mixerView->onGroupSolo   = [this] (const juce::String& grp, bool solo)  { apiSetControlGroupSolo (grp, solo); };
    mixerView->onRemoveGroup = [this] (const juce::String& grp) { apiRemoveControlGroup (grp); };
    mixerView->onListBuses   = [this]
    {
        std::vector<MixerView::BusInfo> out;
        for (auto& ins : apiListInserts()) if (ins.isBus) out.push_back ({ ins.index, ins.name });
        return out;
    };
    mixerView->onInsertSends = [this] (int insert) -> std::vector<MixerView::SendState>
    {
        std::vector<MixerView::SendState> out;
        const juce::ScopedLock sl (engineLock);
        if (juce::isPositiveAndBelow (insert, (int) mixerTracks.size()))
            for (auto& sd : mixerTracks[(size_t) insert]->sends) out.push_back ({ sd.bus, sd.level, sd.postFader });
        return out;
    };
    mixerView->onSetSend     = [this] (int insert, int bus, float level, bool post) { apiSetSend (insert, bus, level, post); if (mixerView) mixerView->rebuild(); };
    mixerView->onAddBus      = [this] (const juce::String& name) { apiAddBus (name); if (mixerView) { mixerView->rebuild(); mixerView->revealLastStrip(); } };   // scroll to show the new bus
    mixerView->getBackingTrack = [this] (int insert) -> MixerView::BackingTrack
    {
        const juce::ScopedLock sl (engineLock);
        for (auto& t : tracks)
            if (t->mixerTrack.load() == insert) return { true, t->name, t->colour };
        return {};
    };
    mixerView->onSetInsertName = [this] (int index, const juce::String& name)
    {
        // If a track is patched into this insert, rename the TRACK (so arrange/session/mixer stay in
        // sync); otherwise it's a bus / unused channel -> rename the insert itself.
        int backingId = -1;
        { const juce::ScopedLock sl (engineLock);
          for (auto& t : tracks) if (t->mixerTrack.load() == index) { backingId = t->id; break; } }
        if (backingId >= 0) apiRenameTrack (backingId, name);
        else                apiSetInsertName (index, name);
    };
    mixerView->onSetOutput   = [this] (int insert, int target)
    {
        apiSetInsertOutput (insert, target);
        if (target > 0) apiGatherGroup (target);   // grouping reorders members contiguous (Ableton-style)
        if (mixerView) mixerView->rebuild();
    };
    mixerView->onInsertOutput = [this] (int insert)
    {
        const juce::ScopedLock sl (engineLock);
        return juce::isPositiveAndBelow (insert, (int) mixerTracks.size()) ? mixerTracks[(size_t) insert]->output.load() : 0;
    };
    // Multi-select "Group" (Cmd+G): fold the selected strips into a new bus, then cluster
    // their tracks contiguous (Ableton-style) so the session shows them under one group column.
    mixerView->onGroupInserts = [this] (const std::vector<int>& inserts)
    {
        closeAllPluginWindows();
        const int bus = apiGroupInserts (inserts, "Group");
        if (bus > 0) apiGatherGroup (bus);
        if (mixerView)   { mixerView->rebuild(); mixerView->revealLastStrip(); }
        if (sessionPane) sessionPane->rebuild();
    };
    mixerView->onUngroup = [this] (int bus)
    {
        closeAllPluginWindows();
        apiUngroup (bus);
        if (mixerView)   mixerView->rebuild();
        if (sessionPane) sessionPane->rebuild();
    };
    mixerView->onOpenDeviceWindow = [this] (int insert) { openDeviceWindow (insert); };

    // The Mixer is an embedded view (Tab cycles to it / the toolbar button switches to it),
    // not a floating window — so it sits in the main area like Arrange and Session.
    mixerViewport.setViewedComponent (mixerView.get(), false);
    mixerViewport.setScrollBarsShown (false, true);
    addChildComponent (mixerViewport);

    // Start with an EMPTY project — no default tracks. Use File -> New from Template
    // (or the browser sidebar) to seed a drum kit / starter beat / lead+bass.

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
        h.panic        = [this] { apiPanic(); };
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
    if (auto* host = keyListenerHost.getComponent()) host->removeKeyListener (this);
    stopTimer();
    teardownMidiInputs();        // stop MIDI callbacks before tracks/audio go away
    grpc.reset();                // stop gRPC (and its message-thread callbacks) first
    osc.reset();                 // stop OSC before tracks/mixer are destroyed
    pluginWindows.clear();       // delete plugin editors before their processors
    deviceWindows.clear();       // detached device panels (their lambdas capture this)
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
        // Give the track its own mixer insert, placed just after the last track insert (before any
        // buses) so buses stay higher-indexed than the channels routing into them.
        const int pos = firstBusIndex();
        insertMixerTrackAt (pos, std::make_unique<MixerTrack> (track->name));
        track->mixerTrack.store (pos);
        ensureSlotCount (track->sessionSlots, (int) scenes.size());   // rectangular session grid
        tracks.push_back (std::move (track));
        sessionLauncher.setTrackCount ((int) tracks.size());
    }
    refreshTrackIds();
    if (! undoSuppressed && ! tracks.empty()) emitChange ("track_added", tracks.back()->id);
    if (arrangeView) arrangeView->rebuild();
    if (sessionPane) sessionPane->rebuild();
    if (mixerView)   mixerView->rebuild();   // a new insert appeared
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
            nextTrackId = juce::jmax (nextTrackId, t->id + 1);   // never reuse a preserved (loaded) id
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

std::vector<juce::String> MainComponent::apiListMidiInputs()
{
    // The MIDI input sources Gloopy can receive from. It auto-opens all of these (+ hot-plug), so
    // this is effectively "what Gloopy is listening to". Static ALSA query — safe from any thread.
    std::vector<juce::String> out;
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        out.push_back (d.name);
    return out;
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
        {
            t->liveMidi.addMessageToQueue (m);
            if (m.isNoteOn())   // flash the input LED on the track that's receiving
            {
                midiActivityTrackId.store (id);
                midiActivityMs.store (juce::Time::getMillisecondCounterHiRes());
            }
        }
}

void MainComponent::apiAuditionNote (int pitch, float velocity, bool noteOn)
{
    // Route to the edited/selected instrument via its lock-free live-MIDI collector —
    // the same path incoming MIDI and OSC notes use, so no render-thread work is added.
    int id = midiInputTarget.load();
    if (id < 0) id = firstInstrumentId.load();
    Track* t = resolveTrack (id);
    if (t == nullptr || t->generator == nullptr) return;

    auto m = noteOn ? juce::MidiMessage::noteOn  (1, juce::jlimit (0, 127, pitch), (juce::uint8) juce::jlimit (1, 127, (int) (velocity * 127.0f)))
                    : juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, pitch));
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);   // collector wants seconds
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

void MainComponent::startSessionRecord (int trackIndex, int scene)
{
    Track* t = trackByIndex (trackIndex);
    if (t == nullptr || t->generator == nullptr) return;
    if (recording.load()) finalizeRecording();      // finish any prior take first
    recordTrackId.store (t->id);
    recordStartSample = transport.getPlayheadSamples();
    recordWrite.store (0);
    sessionRecTrack = trackIndex; sessionRecScene = scene;
    recording.store (true);
    midiInputTarget.store (t->id);                  // route live input to this track
    transport.setPlaying (true);                    // roll so the record clock advances
}

void MainComponent::startSessionCapture()
{
    const juce::ScopedLock sl (engineLock);        // audio thread try-locks, so this is safe
    if (captureSegs.size() < 8192) captureSegs.resize (8192);
    captureWrite.store (0);
    const double arrBeat = juce::jmax (0.0, samplesToBeats (transport.getPlayheadSamples()).inBeats());
    capturePrevSlot.assign (tracks.size(), -1);
    captureStartBeat.assign (tracks.size(), arrBeat);
    captureLastBeat = arrBeat;
    sessionCapture.store (true);
    transport.setPlaying (true);
}

void MainComponent::finalizeSessionCapture()
{
    if (! sessionCapture.exchange (false)) return;
    pushUndoSnapshot();
    int built = 0;
    {
        const juce::ScopedLock sl (engineLock);
        // Flush spans for clips still playing at stop.
        for (int ti = 0; ti < (int) tracks.size() && ti < (int) capturePrevSlot.size(); ++ti)
            if (capturePrevSlot[(size_t) ti] >= 0)
            {
                const int idx = captureWrite.fetch_add (1);
                if (idx < (int) captureSegs.size())
                    captureSegs[(size_t) idx] = { ti, capturePrevSlot[(size_t) ti], captureStartBeat[(size_t) ti], captureLastBeat };
            }
        const int n = juce::jmin (captureWrite.load(), (int) captureSegs.size());
        for (int i = 0; i < n; ++i)
        {
            const auto& seg = captureSegs[(size_t) i];
            if (! juce::isPositiveAndBelow (seg.track, (int) tracks.size()) || seg.endBeat <= seg.startBeat + 1.0e-6) continue;
            auto src = slotClip (tracks[(size_t) seg.track]->sessionSlots, seg.scene);
            if (src == nullptr) continue;
            Clip c = *src;                                  // copy notes / content / loop
            c.startBeat   = juce::jmax (0.0, seg.startBeat);
            c.lengthBeats = juce::jmax (0.25, seg.endBeat - seg.startBeat);   // as long as it played; content tiles (looped)
            tracks[(size_t) seg.track]->clips.push_back (std::move (c));
            ++built;
        }
    }
    if (built > 0 && arrangeView) arrangeView->rebuild();
    if (built > 0) setViewMode (ViewMode::Arrange);         // reveal the captured performance
}

void MainComponent::finalizeRecording()
{
    stopAudioRecording();               // finalize audio takes independently of MIDI
    if (! recording.exchange (false)) return;
    const int srTrack = sessionRecTrack, srScene = sessionRecScene;   // session-record target (if any)
    sessionRecTrack = sessionRecScene = -1;                           // clear regardless of outcome
    const int count = juce::jmin (recordWrite.load(), (int) recordBuffer.size());
    Track* t = resolveTrack (recordTrackId.load());
    if (count == 0 || t == nullptr) return;

    // Tempo-aware: recorded positions are absolute song samples; map them through the
    // tempo map to beats (identical to /spb when the map is empty). Notes are stored
    // clip-relative, so subtract the clip's own absolute start beat.
    const double startBeatAbs = samplesToBeats (recordStartSample).inBeats();
    const auto toBeat = [&] (juce::int64 s) { return juce::jmax (0.0, samplesToBeats (s).inBeats() - startBeatAbs); };

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

    if (srScene >= 0 && juce::isPositiveAndBelow (srTrack, (int) tracks.size()))
    {
        // Session recording -> a looping clip in the slot, length rounded up to a whole bar.
        const double bar = juce::jmax (1.0, (double) transport.getTimeSigNumerator());
        Clip c;
        c.name = "Rec";
        c.contentLenBeats = juce::jmax (bar, std::ceil (maxEnd / bar) * bar);
        c.lengthBeats = c.contentLenBeats;
        c.looped = true;
        c.notes = std::move (notes);
        {
            const juce::ScopedLock sl (engineLock);
            ensureSlotCount (tracks[(size_t) srTrack]->sessionSlots, juce::jmax ((int) scenes.size(), srScene + 1));
            tracks[(size_t) srTrack]->sessionSlots[(size_t) srScene] = std::make_shared<Clip> (std::move (c));
            sessionLauncher.requestClip (srTrack, srScene);   // start looping the freshly recorded clip
        }
        if (sessionPane) sessionPane->rebuild();
        return;
    }

    Clip c;
    c.name = "Recording";
    c.startBeat = startBeatAbs;
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
void MainComponent::apiStop()  { if (recording.load()) finalizeRecording(); if (sessionCapture.load()) finalizeSessionCapture(); transport.setPlaying (false); transport.requestReset(); }
// MIDI panic: request the audio thread to send all-notes-off to every generator (clears stuck
// notes). Thread-safe from any caller (UI / gRPC / OSC) — just flips an atomic.
void MainComponent::apiPanic() { panicRequested.store (true); }
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
    return { transport.isPlaying(), transport.getBpm(), transport.getPlayheadBeats(),
             transport.isLoopEnabled(), transport.getLoopStartBeats(), transport.getLoopEndBeats(),
             transport.getSwing() };
}

bool MainComponent::apiSetMetronome (bool enabled) { metronomeEnabled.store (enabled); return enabled; }
bool MainComponent::apiGetMetronome() { return metronomeEnabled.load(); }
void  MainComponent::apiSetMetronomeLevel (float level) { metronomeLevel.store (juce::jlimit (0.0f, 1.0f, level)); }
float MainComponent::apiGetMetronomeLevel() { return metronomeLevel.load(); }

int MainComponent::apiAddSynthTrack (const juce::String& name, int wave, float a, float d, float s, float r, float g)
{
    return callOnMessageThread ([&] () -> int
    {
        auto sg = std::make_unique<SynthGenerator>();
        auto& p = sg->engine.params;
        p.waveform.store (juce::jlimit (0, 3, wave));
        p.attack.store (a); p.decay.store (d); p.sustain.store (s); p.release.store (r); p.gain.store (g);
        for (int i = 0; i < 12; ++i) p.tuning[(size_t) i].store ((float) projectTuning[(size_t) i]);   // inherit project microtuning
        auto t = std::make_unique<Track> (name.isNotEmpty() ? name : "Synth",
                                          std::move (sg), 60, paletteColour ((int) tracks.size()));
        Track* raw = t.get();
        addTrack (std::move (t));
        return raw->id;
    });
}

// Import an audio file (wav/aiff/flac) as a new audio track carrying one clip at bar 1,
// its length derived from the file duration at the current tempo. Shared by the
// "+ Audio" toolbar button and the ImportAudio RPC. Returns the new track id, or -1 on
// an unreadable/empty file. The decode + peak build happen off the message thread; only
// the track insertion is marshalled onto it (callOnMessageThread), mirroring apiImportMidi.
int MainComponent::apiImportAudio (const juce::String& path)
{
    const juce::File file = juce::File::isAbsolutePath (path)
                              ? juce::File (path)
                              : juce::File::getCurrentWorkingDirectory().getChildFile (path);
    if (! file.existsAsFile()) return -1;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0) return -1;

    auto buf = std::make_shared<juce::AudioBuffer<float>> (
        (int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (buf.get(), 0, (int) reader->lengthInSamples, 0, true, true);

    const double srcRate     = reader->sampleRate;
    const double durationSec = (double) reader->lengthInSamples / juce::jmax (1.0, srcRate);
    const juce::String name  = file.getFileNameWithoutExtension();
    auto peaks = std::make_shared<std::vector<float>> (buildPeaks (*buf));

    return callOnMessageThread ([&] () -> int
    {
        Clip c;
        c.type            = ClipType::Audio;
        c.name            = name;
        c.startBeat       = 0.0;
        c.lengthBeats     = juce::jmax (0.25, durationSec * transport.getBpm() / 60.0);
        c.audio           = buf;
        c.audioSourceRate = srcRate;
        c.peaks           = peaks;

        auto track = std::make_unique<Track> (name, nullptr, 60,
                         paletteColour ((int) tracks.size()), TrackType::Audio);
        Track* raw = track.get();
        track->clips.push_back (std::move (c));
        addTrack (std::move (track));
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
        else if (n == "detune")     p.detune.store   (juce::jlimit (-2400.0f, 2400.0f, value));   // ±2 octaves (cents)
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
                             t->volume.load(), t->pan.load(), t->mute.load(), (int) t->clips.size(),
                             t->colour.toString(), t->polarity.load() });   // 8-hex ARGB + phase-invert
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

bool MainComponent::apiSetInsertName (int index, const juce::String& name)
{
    if (name.trim().isEmpty()) return false;
    return callOnMessageThread ([&] () -> bool
    {
        if (! juce::isPositiveAndBelow (index, (int) mixerTracks.size())) return false;
        pushUndoSnapshot();
        { const juce::ScopedLock sl (engineLock); mixerTracks[(size_t) index]->name = name.trim(); }
        if (mixerView) { mixerView->rebuild(); mixerView->repaint(); }
        return true;
    });
}

int MainComponent::apiAddEffect (int insert, int type)
{
    return callOnMessageThread ([&] () -> int
    {
        pushUndoSnapshot();
        static const char* names[] = { "Gain", "Filter", "Delay", "Reverb", "Limiter", "Bitcrusher", "Compressor", "EQ", "Waveshaper", "Stereo Widener", "Tremolo", "Chorus", "Flanger", "Phaser", "Auto-pan", "Noise Gate", "Auto-wah", "Ring Mod" };
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
static float interpAuto (const std::vector<MainComponent::AutoPointSnap>& p, double beat,
                         bool step = false, float curve = 0.0f)
{
    if (p.empty()) return 0.0f;
    if (beat <= p.front().beat) return p.front().value;
    if (beat >= p.back().beat)  return p.back().value;
    for (size_t i = 1; i < p.size(); ++i)
        if (beat <= p[i].beat)
        {
            if (step) return p[i-1].value;   // hold the previous point's value until the next (stepped)
            double t = (beat - p[i-1].beat) / juce::jmax (1e-9, p[i].beat - p[i-1].beat);
            if (curve != 0.0f)               // warp t: curve>0 ease-in (slow start), <0 ease-out (fast start)
                t = std::pow (t, std::pow (2.0, 2.0 * (double) juce::jlimit (-1.0f, 1.0f, curve)));
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
        const float v = interpAuto (lane.points, beat, lane.step, lane.curve);
        if (lane.target.isNotEmpty()) { applyParamValue (lane.target, v); continue; }   // id-addressed (unified path)
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
            AutoLaneSnap lane { type, id, slot, param, points, {} };
            std::sort (lane.points.begin(), lane.points.end(),
                       [] (const AutoPointSnap& a, const AutoPointSnap& b) { return a.beat < b.beat; });
            automationLanes.push_back (std::move (lane));
        }
        return true;
    });
}

// Id-addressed automation: the lane targets a ParamModel id (the same string a
// controller/LFO uses), written each block through applyParamValue. Upsert by target;
// empty points clears it.
void MainComponent::apiSetAutomationById (const juce::String& target, const std::vector<AutoPointSnap>& points)
{
    if (target.trim().isEmpty()) return;
    callOnMessageThread ([&]
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        automationLanes.erase (std::remove_if (automationLanes.begin(), automationLanes.end(),
            [&] (const AutoLaneSnap& l) { return l.target == target; }), automationLanes.end());
        if (! points.empty())
        {
            AutoLaneSnap lane { -1, -1, -1, {}, points, target };
            std::sort (lane.points.begin(), lane.points.end(),
                       [] (const AutoPointSnap& a, const AutoPointSnap& b) { return a.beat < b.beat; });
            automationLanes.push_back (std::move (lane));
        }
        return true;
    });
}

// Toggle a target's automation lane between stepped (hold each point's value until the
// next) and linear interpolation. Applies to the id-addressed lane; the points are kept.
bool MainComponent::apiSetAutomationStep (const juce::String& target, bool step)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        for (auto& l : automationLanes)
            if (l.target == target) { l.step = step; return true; }
        return false;
    });
}

bool MainComponent::apiGetAutomationStep (const juce::String& target)
{
    return callOnMessageThread ([&] () -> bool
    {
        const juce::ScopedLock sl (engineLock);
        for (auto& l : automationLanes) if (l.target == target) return l.step;
        return false;
    });
}

// Ease curve for a target's automation lane: -1 (ease-out, fast start) .. 0 (linear) .. +1
// (ease-in, slow start). Ignored while the lane is stepped. Points are kept.
bool MainComponent::apiSetAutomationCurve (const juce::String& target, float curve)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        for (auto& l : automationLanes)
            if (l.target == target) { l.curve = juce::jlimit (-1.0f, 1.0f, curve); return true; }
        return false;
    });
}

float MainComponent::apiGetAutomationCurve (const juce::String& target)
{
    return callOnMessageThread ([&] () -> float
    {
        const juce::ScopedLock sl (engineLock);
        for (auto& l : automationLanes) if (l.target == target) return l.curve;
        return 0.0f;
    });
}

// Append (or replace, if one already sits at that beat) a single point on a target's
// id-addressed lane — the primitive the "Automate at playhead" desktop hook uses to
// build a curve keyframe by keyframe.
bool MainComponent::apiAddAutomationPointById (const juce::String& target, double beat, float value)
{
    if (target.trim().isEmpty()) return false;
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        auto it = std::find_if (automationLanes.begin(), automationLanes.end(),
                                [&] (const AutoLaneSnap& l) { return l.target == target; });
        if (it == automationLanes.end())
        {
            automationLanes.push_back ({ -1, -1, -1, {}, { { beat, value } }, target });
            return true;
        }
        auto& pts = it->points;
        auto p = std::find_if (pts.begin(), pts.end(),
                               [&] (const AutoPointSnap& q) { return std::abs (q.beat - beat) < 1.0e-6; });
        if (p != pts.end()) p->value = value;            // replace the keyframe at this beat
        else                pts.push_back ({ beat, value });
        std::sort (pts.begin(), pts.end(),
                   [] (const AutoPointSnap& a, const AutoPointSnap& b) { return a.beat < b.beat; });
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
                if (tracks[i]->id == id)
                {
                    const int mi = tracks[i]->mixerTrack.load();   // remove the track's own insert too
                    tracks.erase (tracks.begin() + (long) i);
                    if (mi >= 1 && mi < (int) mixerTracks.size() && ! mixerTracks[(size_t) mi]->isBus)
                        removeMixerTrackAt (mi);
                    ok = true; break;
                }
            // Track positions shifted — reset session playback and resize (transient state only).
            sessionLauncher.reset();
            sessionLauncher.setTrackCount ((int) tracks.size());
        }
        if (! ok) return false;
        refreshTrackIds();
        emitChange ("track_removed", id);
        if (arrangeView) arrangeView->rebuild();
        if (sessionPane) sessionPane->rebuild();
        if (mixerView)   mixerView->rebuild();   // its insert is gone
        selectClip (-1, -1);
        resized();
        return true;
    });
}

int MainComponent::apiDuplicateTrack (int id)
{
    return callOnMessageThread ([&] () -> int
    {
        // Clone the source track's subtree, then build ONE track from it (no full
        // project reload — the reload path crashes). buildTrackFromTree reconstructs the
        // generator + clips; addTrack assigns the duplicate its own mixer strip.
        juce::ValueTree full = toValueTree();
        auto trks = full.getChildWithName ("TRACKS");
        juce::ValueTree src;
        for (int i = 0; i < trks.getNumChildren(); ++i)
            if ((int) trks.getChild (i).getProperty ("tid", 0) == id) { src = trks.getChild (i); break; }
        if (! src.isValid()) return -1;

        juce::ValueTree dup = src.createCopy();
        dup.setProperty ("name", src.getProperty ("name", "Track").toString() + " copy", nullptr);
        dup.removeProperty ("tid", nullptr);           // let refreshTrackIds assign a fresh stable id
        auto t = buildTrackFromTree (dup);
        if (t == nullptr) return -1;
        t->id = -1;                                    // force a new id (addTrack -> refreshTrackIds)
        addTrack (std::move (t));                      // own mixer strip, prepare, undo, push, rebuild
        return tracks.empty() ? -1 : tracks.back()->id;
    });
}

bool MainComponent::apiRenameTrack (int id, const juce::String& name)
{
    if (name.trim().isEmpty()) return false;
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (id);
        if (t == nullptr) return false;
        pushUndoSnapshot();
        {
            const juce::ScopedLock sl (engineLock);
            t->name = name.trim();
        }
        emitChange ("track_renamed", id);
        if (arrangeView) arrangeView->rebuild();
        if (mixerView)   mixerView->rebuild();   // the track's mixer strip shows its name -> refresh the label
        resized();
        return true;
    });
}

bool MainComponent::apiSetTrackPolarity (int id, bool invert)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (id);
        if (t == nullptr) return false;
        pushUndoSnapshot();
        t->polarity.store (invert);   // atomic; the audio thread reads it each block
        emitChange ("track_polarity", id);
        if (arrangeView) arrangeView->rebuild();
        return true;
    });
}

bool MainComponent::apiMoveTrack (int id, int delta)
{
    if (delta == 0) return false;
    return callOnMessageThread ([&] () -> bool
    {
        // Locate + range-check under the lock first, so we only snapshot on a real move.
        int idx = -1, target = -1;
        {
            const juce::ScopedLock sl (engineLock);
            for (size_t i = 0; i < tracks.size(); ++i)
                if (tracks[i]->id == id) { idx = (int) i; break; }
            if (idx < 0) return false;
            target = idx + (delta < 0 ? -1 : 1);   // -1 = up (toward top), +1 = down
            if (! juce::isPositiveAndBelow (target, (int) tracks.size())) return false;   // at an edge
        }
        pushUndoSnapshot();
        {
            const juce::ScopedLock sl (engineLock);
            std::swap (tracks[(size_t) idx], tracks[(size_t) target]);   // mixerTrack lives on the Track, so routing is unchanged
        }
        emitChange ("track_moved", id);
        if (arrangeView) arrangeView->rebuild();
        resized();
        return true;
    });
}

bool MainComponent::apiSetTrackColour (int id, const juce::String& hexArgb)
{
    const auto col = juce::Colour::fromString (hexArgb.startsWith ("#") ? hexArgb.substring (1) : hexArgb);
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (id);
        if (t == nullptr) return false;
        pushUndoSnapshot();
        {
            const juce::ScopedLock sl (engineLock);
            t->colour = col;
        }
        emitChange ("track_coloured", id);
        if (arrangeView) arrangeView->rebuild();
        repaint();
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

// Live playback controls for a one-shot Sampler track: window [start,end] (fractions of
// the sample), reverse, and root note. rootNote <= 0 leaves the root unchanged. Set under
// engineLock (the audio thread reads these fields in render/startVoice).
bool MainComponent::apiSetSamplerControls (int trackId, float startFrac, float endFrac, bool reverse, int rootNote, float fadeIn, float fadeOut, bool loop, bool mono)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        for (auto& t : tracks)
            if (t->id == trackId)
                if (auto* sm = dynamic_cast<Sampler*> (t->generator.get()))
                {
                    sm->setPlaybackWindow (startFrac, endFrac, reverse);
                    sm->setFades (fadeIn, fadeOut);
                    sm->setLoop (loop);
                    sm->setMono (mono);
                    if (rootNote > 0) sm->setRootNote (rootNote);
                    return true;
                }
        return false;
    });
}

MainComponent::SamplerSnap MainComponent::apiGetSamplerControls (int trackId)
{
    return callOnMessageThread ([&] () -> SamplerSnap
    {
        const juce::ScopedLock sl (engineLock);
        for (auto& t : tracks)
            if (t->id == trackId)
                if (auto* sm = dynamic_cast<Sampler*> (t->generator.get()))
                    return { true, sm->getStartFrac(), sm->getEndFrac(), sm->getReverse(), sm->getRootNote(),
                             sm->getFadeIn(), sm->getFadeOut(), sm->getLoop(), sm->getMono(), sm->getName() };
        return {};
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

int MainComponent::apiAddSurgeTrack (const juce::String& name, const juce::String& patch)
{
   #ifndef GLOOPY_WITH_SURGE
    juce::ignoreUnused (name, patch);
    return -1;   // Surge engine not compiled into this binary (GLOOPY_WITH_SURGE=OFF)
   #else
    return callOnMessageThread ([&] () -> int
    {
        pushUndoSnapshot();
        auto surge = std::make_unique<SurgeGenerator>();
        surge->prepare (currentSampleRate, currentBlockSize);
        if (patch.isNotEmpty())
        {
            juce::String err;
            if (! surge->loadPatch (juce::File (patch), err))
                std::cout << "[surge] patch load: " << err << std::endl;
        }
        const auto disp = name.isNotEmpty() ? name : surge->getName();
        auto t = std::make_unique<Track> (disp, std::move (surge), 60, paletteColour ((int) tracks.size()));
        Track* raw = t.get();
        addTrack (std::move (t));
        return raw->id;
    });
   #endif
}

// Build a Surge track off the message thread (constructing the synth scans its patch
// library, and loading a patch is not instant) behind the busy overlay, then install it.
// Shared by the "+ Synth -> Surge XT" menu and the Presets browser tab.
void MainComponent::addSurgeTrackAsync (const juce::String& patchPath, const juce::String& name)
{
    auto slot = std::make_shared<std::unique_ptr<SurgeGenerator>>();
    const double sr = currentSampleRate; const int bs = currentBlockSize;
    const juce::String label = name.isNotEmpty() ? name : "Surge";
    runBackground ("Adding " + label + "…",
        [slot, sr, bs, patchPath]
        {
            auto g = std::make_unique<SurgeGenerator>();
            g->prepare (sr, bs);
            if (patchPath.isNotEmpty())
            {
                juce::String err;
                if (! g->loadPatch (juce::File (patchPath), err))
                    std::cout << "[surge] patch load: " << err << std::endl;
            }
            *slot = std::move (g);
        },
        [this, slot, label]
        {
            if (*slot)
                addTrack (std::make_unique<Track> ((*slot)->getName().isNotEmpty() ? (*slot)->getName() : label,
                              std::move (*slot), 48, paletteColour ((int) tracks.size())));   // addTrack pushes undo
        });
}

// The first-party Surge factory patches under <surge-data>/patches_factory, as
// "Category/Name" labels; also fills browserSurgePatches (label -> full .fxp path).
std::vector<juce::String> MainComponent::listSurgePatches() const
{
    std::vector<juce::String> out;
    const auto root = SurgeGenerator::dataDir().getChildFile ("patches_factory");
    if (! root.isDirectory()) return out;
    for (const auto& e : juce::RangedDirectoryIterator (root, true, "*.fxp", juce::File::findFiles))
    {
        const auto f = e.getFile();
        const auto label = f.getParentDirectory().getFileName() + "/" + f.getFileNameWithoutExtension();
        const_cast<MainComponent*> (this)->browserSurgePatches[label] = f.getFullPathName();
        out.push_back (label);
    }
    std::sort (out.begin(), out.end());
    return out;
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
    if (! f.hasFileExtension ("wav") && ! f.hasFileExtension ("flac"))
        f = f.withFileExtension ("wav");   // default container; .flac is honoured below
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

    // Tempo-aware range: a named/explicit range is in beats; map it through the tempo
    // map (identical to beat*spb when the map is empty).
    const juce::int64 startSample = beatToSamples (gloopy::time::BeatPosition { juce::jmax (0.0, startBeat) });

    // Start at the range beginning, playing, ignoring any live seek/reset or loop region.
    resetModulationSmoothing();   // deterministic slew: each offline render seeds afresh
    // Reset every insert/master effect's internal state (delay lines, LFO phase) so a
    // bounce is bit-reproducible run-to-run — otherwise a delay/chorus/flanger/phaser
    // carries state from a previous render (composition-as-repo wants deterministic renders).
    for (auto& mt : mixerTracks) for (auto& fx : mt->effects) fx->reset();
    double dummy; transport.consumeSeek (dummy); transport.consumeReset();
    transport.setPlaying (true);
    transport.setPlayheadSamples (startSample);
    for (auto& t : tracks) if (t->generator) t->generator->allNotesOff();

    const int    block = juce::jmax (32, currentBlockSize);
    const double rate  = currentSampleRate;
    const double tail  = tailSeconds > 0.0 ? tailSeconds : 2.0;

    // Pick the encoder by output extension: FLAC for .flac (lossless archive), WAV
    // otherwise. Both write 24-bit/stereo at the device rate.
    std::unique_ptr<juce::AudioFormat> fmt;
    if (f.hasFileExtension ("flac")) fmt = std::make_unique<juce::FlacAudioFormat>();
    else                             fmt = std::make_unique<juce::WavAudioFormat>();
    auto os = f.createOutputStream();
    if (os == nullptr) { restore(); return false; }
    std::unique_ptr<juce::AudioFormatWriter> writer (fmt->createWriterFor (os.release(), rate, 2, 24, {}, 0));
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
            const juce::int64 endSample = endBeat > startBeat ? beatToSamples (gloopy::time::BeatPosition { endBeat }) : songLen;
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

// Built-in project templates (a new project is EMPTY by default; templates are opt-in,
// reachable via File -> New from Template and, in future, the browser sidebar). Each
// builds tracks into the current (freshly-emptied) project. Keep this list the single
// source of truth for apiListTemplates().
juce::StringArray MainComponent::builtinTemplateNames() const
{
    return { "Starter Beat", "Piano + Bass + Drums", "Drum Kit", "Lead + Bass" };
}

// Locate the Piano-template SFZ. Prefer the vendored Salamander Grand Piano (Retuned)
// that ships with Gloopy; fall back to a copy next to the executable, then to ~/sfz.
// Empty File if none (caller falls back to a synth patch).
juce::File MainComponent::findPianoSfz() const
{
    const juce::String vendored = "SalamanderGrandPiano/SalamanderGrandPianoV3Retuned.sfz";

   #ifdef GLOOPY_ASSETS_DIR
    {
        auto f = juce::File (GLOOPY_ASSETS_DIR).getChildFile (vendored);
        if (f.existsAsFile()) return f;
    }
   #endif
    // Next to the executable (installed / relocated builds).
    {
        auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        auto f = exeDir.getChildFile ("assets").getChildFile (vendored);
        if (f.existsAsFile()) return f;
    }
    // The user's own ~/sfz collection.
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("sfz");
    if (dir.isDirectory())
    {
        for (auto* pref : { "SalamanderGrandPianoV3_OggVorbis/SalamanderGrandPianoV3Retuned.sfz",
                            "SalamanderGrandPianoV3_OggVorbis/SalamanderGrandPianoV3.sfz" })
        {
            auto f = dir.getChildFile (pref);
            if (f.existsAsFile()) return f;
        }
        auto hits = dir.findChildFiles (juce::File::findFiles, true, "*iano*.sfz");
        if (! hits.isEmpty()) return hits[0];
    }
    return {};
}

void MainComponent::buildTemplate (const juce::String& name)
{
    auto drum = [this] (const juce::String& n, juce::AudioBuffer<float> buf, juce::Colour col)
    {
        auto sampler = std::make_unique<Sampler>();
        sampler->prepare (currentSampleRate, currentBlockSize);
        sampler->setSample (std::move (buf), DrumSynth::kRate, n);
        addTrack (std::make_unique<Track> (n, std::move (sampler), 60, col));
    };
    auto synth = [this] (const juce::String& n, int wave, float release, int pitch, juce::Colour col)
    {
        auto g = std::make_unique<SynthGenerator>();
        g->engine.params.waveform.store (wave);
        g->engine.params.release.store (release);
        addTrack (std::make_unique<Track> (n, std::move (g), pitch, col));
    };

    if (name == "Lead + Bass")
    {
        synth ("Lead", 0, 0.25f, 60, juce::Colours::aquamarine);
        synth ("Bass", 1, 0.15f, 36, juce::Colours::skyblue);
        return;
    }

    if (name == "Piano + Bass + Drums")
    {
        // Prefer a real sampled piano (Salamander Grand / any *piano*.sfz under ~/sfz).
        // Fall back to a synth voiced as an electric-piano/keys patch if none is installed.
        auto synthPiano = [this]
        {
            auto piano = std::make_unique<SynthGenerator>();
            auto& pp = piano->engine.params;
            pp.waveform.store (3); pp.subLevel.store (0.2f);
            pp.attack.store (0.002f); pp.decay.store (1.2f); pp.sustain.store (0.0f); pp.release.store (0.35f);
            pp.cutoff.store (6000.0f); pp.resonance.store (0.7f);
            pp.filterEnvAmt.store (1.5f); pp.fAttack.store (0.002f); pp.fDecay.store (0.8f); pp.fSustain.store (0.0f);
            addTrack (std::make_unique<Track> ("Piano", std::move (piano), 60, juce::Colours::whitesmoke));
        };
        const auto pianoSfz = findPianoSfz();
        bool loaded = false;
        if (pianoSfz.existsAsFile())
        {
            auto sfz = std::make_unique<SfizzGenerator>();
            sfz->prepare (currentSampleRate, currentBlockSize);
            juce::String err;
            if (sfz->loadSfz (pianoSfz, err))
            {
                addTrack (std::make_unique<Track> ("Piano", std::move (sfz), 60, juce::Colours::whitesmoke));
                loaded = true;
            }
            else std::cout << "[template] piano SFZ load failed: " << err << std::endl;
        }
        if (! loaded) synthPiano();

        synth ("Bass", 1, 0.15f, 36, juce::Colours::skyblue);
        drum ("Kick",  DrumSynth::makeKick(),  juce::Colours::orangered);
        drum ("Snare", DrumSynth::makeSnare(), juce::Colours::gold);
        drum ("Hat",   DrumSynth::makeHat(),   juce::Colours::aquamarine);
        drum ("Clap",  DrumSynth::makeClap(),  juce::Colours::violet);
        return;
    }

    // "Starter Beat" and "Drum Kit" both build the kit; only Starter Beat seeds a groove.
    drum ("Kick",  DrumSynth::makeKick(),  juce::Colours::orangered);
    drum ("Snare", DrumSynth::makeSnare(), juce::Colours::gold);
    drum ("Hat",   DrumSynth::makeHat(),   juce::Colours::aquamarine);
    drum ("Clap",  DrumSynth::makeClap(),  juce::Colours::violet);
    synth ("Bass", 1, 0.15f, 36, juce::Colours::skyblue);

    if (name != "Starter Beat") return;

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

void MainComponent::runBackground (const juce::String& label,
                                   std::function<void()> heavy, std::function<void()> done)
{
    busyOverlay.show (label);
    bgPool.addJob ([this, heavy = std::move (heavy), done = std::move (done)]
    {
        heavy();                                        // pool thread: the slow part
        juce::MessageManager::callAsync ([this, done]   // back on the message thread
        {
            done();                                     // touch the engine safely here
            busyOverlay.hide();
        });
    });
}

// User templates are saved projects: <userAppData>/Gloopy/templates/<name>.gloopy
// (override the base with GLOOPY_TEMPLATE_PATH, mirroring presetsDir).
juce::File MainComponent::templatesDir() const
{
    auto base = juce::SystemStats::getEnvironmentVariable ("GLOOPY_TEMPLATE_PATH", {});
    return base.isNotEmpty()
        ? juce::File (base)
        : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Gloopy").getChildFile ("templates");
}

// Bundled example compositions for the browser's Demos tab. Resolved from
// $GLOOPY_EXAMPLES_PATH, else an "examples" folder next to the CWD or the executable.
juce::File MainComponent::demosDir() const
{
    auto base = juce::SystemStats::getEnvironmentVariable ("GLOOPY_EXAMPLES_PATH", {});
    if (base.isNotEmpty()) return juce::File (base);
    const juce::File cands[] = {
        juce::File::getCurrentWorkingDirectory().getChildFile ("examples"),
        juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getChildFile ("examples"),
        juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getParentDirectory().getChildFile ("examples"),
    };
    for (auto& c : cands) if (c.isDirectory()) return c;
    return cands[0];
}

// Names of composition folders (each holding a gloopy.toml) under demosDir().
std::vector<juce::String> MainComponent::listDemos() const
{
    std::vector<juce::String> out;
    const auto dir = demosDir();
    if (! dir.isDirectory()) return out;
    for (const auto& e : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findDirectories))
        if (e.getFile().getChildFile ("gloopy.toml").existsAsFile())
            out.push_back (e.getFile().getFileName());
    std::sort (out.begin(), out.end());
    return out;
}

// Audio-sample folder for the browser's Samples tab. Resolved from
// $GLOOPY_SAMPLES_PATH, else a "samples" folder next to the CWD, else ~/Music.
juce::File MainComponent::samplesDir() const
{
    auto base = juce::SystemStats::getEnvironmentVariable ("GLOOPY_SAMPLES_PATH", {});
    if (base.isNotEmpty()) return juce::File (base);
    auto cwd = juce::File::getCurrentWorkingDirectory().getChildFile ("samples");
    if (cwd.isDirectory()) return cwd;
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory);
}

// Names of importable audio files (wav/aiff/flac) under samplesDir().
std::vector<juce::String> MainComponent::listSamples() const
{
    std::vector<juce::String> out;
    const auto dir = samplesDir();
    if (! dir.isDirectory()) return out;
    for (const auto& e : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFiles))
    {
        const auto f = e.getFile();
        if (f.hasFileExtension ("wav;aif;aiff;flac"))
            out.push_back (f.getFileName());
    }
    std::sort (out.begin(), out.end());
    return out;
}

std::vector<juce::String> MainComponent::apiListTemplates()
{
    std::vector<juce::String> out;
    const auto builtins = builtinTemplateNames();
    for (auto& n : builtins) out.push_back (n);
    for (auto& f : templatesDir().findChildFiles (juce::File::findFiles, false, "*.gloopy"))
    {
        const auto n = f.getFileNameWithoutExtension();
        if (! builtins.contains (n)) out.push_back (n);   // built-ins win a name clash
    }
    return out;
}

// Save the current project as a reusable user template (a .gloopy in templatesDir).
bool MainComponent::apiSaveAsTemplate (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    return callOnMessageThread ([&] () -> bool
    {
        auto dir = templatesDir();
        if (! dir.createDirectory()) return false;
        auto file = dir.getChildFile (clean + ".gloopy");
        const auto prev = currentProjectFile;   // saving a template must not retitle the open project
        saveProject (file);
        currentProjectFile = prev;
        return file.existsAsFile();
    });
}

bool MainComponent::apiNewFromTemplate (const juce::String& name)
{
    const bool builtin = builtinTemplateNames().contains (name);
    juce::File userFile = builtin ? juce::File() : templatesDir().getChildFile (juce::File::createLegalFileName (name.trim()) + ".gloopy");
    if (! builtin && ! userFile.existsAsFile()) return false;
    return callOnMessageThread ([&] () -> bool
    {
        if (builtin)
        {
            newProject();                 // empties the project
            undoSuppressed = true;
            buildTemplate (name);
            undoSuppressed = false;
            undoStack.clear(); redoStack.clear();
            refreshUiAfterLoad();
        }
        else
        {
            openAny (userFile);           // load the saved-template project
            currentProjectFile = juce::File();   // a template seeds a NEW untitled project, not the template file
        }
        return true;
    });
}

void MainComponent::selectClip (int track, int clip)
{
    selTrack = track; selClip = clip;
    selSessionTrack = selSessionScene = -1;            // arrangement selection wins
    if (arrangeView) arrangeView->setSelection (track, clip);
    loadSelectedClipIntoEditor();
    // Arm the selected instrument track for live MIDI input.
    midiInputTarget.store (juce::isPositiveAndBelow (track, (int) tracks.size())
                             && tracks[(size_t) track]->generator != nullptr
                           ? tracks[(size_t) track]->id : -1);
    if (bottomMode == BottomMode::Devices) refreshDevicePanel();
}

void MainComponent::selectSessionClip (int trackIndex, int scene)
{
    selSessionTrack = trackIndex; selSessionScene = scene;
    selTrack = selClip = -1;                            // editing a session slot, not an arrangement clip
    if (arrangeView) arrangeView->setSelection (-1, -1);
    loadSelectedClipIntoEditor();
    midiInputTarget.store (juce::isPositiveAndBelow (trackIndex, (int) tracks.size())
                             && tracks[(size_t) trackIndex]->generator != nullptr
                           ? tracks[(size_t) trackIndex]->id : -1);
    if (bottomMode == BottomMode::Devices) refreshDevicePanel();
}

// Point the device panel at the mixer insert of the currently selected track (arrangement or
// session). Called on selection changes while the device view is showing, and when it opens.
void MainComponent::refreshDevicePanel()
{
    const int tIdx = selTrack >= 0 ? selTrack : selSessionTrack;
    {
        const juce::ScopedLock sl (engineLock);
        deviceTrack = juce::isPositiveAndBelow (tIdx, (int) tracks.size())
                        ? juce::jlimit (0, (int) mixerTracks.size() - 1, tracks[(size_t) tIdx]->mixerTrack.load())
                        : -1;
    }
    devicePanel.refresh();
}

// The clip currently loaded in the editor — a session slot (if selected) or an arrangement clip.
// Caller must hold engineLock. Sets outTrackIndex to the owning track, or -1 if none.
Clip* MainComponent::editingClip (int& outTrackIndex)
{
    outTrackIndex = -1;
    if (selSessionScene >= 0 && juce::isPositiveAndBelow (selSessionTrack, (int) tracks.size()))
    {
        auto& slots = tracks[(size_t) selSessionTrack]->sessionSlots;
        if (selSessionScene < (int) slots.size() && slots[(size_t) selSessionScene] != nullptr)
        { outTrackIndex = selSessionTrack; return slots[(size_t) selSessionScene].get(); }
        return nullptr;
    }
    if (juce::isPositiveAndBelow (selTrack, (int) tracks.size())
          && juce::isPositiveAndBelow (selClip, (int) tracks[(size_t) selTrack]->clips.size()))
    { outTrackIndex = selTrack; return &tracks[(size_t) selTrack]->clips[(size_t) selClip]; }
    return nullptr;
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
    std::vector<Note> ghosts;
    double contentLen = 4.0;
    int pitch = 60;
    juce::String trackName;
    {
        const juce::ScopedLock sl (engineLock);
        int et = -1;
        if (Clip* cp = editingClip (et))
        {
            const auto& c = *cp;
            trackName = tracks[(size_t) et]->name;
            if (c.isAudio())
                isAudio = true;
            else
            {
                notes = c.notes;
                contentLen = c.looped ? c.contentLenBeats : c.lengthBeats;
                pitch = tracks[(size_t) et]->defaultPitch;
                valid = true;

                // Ghost notes only for arrangement clips (they map to a timeline position); a
                // session clip is a free-floating loop, so it gets none.
                if (selSessionScene < 0)
                {
                    const double selStart = c.startBeat;
                    for (int ti = 0; ti < (int) tracks.size(); ++ti)
                    {
                        if (ti == et || tracks[(size_t) ti]->generator == nullptr) continue;
                        for (const auto& gc : tracks[(size_t) ti]->clips)
                        {
                            if (gc.isAudio()) continue;
                            for (const auto& gn : gc.notes)
                            {
                                const double rel = (gc.startBeat + gn.startBeat) - selStart;
                                if (rel >= 0.0 && rel < contentLen)
                                    ghosts.push_back ({ gn.pitch, rel, gn.lengthBeats, gn.velocity });
                            }
                        }
                    }
                }
            }
        }
    }

    editorPanel.roll.setLength (contentLen);
    editorPanel.roll.setGhostNotes (std::move (ghosts));
    editorPanel.roll.loadNotes (notes);
    editorPanel.roll.setEnabledEditing (valid);
    editorPanel.steps.setContent (contentLen, pitch);
    editorPanel.steps.loadNotes (notes);
    editorPanel.steps.setEnabledEditing (valid);

    const juce::String dot = "  " + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2")) + "  ";   // UTF-8 bullet
    if (valid)
        editorPanel.title.setText ("EDITOR" + dot + trackName.toUpperCase()
                                     + dot + (selSessionScene >= 0 ? "SESSION CLIP" : "CLIP"),
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
        int et = -1;
        if (Clip* cp = editingClip (et); cp != nullptr && ! cp->isAudio())
        {
            cp->notes = notes;
            if (selSessionScene < 0 && tracks[(size_t) et]->arp.enabled)
                applyArpToTrack (*tracks[(size_t) et]);   // arrangement clip: keep the live-arp expansion current
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

    if (panicRequested.exchange (false))                 // MIDI panic: kill any stuck/hanging notes
    {
        for (auto& t : tracks) if (t->generator) { t->generator->allNotesOff(); t->liveArp.reset(); }
        sessionLauncher.reset();                          // stop any launched session clips
    }

    if (transport.consumeReset())
    {
        transport.setPlayheadSamples (0);
        for (auto& t : tracks) if (t->generator) { t->generator->allNotesOff(); t->liveArp.reset(); }
        sessionLauncher.reset(); sessionBeat = 0.0;        // stop = clear session playback + clock
    }

    const bool        playing = transport.isPlaying();
    const double      spb     = transport.samplesPerBeat();
    const juce::int64 blockStartPlayhead = transport.getPlayheadSamples();   // for MIDI capture

    // Snapshot the tempo map once per block (allocation-free) so the scheduler can
    // convert beats<->samples on the audio thread without locking or heap traffic.
    // Empty map -> constant path (llround(beat*spb)), identical to the pre-map engine.
    // renderBlock always runs under engineLock (audio: try-lock; offline: scoped), so
    // reading tempoMap here is safe. tempoMap is kept beat-ascending on edit/load.
    TempoConv tc;
    {
        const int nm = juce::jmin ((int) tempoMap.size(), TempoConv::kMaxMarkers);
        if (nm <= 0) tc.setMarkers (nullptr, nullptr, 0, currentSampleRate, spb);
        else
        {
            double beats[TempoConv::kMaxMarkers], bpms[TempoConv::kMaxMarkers];
            for (int i = 0; i < nm; ++i) { beats[i] = tempoMap[(size_t) i].beat; bpms[i] = tempoMap[(size_t) i].bpm; }
            tc.setMarkers (beats, bpms, nm, currentSampleRate, spb);
        }
    }

    if (playing && ! automationLanes.empty())
        evaluateAutomation (tc.sampleToBeat (blockStartPlayhead));

    // LFO modulation is driven off the playhead so a render is deterministic — seconds
    // for free LFOs, beats (via the tempo map) for tempo-synced ones.
    if (! modulations.empty())
        evaluateModulation ((double) blockStartPlayhead / juce::jmax (1.0, currentSampleRate),
                            tc.sampleToBeat (blockStartPlayhead));

    // Seek (from dragging the playhead) — applies whether playing or stopped.
    double seekBeats = 0.0;
    if (transport.consumeSeek (seekBeats))
    {
        transport.setPlayheadSamples (juce::jmax ((juce::int64) 0, tc.beatToSample (seekBeats)));
        for (auto& t : tracks) if (t->generator) { t->generator->allNotesOff(); t->liveArp.reset(); }
    }

    // Song length = furthest clip end (min 1 bar).
    double songBeats = 4.0;
    for (auto& t : tracks)
        for (auto& c : t->clips)
            songBeats = juce::jmax (songBeats, c.endBeat());
    juce::int64 loopLen = tc.beatToSample (songBeats);
    if (loopLen < 1) loopLen = 1;

    // Playback window: the loop region if enabled, else the whole song.
    juce::int64 winStart = 0, winEnd = loopLen;
    if (transport.isLoopEnabled() && ! ignoreLoopWindow)
    {
        winStart = juce::jlimit ((juce::int64) 0, loopLen, tc.beatToSample (transport.getLoopStartBeats()));
        winEnd   = tc.beatToSample (transport.getLoopEndBeats());
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
    auto collectClip = [&tc, swing] (juce::MidiBuffer& midi, const Clip& clip,
                              juce::int64 songStart, int chunk, int tsOffset, bool useArp)
    {
        if (clip.type != ClipType::Midi || clip.muted) return;   // muted = disabled clip / inactive take
        // Live arp: play the precomputed expansion instead of the raw chord (both under the lock).
        const std::vector<Note>& src = (useArp && ! clip.arpNotes.empty()) ? clip.arpNotes : clip.notes;
        const juce::int64 songEnd   = songStart + chunk;
        const juce::int64 clipStart = tc.beatToSample (clip.startBeat);
        const juce::int64 clipEnd   = tc.beatToSample (clip.endBeat());

        const juce::int64 lo = juce::jmax (songStart, clipStart);
        const juce::int64 hi = juce::jmin (songEnd, clipEnd);
        if (lo >= hi) return;

        // Repetitions tile the clip in BEAT space from clip.startBeat, so each rep's
        // sample stride follows the tempo map (it's constant only when the map is).
        const double repBeats = clip.looped ? clip.contentLenBeats : clip.lengthBeats;
        const bool   tiled    = repBeats > 0.0
                                && tc.beatToSample (clip.startBeat + repBeats) - clipStart >= 1;
        if (! tiled)   // single pass over the whole clip window (degenerate/one-shot)
        {
            collectNotes (src, midi, tc, clip.startBeat, songStart, tsOffset, lo, hi, swing, clip.transpose, clip.velocityScale);
            return;
        }

        // First repetition whose beat span can reach 'lo'; walk forward until past 'hi'.
        int repK = (int) std::floor ((tc.sampleToBeat (lo) - clip.startBeat) / repBeats);
        if (repK < 0) repK = 0;
        for (;; ++repK)
        {
            const double     repStartBeat = clip.startBeat + repK * repBeats;
            const juce::int64 repStart = tc.beatToSample (repStartBeat);
            if (repStart >= hi) break;
            const juce::int64 repEnd = tc.beatToSample (repStartBeat + repBeats);
            const juce::int64 winLo  = juce::jmax (lo, repStart);
            const juce::int64 winHi  = juce::jmin (hi, repEnd);
            if (winLo < winHi)
                collectNotes (src, midi, tc, repStartBeat, songStart, tsOffset, winLo, winHi, swing, clip.transpose, clip.velocityScale);
        }
    };

    // Render an audio clip's samples over a song-sample window (natural speed).
    auto renderAudioClip = [&tc, deviceRate] (juce::AudioBuffer<float>& buffer, const Clip& clip,
                                              juce::int64 songStart, int chunk, int tsOffset)
    {
        if (! clip.isAudio() || clip.audio == nullptr || clip.muted) return;   // muted = inactive take
        const auto& ab = *clip.audio;
        const int frames = ab.getNumSamples();
        if (frames <= 0) return;

        const int nchSrc = ab.getNumChannels();
        const double ratio = clip.audioSourceRate / deviceRate;
        // Audio plays at natural speed; only its start/end anchors follow the tempo map.
        const juce::int64 clipStart = tc.beatToSample (clip.startBeat);
        const juce::int64 clipEnd   = tc.beatToSample (clip.endBeat());
        const juce::int64 clipLen   = clipEnd - clipStart;
        // Shaped fade edges (tempo-aware lengths). Silent at the very start/end.
        const juce::int64 fadeInS  = clip.fadeInBeats  > 0.0 ? tc.beatToSample (clip.startBeat + clip.fadeInBeats) - clipStart : 0;
        const juce::int64 fadeOutS = clip.fadeOutBeats > 0.0 ? clipEnd - tc.beatToSample (clip.endBeat() - clip.fadeOutBeats) : 0;

        for (int i = 0; i < chunk; ++i)
        {
            const juce::int64 songPos = songStart + i;
            if (songPos < clipStart || songPos >= clipEnd) continue;
            const double readPos = (double) (songPos - clipStart) * ratio;
            if (readPos >= frames - 1) continue;

            const juce::int64 pos = songPos - clipStart;
            float fade = 1.0f;
            if (fadeInS  > 0 && pos < fadeInS)              fade  = fadeShapeGain (clip.fadeShape, (float) pos / (float) fadeInS);
            if (fadeOutS > 0 && pos > clipLen - fadeOutS)   fade *= fadeShapeGain (clip.fadeShape, (float) (clipLen - pos) / (float) fadeOutS);
            const float g = clip.audioGain * fade;

            const int r0 = (int) readPos;
            const float fr = (float) (readPos - r0);
            const float l = ab.getSample (0, r0) * (1.0f - fr) + ab.getSample (0, r0 + 1) * fr;
            const float r = nchSrc > 1
                ? ab.getSample (1, r0) * (1.0f - fr) + ab.getSample (1, r0 + 1) * fr : l;
            buffer.addSample (0, tsOffset + i, l * g);
            buffer.addSample (1, tsOffset + i, r * g);
        }
    };

    bool anySolo = false;
    for (auto& t : tracks) if (t->solo.load()) { anySolo = true; break; }

    // Session-view launch clock: a monotonic beat (advanced only while playing) that drives
    // launch quantization and clip loop phase, independent of the loopable arrangement playhead.
    // Session clips play at the current tempo (constant-path TempoConv), not the tempo map — v1.
    TempoConv sessionTc; sessionTc.setMarkers (nullptr, nullptr, 0, currentSampleRate, spb);
    const double blockBeats            = spb > 0.0 ? (double) num / spb : 0.0;
    const double blockStartSessionBeat = sessionBeat;
    if (playing)
    {
        sessionLauncher.advance (sessionBeat, sessionBeat + blockBeats);   // apply pending at boundaries
        sessionBeat += blockBeats;

        // Arrangement capture: log each session clip's played span onto the timeline.
        if (sessionCapture.load())
        {
            const double arrBeat = tc.sampleToBeat (blockStartPlayhead);
            const int nt = (int) tracks.size();
            if ((int) capturePrevSlot.size() != nt) { capturePrevSlot.assign ((size_t) nt, -1); captureStartBeat.assign ((size_t) nt, arrBeat); }
            for (int ti = 0; ti < nt; ++ti)
            {
                const int ns = sessionLauncher.playingSlot (ti);
                if (ns != capturePrevSlot[(size_t) ti])
                {
                    if (capturePrevSlot[(size_t) ti] >= 0)   // a clip just stopped -> record the span it played
                    {
                        const int idx = captureWrite.fetch_add (1, std::memory_order_relaxed);
                        if (idx < (int) captureSegs.size())
                            captureSegs[(size_t) idx] = { ti, capturePrevSlot[(size_t) ti], captureStartBeat[(size_t) ti], arrBeat };
                    }
                    captureStartBeat[(size_t) ti] = arrBeat;
                    capturePrevSlot[(size_t) ti]  = ns;
                }
            }
            captureLastBeat = arrBeat + blockBeats;   // trailing spans (still playing at stop) end here
        }
    }

    const int numTracks = (int) mixerTracks.size();
    if (mixBuffer.getNumSamples() < num) mixBuffer.setSize (2, num, false, false, true);
    for (auto& mt : mixerTracks)
    {
        if (mt->buffer.getNumSamples() < num) mt->buffer.setSize (2, num, false, false, true);
        mt->buffer.clear();
    }

    // --- each track -> its mixer insert ---
    for (int ti = 0; ti < (int) tracks.size(); ++ti)
    {
        auto& t = tracks[(size_t) ti];
        mixBuffer.clear();

        if (t->generator != nullptr)   // instrument track
        {
            juce::MidiBuffer midi;
            // Session view (per-track override): if this track has a launched session clip it plays
            // that (looped from launchBeat) INSTEAD of its arrangement clips; -1 = play arrangement.
            const int  sessionSlot = sessionLauncher.playingSlot (ti);
            const auto& chg        = sessionLauncher.changedTracks();
            const bool  transitioned = std::find (chg.begin(), chg.end(), ti) != chg.end();
            if (playing)
            {
                // On a launch/stop this block, clear the outgoing content's tail. Block-granular
                // for now; the sample-accurate split is the next slice (docs/session-view.md).
                if (transitioned) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);

                if (sessionSlot >= 0)   // session clip overrides the arrangement on this track
                {
                    if (auto clip = slotClip (t->sessionSlots, sessionSlot))
                    {
                        const double loopLen = clip->looped ? clip->contentLenBeats : clip->lengthBeats;
                        collectSessionClip (clip->notes, midi, sessionTc, spb,
                                            sessionLauncher.launchBeat (ti), blockStartSessionBeat,
                                            0, num, loopLen, clip->transpose, clip->velocityScale);
                    }
                }
                else                    // arrangement playback (the original path)
                for (int s = 0; s < nseg; ++s)
                {
                    for (auto& c : t->clips)
                        collectClip (midi, c, segs[(size_t) s].loopStart,
                                     segs[(size_t) s].chunk, segs[(size_t) s].tsOffset, t->arp.enabled);
                    if (segs[(size_t) s].wrap)
                        midi.addEvent (juce::MidiMessage::allNotesOff (1),
                                       segs[(size_t) s].tsOffset + segs[(size_t) s].chunk);
                }
            }
            // Merge live OSC/keyboard notes (played whether or not the transport is running).
            // With the arp on, run them through the live arpeggiator so a held chord plays as a
            // stepped pattern; otherwise pass them straight to the generator. Recording below
            // still captures the raw `live` input (non-destructive: the clip arp re-applies on
            // playback), matching how the clip arp works.
            juce::MidiBuffer live;
            t->liveMidi.removeNextBlockOfMessages (live, num);
            if (t->arp.enabled)
                t->liveArp.process (live, midi, num, spb, t->arp.rate, t->arp.octaves,
                                    t->arp.gate, t->arp.mode, t->arp.hold);
            else
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

        const float pol = t->polarity.load() ? -1.0f : 1.0f;   // phase invert
        const float v = t->volume.load() * pol;
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
    // VCA solo: soloing a control group makes only its members audible (like soloing them).
    bool anyGroupSolo = false;
    for (auto& g : controlGroups) if (g->solo.load()) { anyGroupSolo = true; break; }

    // Transitive insert-solo path: an insert is "solo-lit" if any node in its output chain is
    // soloed (it feeds a soloed bus) OR it is downstream of a soloed node (a soloed insert's
    // output chain passes through it). So soloing a group lights its members, and soloing a member
    // keeps its group passing. (Grow-only scratch; no per-block allocation once sized.)
    if ((int) soloImplied.size() < numTracks) soloImplied.assign ((size_t) numTracks, 0);
    else std::fill (soloImplied.begin(), soloImplied.begin() + numTracks, (char) 0);
    if (anyTrackSolo)
    {
        auto outOf = [&] (int i) { const int o = mixerTracks[(size_t) i]->output.load(); return (o > 0 && o < numTracks && o != i) ? o : -1; };
        for (int i = 1; i < numTracks; ++i)                                  // (A) i feeds / is a soloed node
            for (int j = i; j >= 1; j = outOf (j))
                if (mixerTracks[(size_t) j]->solo.load()) { soloImplied[(size_t) i] = 1; break; }
        for (int x = 1; x < numTracks; ++x)                                  // (B) i is downstream of a soloed node
            if (mixerTracks[(size_t) x]->solo.load())
                for (int j = outOf (x); j >= 1; j = outOf (j)) soloImplied[(size_t) j] = 1;
    }

    MixerTrack& master = *mixerTracks[0];
    const double fxBpm = juce::jmax (1.0, transport.getBpm());   // tempo for tempo-synced effects this block
    for (int ti = 1; ti < numTracks; ++ti)
    {
        MixerTrack& mt = *mixerTracks[(size_t) ti];
        { auto sub = subView (mt.buffer); for (auto& fx : mt.effects) { fx->setTempo (fxBpm); fx->process (sub); } }
        const float rawL = mt.buffer.getMagnitude (0, 0, num), rawR = mt.buffer.getMagnitude (1, 0, num);

        // Compute the fader gain + audibility first — post-fader sends AND the meter need them.
        // Solo: audible if nothing is soloed, or this insert is on the transitive solo path
        // (soloImplied — direct/bus solo, incl. soloing a group lighting its members), or it
        // belongs to a soloed control group (VCA solo). Track-solo and group-solo combine.
        bool soloed = soloImplied[(size_t) ti] != 0;
        if (! soloed && anyGroupSolo && mt.group.isNotEmpty())
            if (auto* grp = findControlGroup (mt.group)) soloed = grp->solo.load();
        bool audible = ! mt.mute.load() && ((! anyTrackSolo && ! anyGroupSolo) || soloed);
        float v = mt.volume.load();
        // VCA-lite: an insert's control group scales its fader; a muted group silences it.
        if (mt.group.isNotEmpty())
            if (auto* grp = findControlGroup (mt.group))
            {
                if (grp->mute.load()) audible = false;
                else                  v *= grp->gain.load();
            }

        // Constant-power pan law (also applied to the output below): fold it into the meter so
        // each channel reflects the balance, not just the fader.
        const float pan   = mt.pan.load();
        const float theta = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        const float panL  = std::cos (theta), panR = std::sin (theta);

        // Meter POST-fader AND POST-pan (like the master): pulling the fader to 0, muting, or
        // panning hard drops the corresponding channel's meter — what you hear is what you see.
        const float mpL = audible ? rawL * v * panL : 0.0f, mpR = audible ? rawR * v * panR : 0.0f;
        mt.peakL.store (mpL); mt.peakR.store (mpR);
        if (mpL >= 1.0f || mpR >= 1.0f) mt.clipped.store (true);

        // Aux sends: tap this insert's post-effects signal into its target buses. A PRE-fader
        // send taps at its own level regardless of the fader/mute (a classic aux); a POST-fader
        // send follows the fader gain and is silenced when the channel is muted/soloed out.
        for (auto& sd : mt.sends)
            if (sd.level > 0.0f && sd.bus > 0 && sd.bus < numTracks && sd.bus != ti)
            {
                const float g = sd.postFader ? (audible ? v * sd.level : 0.0f) : sd.level;
                if (g <= 0.0f) continue;
                auto& bus = mixerTracks[(size_t) sd.bus]->buffer;
                bus.addFrom (0, 0, mt.buffer, 0, 0, num, g);
                bus.addFrom (1, 0, mt.buffer, 1, 0, num, g);
            }

        if (! audible) continue;
        // Main output → master (0) by default, or into a group/bus insert. The target must be
        // processed later in this loop (higher index) so it accumulates before it's summed; else
        // fall back to master rather than lose/delay the signal.
        const int out = mt.output.load();
        auto& dest = (out > ti && out < numTracks) ? mixerTracks[(size_t) out]->buffer : master.buffer;
        dest.addFrom (0, 0, mt.buffer, 0, 0, num, v * panL);
        dest.addFrom (1, 0, mt.buffer, 1, 0, num, v * panR);
    }

    // --- master -> output ---
    { auto sub = subView (master.buffer); for (auto& fx : master.effects) { fx->setTempo (fxBpm); fx->process (sub); } }
    const float mv = master.volume.load();
    // Meter the master POST-fader so it reflects the master level control (what you hear).
    const float mpL = master.buffer.getMagnitude (0, 0, num) * mv, mpR = master.buffer.getMagnitude (1, 0, num) * mv;
    master.peakL.store (mpL); master.peakR.store (mpR);
    if (mpL >= 1.0f || mpR >= 1.0f) master.clipped.store (true);
    if (out->getNumChannels() > 0) out->addFrom (0, start, master.buffer, 0, 0, num, mv);
    if (out->getNumChannels() > 1) out->addFrom (1, start, master.buffer, 1, 0, num, mv);

    // --- metronome: a short click at each beat, accented on bar downbeats. A monitor
    // layer added on top of the master (allocation-free; state persists across blocks so
    // a click can span a block boundary). Sounds whenever enabled + playing (so an offline
    // bounce with it on captures it — disable before a clean bounce).
    if (metronomeEnabled.load() && playing && out->getNumChannels() > 0)
    {
        const double rate = juce::jmax (1.0, currentSampleRate);
        const int    clickLen = (int) (0.03 * rate);   // 30 ms
        const int    bpb = juce::jmax (1, (int) std::llround (transport.beatsPerBar()));
        int k = (int) std::ceil (tc.sampleToBeat (blockStartPlayhead) - 1.0e-6);
        if (k < 0) k = 0;
        juce::int64 nextBeat = tc.beatToSample (k);   // re-evaluated once per beat, not per sample
        for (int i = 0; i < num; ++i)
        {
            if (blockStartPlayhead + i >= nextBeat)
            {
                const bool accent = (k % bpb) == 0;
                metroSamplesLeft = clickLen; metroPhase = 0.0;
                metroInc = 2.0 * juce::MathConstants<double>::pi * (accent ? 1600.0 : 1000.0) / rate;
                metroAmp = (accent ? 0.6f : 0.4f) * metronomeLevel.load();
                nextBeat = tc.beatToSample (++k);
            }
            if (metroSamplesLeft > 0)
            {
                const float s = std::sin ((float) metroPhase) * ((float) metroSamplesLeft / (float) clickLen) * metroAmp;
                out->addSample (0, start + i, s);
                if (out->getNumChannels() > 1) out->addSample (1, start + i, s);
                metroPhase += metroInc;
                --metroSamplesLeft;
            }
        }
    }

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
    // No fixed insert pool: each track creates its own insert in addTrack (Master + per-track
    // inserts + buses). This is what keeps the mixer showing exactly one strip per track.
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

// Tab toggles the two main views — Arrange <-> Session — like Ableton. The Mixer is opened via
// its toolbar button (a full console), not a Tab stop; effects are edited in the bottom Device
// panel. From the Mixer, Tab returns to the Arrangement.
void MainComponent::cycleView()
{
    // Tab rotates through all three main views: Arrange -> Session -> Mixer -> Arrange.
    setViewMode (viewMode == ViewMode::Arrange ? ViewMode::Session
               : viewMode == ViewMode::Session ? ViewMode::Mixer
                                               : ViewMode::Arrange);
}

void MainComponent::setViewMode (ViewMode m) { viewMode = m; applyViewMode(); }

// Listen for keys on the top-level window so Tab switches views even when nothing has focus.
void MainComponent::parentHierarchyChanged()
{
    auto* top = getTopLevelComponent();
    if (top == keyListenerHost.getComponent()) return;
    if (auto* old = keyListenerHost.getComponent()) old->removeKeyListener (this);
    keyListenerHost = top;
    if (top != nullptr) top->addKeyListener (this);
}

void MainComponent::applyViewMode()
{
    arrangeViewport.setVisible (viewMode == ViewMode::Arrange);
    if (sessionPane) sessionPane->setVisible (viewMode == ViewMode::Session);
    mixerViewport  .setVisible (viewMode == ViewMode::Mixer);
    if (viewMode == ViewMode::Session && sessionPane) sessionPane->rebuild();
    if (viewMode == ViewMode::Mixer   && mixerView)   mixerView->rebuild();
    resized();
    grabKeyboardFocus();   // keep Tab reaching keyPressed regardless of what had focus
}

void MainComponent::openMappings()
{
    // Build the row list from the live mapping state (controller maps + LFO routes).
    std::vector<MappingsView::Row> rows;
    for (const auto& c : apiListControllerMaps())
    {
        juce::String t = c.source + "  ->  " + c.target + "   [" + juce::String (c.lo, 2) + ".." + juce::String (c.hi, 2) + "]";
        if (c.bypass) t += "  (bypassed)";
        rows.push_back ({ t, "ctrl", c.source });
    }
    static const char* shapeName[] = { "sine", "tri", "saw", "square", "random" };
    for (const auto& m : apiListModulations())
    {
        const juce::String sh = juce::isPositiveAndBelow (m.shape, 5) ? shapeName[m.shape] : "sine";
        juce::String t = "LFO (" + sh + (m.syncBeats > 0.0f ? ", " + juce::String (m.syncBeats, 2) + "bt" : ", " + juce::String (m.rate, 2) + "Hz")
                       + ", depth " + juce::String (m.depth, 2) + ")  ->  " + m.target;
        rows.push_back ({ t, "mod", m.target });
    }
    mappingsView.setRows (std::move (rows));

    if (mappingsWindow == nullptr)
    {
        auto w = std::make_unique<HideOnCloseWindow>();
        w->setName ("Mappings");
        w->setContentNonOwned (&mappingsView, false);
        w->setResizable (true, false);
        w->setSize (560, 420);
        w->centreWithSize (560, 420);
        mappingsWindow = std::move (w);
    }
    mappingsWindow->setVisible (true);
    mappingsWindow->toFront (true);
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

void MainComponent::openSourceControl()
{
    // Seed of the full Source Control panel — read-only git status for now
    // (branches / commits / diff / commit land in later Wave-9 slices). The git
    // logic + report text live in Git.cpp; this just hosts it in a window.
    if (sourceControlWindow == nullptr)
    {
        sourceControlEditor.setMultiLine (true);
        sourceControlEditor.setReadOnly (true);
        sourceControlEditor.setScrollbarsShown (true);
        sourceControlEditor.setFont (juce::Font (juce::FontOptions (
            juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));

        auto w = std::make_unique<HideOnCloseWindow>();
        w->setName ("Source Control");
        w->setContentNonOwned (&sourceControlEditor, false);
        w->setResizable (true, false);
        w->setSize (560, 420);
        w->centreWithSize (560, 420);
        sourceControlWindow = std::move (w);
    }
    sourceControlEditor.setText (gitStatusReport(), juce::dontSendNotification);
    sourceControlWindow->setVisible (true);
    sourceControlWindow->toFront (true);
}

void MainComponent::openHistory()
{
    // The commit-history window — the seed of the full commit-graph panel (the DAG lines
    // + per-commit diff land in later slices). Read-only; refreshed each time it opens.
    if (historyWindow == nullptr)
    {
        historyEditor.setMultiLine (true);
        historyEditor.setReadOnly (true);
        historyEditor.setScrollbarsShown (true);
        historyEditor.setFont (juce::Font (juce::FontOptions (
            juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));

        auto w = std::make_unique<HideOnCloseWindow>();
        w->setName ("History");
        w->setContentNonOwned (&historyEditor, false);
        w->setResizable (true, false);
        w->setSize (640, 480);
        w->centreWithSize (640, 480);
        historyWindow = std::move (w);
    }
    historyEditor.setText (gitHistoryReport(), juce::dontSendNotification);
    historyWindow->setVisible (true);
    historyWindow->toFront (true);
}

void MainComponent::showBranchMenu()
{
    // Branches = alternate arrangements. Checkout / merge change files on disk, so we
    // reload the project after them; both are guarded against an uncommitted working tree
    // (stash-choice arrives in a later slice — for now: commit first).
    if (currentProjectFile.getFileName() != "gloopy.toml") return;
    const auto dir = currentProjectFile.getParentDirectory().getFullPathName();
    if (! apiGitStatus (dir).isRepo)
    {
        juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
            "Branches", "This folder isn't a git repository yet — use File \xe2\x86\x92 Enable Git first.");
        return;
    }

    auto b = apiGitBranches (dir);
    const auto current = b.current;
    auto others = b.branches;
    others.removeString (current);

    juce::PopupMenu m;
    m.addItem (-1, "On branch: " + (current.isNotEmpty() ? current : juce::String ("(detached)")), false);
    m.addSeparator();
    for (int i = 0; i < b.branches.size(); ++i)                       // click a branch to check it out
        m.addItem (1000 + i, b.branches[i], true, b.branches[i] == current);
    m.addSeparator();
    m.addItem (1, "New branch...");
    if (others.size() > 0)
    {
        juce::PopupMenu mergeM, delM;
        for (int i = 0; i < others.size(); ++i) mergeM.addItem (2000 + i, others[i]);
        for (int i = 0; i < others.size(); ++i) delM.addItem (3000 + i, others[i]);
        m.addSubMenu ("Merge into " + current, mergeM);
        m.addSubMenu ("Delete branch", delM);
    }
    if (current.isNotEmpty()) m.addItem (2, "Rename current branch...");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fileButton),
        [this, dir, allBranches = b.branches, others] (int r)
        {
            if (r == 0) return;
            auto dirty = [this, dir] { return ! apiGitStatus (dir).changes.empty(); };
            auto reloadProject = [this] { openAny (currentProjectFile);
                                          if (sourceControlWindow != nullptr && sourceControlWindow->isVisible()) openSourceControl(); };
            auto ok = [] (const juce::String& title, const GitResult& res)
            {
                if (! res.ok) juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, res.error);
                return res.ok;
            };

            if (r >= 1000 && r < 2000)          // checkout a branch (reloads the project)
            {
                const auto name = allBranches[r - 1000];
                if (name == apiGitBranches (dir).current) return;
                if (dirty()) { juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                    "Checkout", "Commit your changes before switching branches."); return; }
                if (ok ("Checkout", apiGitCheckout (dir, name))) reloadProject();
            }
            else if (r >= 2000 && r < 3000)     // merge a branch into the current one (reloads)
            {
                const auto name = others[r - 2000];
                if (dirty()) { juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                    "Merge", "Commit your changes before merging."); return; }
                if (ok ("Merge", apiGitMerge (dir, name))) reloadProject();
            }
            else if (r >= 3000)                 // delete a branch (safe -d; git refuses if unmerged)
            {
                ok ("Delete branch", apiGitBranchDelete (dir, others[r - 3000], false));
            }
            else if (r == 1)                    // new branch from the current commit, then switch to it
            {
                auto* aw = new juce::AlertWindow ("New branch",
                    "Name the new branch (created from the current commit).", juce::MessageBoxIconType::NoIcon);
                aw->addTextEditor ("name", "", "Branch name");
                aw->addButton ("Create + switch", 1, juce::KeyPress (juce::KeyPress::returnKey));
                aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, dir] (int rr)
                {
                    if (rr == 1)
                    {
                        const auto name = aw->getTextEditorContents ("name").trim();
                        if (name.isNotEmpty())
                        {
                            auto res = apiGitBranchCreate (dir, name, {});
                            if (res.ok) res = apiGitCheckout (dir, name);   // same commit -> no reload needed
                            if (! res.ok) juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "New branch", res.error);
                            else if (sourceControlWindow != nullptr && sourceControlWindow->isVisible()) openSourceControl();
                        }
                    }
                    delete aw;
                }), false);
            }
            else if (r == 2)                    // rename the current branch
            {
                const auto cur = apiGitBranches (dir).current;
                auto* aw = new juce::AlertWindow ("Rename branch", "Rename '" + cur + "' to:", juce::MessageBoxIconType::NoIcon);
                aw->addTextEditor ("name", cur, "New name");
                aw->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
                aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, dir, cur] (int rr)
                {
                    if (rr == 1)
                    {
                        const auto nn = aw->getTextEditorContents ("name").trim();
                        if (nn.isNotEmpty() && nn != cur)
                            if (! apiGitBranchRename (dir, cur, nn).ok)
                                juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Rename branch",
                                    "Could not rename the branch.");
                    }
                    delete aw;
                }), false);
            }
        });
}

void MainComponent::showCommitDialog()
{
    // The IDE commit surface: save the current edits, list what changed, and let the
    // user write a message. Commit stages all + commits; Amend rewrites the last commit.
    if (currentProjectFile.getFileName() != "gloopy.toml") return;
    const auto dir = currentProjectFile.getParentDirectory().getFullPathName();

    if (! apiGitStatus (dir).isRepo)
    {
        juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
            "Commit", "This folder isn't a git repository yet — use File \xe2\x86\x92 Enable Git first.");
        return;
    }
    saveComposition (currentProjectFile.getParentDirectory());   // capture the current edits
    auto s = apiGitStatus (dir);
    if (s.changes.empty())
    {
        juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
            "Commit", "Nothing to commit \xe2\x80\x94 the working tree is clean.");
        return;
    }

    juce::String fileList;
    for (auto& c : s.changes) fileList << "  " << c.xy << "  " << c.path << "\n";

    auto* aw = new juce::AlertWindow ("Commit",
        juce::String (s.changes.size()) + " changed file(s):\n" + fileList,
        juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("msg", "", "Commit message:");
    aw->addButton ("Commit", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Amend",  2);
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, dir] (int r)
    {
        if (r == 1 || r == 2)
        {
            const auto msg = aw->getTextEditorContents ("msg");
            auto add = apiGitAdd (dir, {});
            GitResult res = add.ok ? apiGitCommit (dir, msg, r == 2) : add;
            if (! res.ok)
                juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    "Commit", res.error);
            else if (sourceControlWindow != nullptr && sourceControlWindow->isVisible())
                openSourceControl();   // refresh the status readout behind the dialog
        }
        delete aw;
    }), false);
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

int MainComponent::indexOfMixerTrack (const MixerTrack* mt) const
{
    const juce::ScopedLock sl (engineLock);
    for (int i = 0; i < (int) mixerTracks.size(); ++i)
        if (mixerTracks[(size_t) i].get() == mt) return i;
    return -1;
}

void MainComponent::pruneDeviceWindows()
{
    for (int i = deviceWindows.size() - 1; i >= 0; --i)
    {
        auto* w = deviceWindows[i];
        auto* mt = reinterpret_cast<const MixerTrack*> ((juce::pointer_sized_int) (juce::int64) w->getProperties()["mt"]);
        if (indexOfMixerTrack (mt) < 0) deviceWindows.remove (i);   // its insert was deleted
    }
}

// Pop a floating device-chain window pinned to one insert (Reaper/Ardour style). Bound to the
// MixerTrack pointer so it survives insert reindexing; pruneDeviceWindows() closes it if that
// insert is later removed.
void MainComponent::openDeviceWindow (int insert)
{
    MixerTrack* target = nullptr;
    { const juce::ScopedLock sl (engineLock);
      if (juce::isPositiveAndBelow (insert, (int) mixerTracks.size())) target = mixerTracks[(size_t) insert].get(); }
    if (target == nullptr) return;

    const juce::int64 key = (juce::int64) (juce::pointer_sized_int) target;
    for (auto* w : deviceWindows)                                   // already open? just raise it
        if (w != nullptr && (juce::int64) w->getProperties()["mt"] == key) { w->toFront (true); return; }

    struct DeviceWindow : juce::DocumentWindow
    {
        DeviceWindow (const juce::String& n) : DocumentWindow (n, Palette::bg, DocumentWindow::closeButton)
        { setUsingNativeTitleBar (true); }
        void closeButtonPressed() override { if (onClose) onClose(); }
        std::function<void()> onClose;
    };

    auto* panel = new DevicePanel();
    panel->setStandalone (true);
    panel->getEffectTypes = devicePanel.getEffectTypes;            // same built-in effect list
    panel->getTitle = [this, target]
    {
        const juce::ScopedLock sl (engineLock);
        const int i = indexOfMixerTrack (target);
        if (i < 0) return juce::String ("DEVICES");
        juce::String nm = mixerTracks[(size_t) i]->name;
        for (auto& t : tracks) if (t->mixerTrack.load() == i) { nm = t->name; break; }   // prefer the track name (matches the strip)
        const juce::String bullet (juce::CharPointer_UTF8 ("\xe2\x80\xa2"));
        return "DEVICES   " + bullet + "   " + nm.toUpperCase();
    };
    panel->getChain = [this, target]
    {
        std::vector<std::pair<juce::String, bool>> out;
        const juce::ScopedLock sl (engineLock);
        const int i = indexOfMixerTrack (target);
        if (i >= 0) for (auto& fx : mixerTracks[(size_t) i]->effects) out.push_back ({ fx->name(), fx->bypassed.load() });
        return out;
    };
    panel->getParams = [this, target] (int slot)
    {
        std::vector<DevicePanel::Param> out;
        const int i = indexOfMixerTrack (target);
        if (i >= 0) for (auto& p : apiGetEffectParams (i, slot)) out.push_back ({ p.name, p.value, p.min, p.max });
        return out;
    };
    panel->onAddEffect    = [this, target, panel] (int type)                 { const int i = indexOfMixerTrack (target); if (i >= 0) { apiAddEffect (i, type); panel->refresh(); } };
    panel->onRemoveEffect = [this, target, panel] (int slot)                 { const int i = indexOfMixerTrack (target); if (i >= 0) { apiRemoveEffect (i, slot); panel->refresh(); } };
    panel->onSetBypass    = [this, target] (int slot, bool b)                { const int i = indexOfMixerTrack (target); if (i >= 0) apiSetEffectBypass (i, slot, b); };
    panel->onSetParam     = [this, target] (int slot, const juce::String& n, float v) { const int i = indexOfMixerTrack (target); if (i >= 0) apiSetEffectParam (i, slot, n, v); };

    juce::String name;
    { const juce::ScopedLock sl (engineLock);
      name = target->name;
      for (auto& t : tracks) if (t->mixerTrack.load() == insert) { name = t->name; break; } }   // track name for the title bar
    auto* w = new DeviceWindow ("Devices \xe2\x80\x94 " + name);
    w->getProperties().set ("mt", key);
    w->setContentOwned (panel, true);
    w->setResizable (true, false);
    w->setContentComponentSize (540, 300);
    // Cascade so multiple windows don't stack exactly.
    w->setTopLeftPosition (120 + deviceWindows.size() * 28, 120 + deviceWindows.size() * 28);
    w->onClose = [this, w] { juce::MessageManager::callAsync ([this, w] { deviceWindows.removeObject (w); }); };
    w->setVisible (true);
    deviceWindows.add (w);
    panel->refresh();
}

// ---------------------------------------------------------------------------
// GUI
// ---------------------------------------------------------------------------
void MainComponent::timerCallback()
{
    if (audioRecActive.load() && loopRecRotate.exchange (false))
        rotateLoopTakes();   // loop recording: finalize the pass, start a new take

    if (! deviceWindows.isEmpty()) pruneDeviceWindows();   // close any whose insert was deleted

    if (renderFinished.load())
    {
        renderFinished = false;
        if (auto* app = juce::JUCEApplication::getInstance())
            app->systemRequestedQuit();
        return;
    }

    const double beats = transport.getPlayheadBeats();
    const double bpb   = juce::jmax (1.0, transport.beatsPerBar());   // time-signature aware
    const int bar  = (int) (beats / bpb) + 1;
    const int beat = (int) std::fmod (beats, bpb) + 1;
    const int tick = (int) (std::fmod (beats, 1.0) * 100.0);
    posLabel.setText (juce::String::formatted ("%d . %d . %02d", bar, beat, tick),
                      juce::dontSendNotification);

    // ~1x/sec (well below the 30 Hz tick): refresh hosted-instrument display names — e.g. the
    // current Surge XT patch, which the user changes from Surge's own browser — and repaint the
    // arrange header if any changed. Only Surge tracks pay getStateInformation (the helper bails
    // on the plugin name first), so this stays cheap for everything else.
    static int uiRefreshTick = 0;
    if (++uiRefreshTick >= 30)
    {
        uiRefreshTick = 0;
        bool changed = false;
        for (auto& t : tracks)
            if (t != nullptr && t->generator != nullptr)
            {
                const auto prev = t->generator->uiPatchName;
                t->generator->refreshUiPatchName();
                changed = changed || (t->generator->uiPatchName != prev);
            }
        if (changed && arrangeView != nullptr) arrangeView->repaint();
    }

    // Keep repainting the header briefly after live MIDI so the input LED animates/fades.
    if (arrangeView != nullptr
          && juce::Time::getMillisecondCounterHiRes() - midiActivityMs.load() < 300.0)
        arrangeView->repaint();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Palette::bg);

    g.setColour (Palette::panel);              // toolbar reads as a raised surface (shade), with only a soft seam
    g.fillRect (toolbarBounds);
    g.setColour (Palette::lineSoft);
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
        g.fillRoundedRectangle (transportBounds.toFloat(), Palette::radius);
    }

    // Tempo / position display.
    if (! displayBounds.isEmpty())
    {
        g.setColour (Palette::inset);          // the sunken fill is the display's edge; no hard outline
        g.fillRoundedRectangle (displayBounds.toFloat(), Palette::radius);
        const int half = displayBounds.getWidth() / 2;
        g.setColour (Palette::lineSoft);
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

    // Browser toggle: first control on the left, directly above where the panel
    // docks — the conventional home for a left-sidebar toggle.
    browseButton.setBounds (bar.removeFromLeft (30)); bar.removeFromLeft (12);

    fileButton.setBounds (bar.removeFromLeft (52)); bar.removeFromLeft (14);   // a little room before transport

    // Transport cluster — Play is the hero (wider), stop/record secondary.
    transportBounds = bar.removeFromLeft (128).expanded (0, 1);
    {
        auto t = transportBounds.reduced (4, 2);
        const int playW = (int) (t.getWidth() * 0.42f);
        const int bw = (t.getWidth() - playW - 8) / 2;
        playButton  .setBounds (t.removeFromLeft (playW)); t.removeFromLeft (4);
        stopButton  .setBounds (t.removeFromLeft (bw)); t.removeFromLeft (4);
        recordButton.setBounds (t.removeFromLeft (bw));
    }
    bar.removeFromLeft (14);   // a little room after transport

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
    mapsButton   .setBounds (bar.removeFromRight (52)); bar.removeFromRight (6);
    loopButton   .setBounds (bar.removeFromRight (54)); bar.removeFromRight (6);
    metroButton  .setBounds (bar.removeFromRight (58)); bar.removeFromRight (6);
    panicButton  .setBounds (bar.removeFromRight (54)); bar.removeFromRight (12);
    scaleNameBox .setBounds (bar.removeFromRight (128).reduced (0, 4)); bar.removeFromRight (4);
    scaleRootBox .setBounds (bar.removeFromRight (52).reduced (0, 4));

    // Collapsible browser docks on the left of the main content area.
    if (browser != nullptr)
    {
        browser->setVisible (browserVisible);
        if (browserVisible)
            browser->setBounds (area.removeFromLeft (210));
    }

    // Arrangement | divider | editor.
    // The top pane is the arrangement, the session grid, or the mixer, per the view mode.
    Component* topPane = (viewMode == ViewMode::Session && sessionPane) ? (Component*) sessionPane.get()
                       : viewMode == ViewMode::Mixer   ? (Component*) &mixerViewport
                                                       : (Component*) &arrangeViewport;
    // The bottom pane is the clip editor or the device chain, per bottomMode.
    const bool devices = bottomMode == BottomMode::Devices;
    editorPanel.setVisible (! devices);
    devicePanel.setVisible (devices);
    Component* bottomPane = devices ? (Component*) &devicePanel : (Component*) &editorPanel;
    Component* comps[] = { topPane, dividerBar.get(), bottomPane };
    verticalLayout.layOutComponents (comps, 3, area.getX(), area.getY(),
                                     area.getWidth(), area.getHeight(), true, true);

    busyOverlay.setBounds (getLocalBounds());   // covers everything while busy

    if (arrangeView)
        arrangeView->setSize (arrangeViewport.getMaximumVisibleWidth(),
                              juce::jmax (arrangeView->preferredHeight(), arrangeViewport.getHeight()));
    // SessionPane self-sizes its inner grid + frozen scene column in its own resized().
    if (mixerView && viewMode == ViewMode::Mixer)
        mixerView->setSize (juce::jmax (700, mixerViewport.getMaximumVisibleWidth()),
                            juce::jmax (400, mixerViewport.getHeight()));
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

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& p : files)
        if (classifyDroppedFile (juce::File (p)) != DroppedFileKind::Unsupported)
            return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    // Route each dropped file to the same op the File menu / toolbar uses. Projects
    // replace the session; MIDI/audio add tracks. Runs on the message thread (JUCE
    // delivers the drop there), so the api* calls are safe to invoke directly.
    for (const auto& p : files)
    {
        const juce::File f (p);
        switch (classifyDroppedFile (f))
        {
            case DroppedFileKind::Project: openAny (f);                        break;
            case DroppedFileKind::Midi:    apiImportMidi (f.getFullPathName()); break;
            case DroppedFileKind::Audio:   apiImportAudio (f.getFullPathName()); break;
            case DroppedFileKind::Unsupported:                                 break;
        }
    }
}

void MainComponent::showFileMenu()
{
    juce::PopupMenu menu;
    const bool haveProject = currentProjectFile != juce::File();
    const bool isComposition = currentProjectFile.getFileName() == "gloopy.toml";
    // Undo / redo — discoverable here (also bound to Ctrl+Z / Ctrl+Shift+Z). The
    // enabled state reflects the snapshot stacks so users can see when either is live.
    menu.addItem (20, "Undo   Ctrl+Z",        ! undoStack.empty());
    menu.addItem (21, "Redo   Ctrl+Shift+Z",  ! redoStack.empty());
    menu.addSeparator();
    menu.addItem (1, "New Project (empty)");
    juce::PopupMenu templatesMenu;                       // New from a built-in or user template
    const auto templates = apiListTemplates();
    for (int i = 0; i < (int) templates.size(); ++i) templatesMenu.addItem (100 + i, templates[(size_t) i]);
    menu.addSubMenu ("New from Template", templatesMenu);
    menu.addItem (2, "Open...");                         // .gloopy or .zip
    menu.addItem (6, "Open Composition Folder...");
    menu.addItem (9, "Import MIDI File...");             // .mid/.midi -> synth track + clip per track
    menu.addSeparator();
    menu.addItem (3, "Save", haveProject);
    menu.addItem (4, "Save As .gloopy...");
    menu.addItem (7, "Save As Composition...");
    menu.addItem (10, "Save as Template...");
    menu.addItem (13, "Export MIDI File...");          // whole project -> .mid (loops tiled)
    menu.addItem (14, "Export Audio (WAV)...");        // whole mix -> offline WAV bounce
    menu.addItem (15, "Export Loop Region (WAV)...", transport.isLoopEnabled());   // just the loop selection
    menu.addItem (16, "Export Stems (WAV)...");        // one WAV per instrument track -> a folder
    menu.addSeparator();
    menu.addItem (11, "Load Tuning (.scl)...");        // Scala microtuning file
    bool tuned = false; for (double c : projectTuning) if (c != 0.0) tuned = true;
    menu.addItem (12, "Reset Tuning (Equal)", tuned);
    menu.addSeparator();
    menu.addItem (8, "Project Notes...");
    menu.addItem (17, "Source Control...");            // git status of the project's dir (Git.cpp)
    menu.addItem (18, "New Git Project...");           // save as a composition folder + git init
    menu.addItem (19, "Enable Git", isComposition);    // git init the open composition folder
    menu.addItem (22, "Commit...", isComposition);     // save + stage all + commit (Git.cpp)
    menu.addItem (23, "History...", isComposition);    // git commit log (Git.cpp)
    menu.addItem (24, "Branches...", isComposition);   // branch popup (Git.cpp)
    // Live MIDI status (read-only): the input sources Gloopy hears + which track they play.
    juce::PopupMenu midiMenu;
    const auto midiIns = apiListMidiInputs();
    if (midiIns.empty())
        midiMenu.addItem (899, "(no MIDI inputs detected)", false);
    else
        for (const auto& n : midiIns) midiMenu.addItem (899, n, false);
    midiMenu.addSeparator();
    int midiTgt = midiInputTarget.load();
    if (midiTgt < 0) midiTgt = firstInstrumentId.load();
    Track* midiTrack = resolveTrack (midiTgt);
    midiMenu.addItem (899, "Live notes play: " + (midiTrack != nullptr ? midiTrack->name
                                                                        : juce::String ("(select an instrument track)")), false);
    menu.addSubMenu ("MIDI Inputs", midiMenu);
    menu.addItem (5, "Rescan Plugins");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fileButton),
        [this, isComposition] (int result)
        {
            if (result == 8) { openNotes(); return; }
            if (result == 17) { openSourceControl(); return; }
            if (result == 18)   // New Git Project: choose a folder, save the composition into it, git init
            {
                fileChooser = std::make_unique<juce::FileChooser> ("New git project folder", juce::File());
                fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectDirectories,
                    [this] (const juce::FileChooser& fc)
                    {
                        auto d = fc.getResult();
                        if (d == juce::File()) return;
                        const auto dir = d.getFullPathName();
                        auto err = std::make_shared<juce::String>();
                        runBackground ("Creating git project…",
                            [this, dir, err]
                            {
                                if (! apiSaveComposition (dir)) { *err = "Could not save the composition folder."; return; }
                                auto r = apiGitInit (dir);
                                if (! r.ok) *err = r.error;
                            },
                            [this, d, err]
                            {
                                if (err->isNotEmpty())
                                {
                                    juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                        "New Git Project", *err);
                                    return;
                                }
                                currentProjectFile = d.getChildFile ("gloopy.toml");   // the project now lives here
                                openSourceControl();                                    // show the fresh repo
                            });
                    });
                return;
            }
            if (result == 19)   // Enable Git: git init the open composition folder
            {
                if (currentProjectFile.getFileName() != "gloopy.toml") return;
                auto r = apiGitInit (currentProjectFile.getParentDirectory().getFullPathName());
                if (! r.ok)
                    juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                        "Enable Git", r.error);
                else
                    openSourceControl();
                return;
            }
            if (result == 22) { showCommitDialog(); return; }   // save + stage all + commit
            if (result == 23) { openHistory(); return; }        // git commit log
            if (result == 24) { showBranchMenu(); return; }     // branch popup
            if (result == 20) { undo(); return; }
            if (result == 21) { redo(); return; }
            if (result >= 100)                                  // New from Template
            {
                const auto templates = apiListTemplates();
                if (juce::isPositiveAndBelow (result - 100, (int) templates.size()))
                {
                    const auto name = templates[(size_t) (result - 100)];
                    // A template may load a big sampled instrument (piano SFZ) — show the
                    // busy overlay while it builds so the UI doesn't look frozen.
                    busyOverlay.show ("Loading " + name + "…");
                    juce::MessageManager::callAsync ([this, name]
                        { apiNewFromTemplate (name); busyOverlay.hide(); });
                }
                return;
            }
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
            else if (result == 9)   // Import a standard MIDI file into the current project
            {
                fileChooser = std::make_unique<juce::FileChooser> ("Import MIDI file", juce::File(), "*.mid;*.midi");
                fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [this] (const juce::FileChooser& fc)
                    {
                        const auto f = fc.getResult();
                        if (! f.existsAsFile()) return;
                        // apiImportMidi adds a synth track + clip per MIDI track and refreshes
                        // the arrange view itself (same path as the ImportMidi RPC).
                        if (apiImportMidi (f.getFullPathName()) <= 0)
                            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                "Import MIDI", "No importable note tracks were found in\n" + f.getFileName());
                    });
            }
            else if (result == 13)   // Export the whole project to a standard MIDI file
            {
                fileChooser = std::make_unique<juce::FileChooser> ("Export MIDI file", juce::File(), "*.mid");
                fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                    [this] (const juce::FileChooser& fc)
                    {
                        auto f = fc.getResult();
                        if (f == juce::File()) return;
                        if (f.getFileExtension().isEmpty()) f = f.withFileExtension ("mid");
                        if (! apiExportMidi (f.getFullPathName()))
                            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                "Export MIDI", "Could not write\n" + f.getFileName());
                    });
            }
            else if (result == 14 || result == 15)   // Export audio: whole mix (14) or the loop region (15)
            {
                const bool loopOnly = (result == 15);
                fileChooser = std::make_unique<juce::FileChooser> (loopOnly ? "Export loop region" : "Export audio",
                                                                   juce::File(), "*.wav;*.flac");
                fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                            | juce::FileBrowserComponent::warnAboutOverwriting,
                    [this, loopOnly] (const juce::FileChooser& fc)
                    {
                        auto f = fc.getResult();
                        if (f == juce::File()) return;
                        if (f.getFileExtension().isEmpty()) f = f.withFileExtension ("wav");
                        const auto path = f.getFullPathName();
                        // A long bounce shouldn't freeze the UI: render on the pool thread
                        // (apiRenderToFile holds the engine lock and is thread-safe off-message).
                        auto ok = std::make_shared<bool> (false);
                        runBackground (loopOnly ? "Exporting loop region…" : "Exporting audio…",
                            [this, path, loopOnly, ok] { *ok = loopOnly ? apiExportLoopRegion (path)
                                                                        : apiRenderToFile (path, 2.0); },
                            [this, f, ok]
                            {
                                if (! *ok)
                                    juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                        "Export Audio", "Could not write\n" + f.getFileName());
                            });
                    });
            }
            else if (result == 16)   // Export one WAV per instrument track into a chosen folder
            {
                fileChooser = std::make_unique<juce::FileChooser> ("Export stems to folder", juce::File());
                fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectDirectories,
                    [this] (const juce::FileChooser& fc)
                    {
                        auto d = fc.getResult();
                        if (d == juce::File()) return;
                        const auto dir = d.getFullPathName();
                        auto count = std::make_shared<int> (0);
                        runBackground ("Exporting stems…",
                            [this, dir, count] { *count = (int) apiExportStems (dir).size(); },
                            [this, count]
                            {
                                juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                    "Export Stems", juce::String (*count) + (*count == 1 ? " stem written." : " stems written."));
                            });
                    });
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
            else if (result == 10)   // Save the current project as a reusable user template
            {
                auto* aw = new juce::AlertWindow ("Save as Template",
                    "Name this template — it will appear under \"New from Template\".",
                    juce::MessageBoxIconType::NoIcon);
                aw->addTextEditor ("name", "My Template", "Template name");
                aw->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
                aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int r)
                {
                    if (r == 1)
                    {
                        const auto name = aw->getTextEditorContents ("name").trim();
                        if (name.isNotEmpty() && ! apiSaveAsTemplate (name))
                            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                "Save as Template", "Could not save the template \"" + name + "\".");
                    }
                    delete aw;
                }), false);
            }
            else if (result == 11)   // Load a Scala .scl microtuning file
            {
                fileChooser = std::make_unique<juce::FileChooser> ("Load Scala tuning", juce::File(), "*.scl");
                fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [this] (const juce::FileChooser& fc)
                    {
                        const auto f = fc.getResult();
                        if (f.existsAsFile() && ! apiImportScl (f.getFullPathName()))
                            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                "Load Tuning", "Could not parse a 12-note Scala scale from\n" + f.getFileName());
                    });
            }
            else if (result == 12) apiSetTuning (std::vector<double> (12, 0.0));   // reset to equal temperament
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
    nextTrackId = 1;
    projectNotes.clear();
    if (notesWindow != nullptr) notesEditor.setText ({}, juce::dontSendNotification);
    setupMixer();
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
    if (key.getKeyCode() == juce::KeyPress::tabKey)                               { cycleView(); return true; }   // Arrange/Session/Mixer
    if (key == juce::KeyPress (juce::KeyPress::spaceKey, 0, 0))                   { toggleTransport(); return true; }   // play/stop
    if (key == juce::KeyPress ('s', MK::commandModifier, 0))                      { saveCurrentProject(); return true; }
    if (key == juce::KeyPress ('g', MK::commandModifier, 0) && viewMode == ViewMode::Mixer && mixerView)
    { mixerView->groupSelected(); return true; }                                              // group selected strips
    if (key == juce::KeyPress ('g', MK::commandModifier | MK::shiftModifier, 0) && viewMode == ViewMode::Mixer && mixerView)
    { mixerView->ungroupSelected(); return true; }                                            // ungroup the selection's group
    if (key == juce::KeyPress ('g', MK::commandModifier, 0) && viewMode == ViewMode::Session && sessionPane)
    { sessionPane->grid().groupSelectedTracks(); return true; }                               // group selected track columns
    if (key == juce::KeyPress ('g', MK::commandModifier | MK::shiftModifier, 0) && viewMode == ViewMode::Session && sessionPane)
    { sessionPane->grid().ungroupSelectedTracks(); return true; }                             // ungroup the selection's group
    if (key == juce::KeyPress ('f', MK::commandModifier | MK::shiftModifier, 0)) { toggleFoldAllGroups(); return true; }  // fold/unfold all groups
    if (key == juce::KeyPress ('z', MK::commandModifier, 0))                      { undo(); return true; }
    if (key == juce::KeyPress ('z', MK::commandModifier | MK::shiftModifier, 0))  { redo(); return true; }
    if (key == juce::KeyPress ('y', MK::commandModifier, 0))                      { redo(); return true; }
    if (key == juce::KeyPress ('.', MK::commandModifier, 0))                      { apiPanic(); return true; }   // MIDI panic
    return false;
}

// Space: toggle playback in place (keeps the playhead), mirroring the play/pause button.
void MainComponent::toggleTransport()
{
    const bool willPlay = ! transport.isPlaying();
    if (willPlay) clearClipIndicators();
    transport.setPlaying (willPlay);
    playButton.setToggleState (willPlay, juce::dontSendNotification);
    playButton.setIcon (willPlay ? IconButton::Pause : IconButton::Play);
}

// Ctrl/Cmd+S: save to the current file in the format it was opened as; if never saved, Save As.
void MainComponent::saveCurrentProject()
{
    if (currentProjectFile != juce::File())
    {
        if (currentProjectFile.getFileName() == "gloopy.toml") saveComposition (currentProjectFile.getParentDirectory());
        else                                                   saveProject (currentProjectFile);
        return;
    }
    fileChooser = std::make_unique<juce::FileChooser> ("Save as .gloopy", juce::File(), "*.gloopy");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        { auto f = fc.getResult(); if (f != juce::File()) saveProject (f.withFileExtension ("gloopy")); });
}

juce::ValueTree MainComponent::clipToTree (const Clip& c, const juce::Identifier& type)
{
    juce::ValueTree cl (type);
    cl.setProperty ("ctype", (int) c.type, nullptr);
    cl.setProperty ("name", c.name, nullptr);
    cl.setProperty ("start", c.startBeat, nullptr);
    cl.setProperty ("len", c.lengthBeats, nullptr);
    cl.setProperty ("content", c.contentLenBeats, nullptr);
    cl.setProperty ("looped", c.looped, nullptr);
    if (c.transpose != 0) cl.setProperty ("transpose", c.transpose, nullptr);
    if (c.velocityScale != 1.0f) cl.setProperty ("velscale", c.velocityScale, nullptr);
    if (c.muted) cl.setProperty ("muted", true, nullptr);
    if (c.colour.getARGB() != 0) cl.setProperty ("colour", (int) c.colour.getARGB(), nullptr);   // omit when inheriting
    if (c.fadeInBeats  > 0.0) cl.setProperty ("fadein",  c.fadeInBeats,  nullptr);
    if (c.fadeOutBeats > 0.0) cl.setProperty ("fadeout", c.fadeOutBeats, nullptr);
    if (c.fadeShape != 0)     cl.setProperty ("fadeshape", c.fadeShape, nullptr);
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
            if (n.probability < 1.0f) nt.setProperty ("prob", n.probability, nullptr);
            cl.addChild (nt, -1, nullptr);
        }
    }
    return cl;
}

Clip MainComponent::clipFromTree (const juce::ValueTree& cl)
{
    Clip c;
    c.type = (ClipType) (int) cl.getProperty ("ctype", (int) ClipType::Midi);
    c.name = cl.getProperty ("name", "").toString();
    c.startBeat = (double) cl.getProperty ("start", 0.0);
    c.lengthBeats = (double) cl.getProperty ("len", 4.0);
    c.contentLenBeats = (double) cl.getProperty ("content", 4.0);
    c.looped = (bool) cl.getProperty ("looped", true);
    c.transpose = (int) cl.getProperty ("transpose", 0);
    c.velocityScale = (float) (double) cl.getProperty ("velscale", 1.0);
    c.muted  = (bool) cl.getProperty ("muted", false);
    c.colour = juce::Colour ((juce::uint32) (int) cl.getProperty ("colour", (int) 0));   // 0 = inherit track colour
    c.fadeInBeats  = (double) cl.getProperty ("fadein", 0.0);
    c.fadeOutBeats = (double) cl.getProperty ("fadeout", 0.0);
    c.fadeShape    = (int) cl.getProperty ("fadeshape", 0);

    if (c.isAudio() && cl.hasProperty ("afile"))
    {
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
            if (! nt.hasType ("NOTE")) continue;
            c.notes.push_back ({ (int) nt.getProperty ("pitch", 60),
                                 (double) nt.getProperty ("start", 0.0),
                                 (double) nt.getProperty ("nlen", 0.25),
                                 (float) (double) nt.getProperty ("vel", 0.85),
                                 (float) (double) nt.getProperty ("prob", 1.0) });
        }
    }
    return c;
}

juce::ValueTree MainComponent::toValueTree()
{
    const juce::ScopedLock sl (engineLock);
    juce::ValueTree root ("GLOOPY");
    root.setProperty ("version", 2, nullptr);
    root.setProperty ("bpm", transport.getBpm(), nullptr);
    root.setProperty ("tsnum", transport.getTimeSigNumerator(), nullptr);
    root.setProperty ("tsden", transport.getTimeSigDenominator(), nullptr);
    root.setProperty ("swing", transport.getSwing(), nullptr);
    root.setProperty ("notes", projectNotes, nullptr);
    root.setProperty ("scaleRoot", scaleRoot, nullptr);
    root.setProperty ("scaleName", scaleName, nullptr);
    { juce::StringArray iv; for (int i : scaleIntervals) iv.add (juce::String (i));
      root.setProperty ("scaleIntervals", iv.joinIntoString (","), nullptr); }
    { bool anyTune = false; for (double c : projectTuning) if (c != 0.0) anyTune = true;   // omit for 12-TET
      if (anyTune) { juce::StringArray tv; for (double c : projectTuning) tv.add (juce::String (c, 4));
                     root.setProperty ("tuningCents", tv.joinIntoString (","), nullptr); } }

    juce::ValueTree trks ("TRACKS");
    for (auto& t : tracks)
    {
        juce::ValueTree tr ("TRACK");
        tr.setProperty ("tid", t->id, nullptr);   // stable control-API id (keeps track/<id>/... param ids stable)
        tr.setProperty ("name", t->name, nullptr);
        tr.setProperty ("colour", (int) t->colour.getARGB(), nullptr);
        tr.setProperty ("pitch", t->defaultPitch, nullptr);
        tr.setProperty ("vol", t->volume.load(), nullptr);
        tr.setProperty ("pan", t->pan.load(), nullptr);
        tr.setProperty ("mute", t->mute.load(), nullptr);
        tr.setProperty ("solo", t->solo.load(), nullptr);
        if (t->polarity.load()) tr.setProperty ("polarity", true, nullptr);   // omit when off, to keep files clean
        tr.setProperty ("mixerTrack", t->mixerTrack.load(), nullptr);
        tr.setProperty ("type", (int) t->type, nullptr);
        if (t->generator) tr.setProperty ("gen", t->generator->typeName(), nullptr);
        if (t->arp.enabled)   // live arpeggiator (only stored when on, to keep files clean)
        {
            tr.setProperty ("arpOn", true, nullptr);
            tr.setProperty ("arpRate", t->arp.rate, nullptr);
            tr.setProperty ("arpOct", t->arp.octaves, nullptr);
            tr.setProperty ("arpGate", t->arp.gate, nullptr);
            tr.setProperty ("arpMode", t->arp.mode, nullptr);
            tr.setProperty ("arpSwing", t->arp.swing, nullptr);
            tr.setProperty ("arpHold", t->arp.hold, nullptr);
            if (t->arp.probability < 1.0f) tr.setProperty ("arpProb", t->arp.probability, nullptr);
        }

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
            s.setProperty ("sstart", sm->getStartFrac(), nullptr);
            s.setProperty ("send", sm->getEndFrac(), nullptr);
            s.setProperty ("srev", sm->getReverse(), nullptr);
            s.setProperty ("sfadein", sm->getFadeIn(), nullptr);
            s.setProperty ("sfadeout", sm->getFadeOut(), nullptr);
            s.setProperty ("sloop", sm->getLoop(), nullptr);
            if (sm->getMono()) s.setProperty ("smono", true, nullptr);
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
            tr.addChild (clipToTree (c), -1, nullptr);
        // Session-view slots: one SCLIP child per non-empty slot, tagged with its scene index.
        for (int s = 0; s < (int) t->sessionSlots.size(); ++s)
            if (auto& slot = t->sessionSlots[(size_t) s])
            {
                auto sc = clipToTree (*slot, "SCLIP");
                sc.setProperty ("scene", s, nullptr);
                tr.addChild (sc, -1, nullptr);
            }
        trks.addChild (tr, -1, nullptr);
    }
    root.addChild (trks, -1, nullptr);

    // Global scene rows (session view). Distinct node name from the mixer's SCENES/SCENE.
    if (! scenes.empty())
    {
        juce::ValueTree sc ("SESSIONSCENES");
        for (auto& s : scenes)
        {
            juce::ValueTree one ("SSCENE");
            one.setProperty ("name", s.name, nullptr);
            if (s.colour.getARGB() != 0) one.setProperty ("colour", (int) s.colour.getARGB(), nullptr);
            sc.addChild (one, -1, nullptr);
        }
        root.addChild (sc, -1, nullptr);
    }

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
        if (mt->output.load() != 0) t.setProperty ("out", mt->output.load(), nullptr);   // group/bus routing
        if (mt->folded.load()) t.setProperty ("fold", true, nullptr);                     // session group collapsed
        if (mt->colour.getARGB() != 0) t.setProperty ("col", (int) mt->colour.getARGB(), nullptr);   // group colour
        if (mt->group.isNotEmpty()) t.setProperty ("group", mt->group, nullptr);
        for (auto& sd : mt->sends)
        {
            juce::ValueTree sv ("SEND");
            sv.setProperty ("to", sd.bus, nullptr); sv.setProperty ("level", sd.level, nullptr);
            if (sd.postFader) sv.setProperty ("post", true, nullptr);
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

    juce::ValueTree grps ("GROUPS");
    for (auto& g : controlGroups)
    {
        juce::ValueTree gv ("GROUP");
        gv.setProperty ("name", g->name, nullptr);
        gv.setProperty ("gain", g->gain.load(), nullptr);
        gv.setProperty ("mute", g->mute.load(), nullptr);
        if (g->solo.load()) gv.setProperty ("solo", true, nullptr);
        grps.addChild (gv, -1, nullptr);
    }
    root.addChild (grps, -1, nullptr);

    juce::ValueTree au ("AUTOMATION");
    for (auto& lane : automationLanes)
    {
        juce::ValueTree l ("LANE");
        l.setProperty ("type", lane.type, nullptr); l.setProperty ("id", lane.id, nullptr);
        l.setProperty ("slot", lane.slot, nullptr); l.setProperty ("param", lane.param, nullptr);
        if (lane.target.isNotEmpty()) l.setProperty ("target", lane.target, nullptr);
        if (lane.step) l.setProperty ("step", true, nullptr);
        if (lane.curve != 0.0f) l.setProperty ("curve", lane.curve, nullptr);
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
            juce::String se; for (auto& s : in.sends) { if (se.isNotEmpty()) se << "|"; se << s.first << ":" << s.second; }
            if (se.isNotEmpty()) iv.setProperty ("sends", se, nullptr);
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
        if (m.syncBeats > 0.0f) mv.setProperty ("sync", m.syncBeats, nullptr);
        if (m.phase > 0.0f)     mv.setProperty ("phase", m.phase, nullptr);
        if (m.unipolar)         mv.setProperty ("unipolar", true, nullptr);
        if (m.slewMs > 0.0f)    mv.setProperty ("slew", m.slewMs, nullptr);
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
        if (m.bypass) v.setProperty ("bypass", true, nullptr);
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

std::unique_ptr<Track> MainComponent::buildTrackFromTree (const juce::ValueTree& tr)
{
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
            sm->setPlaybackWindow ((float) (double) s.getProperty ("sstart", 0.0),
                                   (float) (double) s.getProperty ("send", 1.0),
                                   (bool) s.getProperty ("srev", false));
            sm->setFades ((float) (double) s.getProperty ("sfadein", 0.0),
                          (float) (double) s.getProperty ("sfadeout", 0.0));
            sm->setLoop ((bool) s.getProperty ("sloop", false));
            sm->setMono ((bool) s.getProperty ("smono", false));
            gen = std::move (sm);
        }
        if (gen) gen->prepare (currentSampleRate, currentBlockSize);

        auto t = std::make_unique<Track> (tr.getProperty ("name", "Track").toString(),
                    std::move (gen), (int) tr.getProperty ("pitch", 60),
                    juce::Colour ((juce::uint32) (int) tr.getProperty ("colour", (int) 0xff4a90d9)),
                    (TrackType) ttype);
        t->id = (int) tr.getProperty ("tid", -1);   // preserve the stable control-API id if saved
        t->volume.store ((float) (double) tr.getProperty ("vol", 0.8));
        t->pan.store    ((float) (double) tr.getProperty ("pan", 0.0));
        t->mute.store   ((bool) tr.getProperty ("mute", false));
        t->solo.store   ((bool) tr.getProperty ("solo", false));
        t->polarity.store ((bool) tr.getProperty ("polarity", false));
        t->mixerTrack.store ((int) tr.getProperty ("mixerTrack", 0));
        t->arp.enabled = (bool) tr.getProperty ("arpOn", false);
        t->arp.rate    = (double) tr.getProperty ("arpRate", 0.25);
        t->arp.octaves = (int)  tr.getProperty ("arpOct", 1);
        t->arp.gate    = (float) (double) tr.getProperty ("arpGate", 0.5);
        t->arp.mode    = (int)  tr.getProperty ("arpMode", 0);
        t->arp.swing   = (float) (double) tr.getProperty ("arpSwing", 0.0);
        t->arp.hold    = (bool) tr.getProperty ("arpHold", false);
        t->arp.probability = (float) (double) tr.getProperty ("arpProb", 1.0);

        for (int ci = 0; ci < tr.getNumChildren(); ++ci)
        {
            auto cl = tr.getChild (ci);
            if (cl.hasType ("CLIP"))
                t->clips.push_back (clipFromTree (cl));
            else if (cl.hasType ("SCLIP"))   // session-view slot at its scene index
            {
                const int scene = (int) cl.getProperty ("scene", -1);
                if (scene >= 0)
                {
                    if ((int) t->sessionSlots.size() <= scene) ensureSlotCount (t->sessionSlots, scene + 1);
                    t->sessionSlots[(size_t) scene] = std::make_shared<Clip> (clipFromTree (cl));
                }
            }
        }
    return t;
}

void MainComponent::loadFromTree (const juce::ValueTree& root)
{
    if (! root.hasType ("GLOOPY")) return;

    closeAllPluginWindows();
    const juce::ScopedLock sl (engineLock);
    transport.setPlaying (false);
    tracks.clear();
    mixerTracks.clear();
    controlGroups.clear();
    locations.clear();
    exportProfiles.clear();
    mixerScenes.clear();
    modulations.clear();
    tempoMap.clear();
    controllerMaps.clear();
    automationLanes.clear();
    scenes.clear();
    sessionLauncher.reset();
    sessionBeat = 0.0;
    nextTrackId = 1;

    // Session-view scene rows (before tracks, so slots can be normalized to the scene count).
    if (auto sc = root.getChildWithName ("SESSIONSCENES"); sc.isValid())
        for (int i = 0; i < sc.getNumChildren(); ++i)
        {
            auto one = sc.getChild (i);
            if (! one.hasType ("SSCENE")) continue;
            scenes.push_back ({ one.getProperty ("name", "Scene " + juce::String (i + 1)).toString(),
                                juce::Colour ((juce::uint32) (int) one.getProperty ("colour", (int) 0)) });
        }

    auto trks = root.getChildWithName ("TRACKS");
    for (int i = 0; i < trks.getNumChildren(); ++i)
        tracks.push_back (buildTrackFromTree (trks.getChild (i)));
    // Keep every track's slot column rectangular (== scene count) after load.
    for (auto& t : tracks) ensureSlotCount (t->sessionSlots, (int) scenes.size());
    sessionLauncher.setTrackCount ((int) tracks.size());

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
            mt->output.store ((int) tv.getProperty ("out", 0));
            mt->folded.store ((bool) tv.getProperty ("fold", false));
            if (tv.hasProperty ("col")) mt->colour = juce::Colour ((juce::uint32) (int) tv.getProperty ("col", 0));
            mt->group = tv.getProperty ("group", juce::String()).toString();
            mt->buffer.setSize (2, juce::jmax (16, currentBlockSize));
            for (int f = 0; f < tv.getNumChildren(); ++f)
            {
                auto ft = tv.getChild (f);
                if (ft.hasType ("SEND"))   // aux send, not an effect
                {
                    mt->sends.push_back ({ (int) ft.getProperty ("to", 0),
                                           (float) (double) ft.getProperty ("level", 0.0),
                                           (bool) ft.getProperty ("post", false) });
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
    if (mixerTracks.empty())   // legacy project with no MIXER node: one insert per track
    {
        mixerTracks.push_back (std::make_unique<MixerTrack> ("Master"));
        for (auto& t : tracks)
        {
            mixerTracks.push_back (std::make_unique<MixerTrack> (t->name));
            t->mixerTrack.store ((int) mixerTracks.size() - 1);
        }
    }
    pruneUnbackedInserts();   // drop any leftover fixed-pool "Ins N" strips no track uses

    auto grps = root.getChildWithName ("GROUPS");
    for (int i = 0; i < grps.getNumChildren(); ++i)
    {
        auto gv = grps.getChild (i);
        auto g = std::make_unique<ControlGroup>();
        g->name = gv.getProperty ("name", "Group").toString();
        g->gain.store ((float) (double) gv.getProperty ("gain", 1.0));
        g->mute.store ((bool) gv.getProperty ("mute", false));
        g->solo.store ((bool) gv.getProperty ("solo", false));
        controlGroups.push_back (std::move (g));
    }

    auto au = root.getChildWithName ("AUTOMATION");
    for (int i = 0; i < au.getNumChildren(); ++i)
    {
        auto l = au.getChild (i);
        AutoLaneSnap lane { (int) l.getProperty ("type"), (int) l.getProperty ("id"),
                            (int) l.getProperty ("slot"), l.getProperty ("param").toString(), {},
                            l.getProperty ("target").toString(), (bool) l.getProperty ("step", false),
                            (float) (double) l.getProperty ("curve", 0.0) };
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
            const auto se = iv.getProperty ("sends").toString();
            for (auto& tok : juce::StringArray::fromTokens (se, "|", ""))
                if (tok.contains (":"))
                    in.sends.push_back ({ tok.upToFirstOccurrenceOf (":", false, false).getIntValue(),
                                          (float) tok.fromFirstOccurrenceOf (":", false, false).getDoubleValue() });
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
                                 (int) mv.getProperty ("shape", 0),
                                 (float) (double) mv.getProperty ("sync", 0.0),
                                 (float) (double) mv.getProperty ("phase", 0.0),
                                 (bool) mv.getProperty ("unipolar", false),
                                 (float) (double) mv.getProperty ("slew", 0.0) });
    }

    auto tm = root.getChildWithName ("TEMPOMAP");
    for (int i = 0; i < tm.getNumChildren(); ++i)
    {
        auto v = tm.getChild (i);
        tempoMap.push_back ({ (double) v.getProperty ("beat", 0.0), (double) v.getProperty ("bpm", 120.0) });
    }
    // The render snapshot integrates the tempo map assuming beat-ascending order; a
    // hand-edited project may not be, so enforce the invariant here.
    std::sort (tempoMap.begin(), tempoMap.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });

    auto ctl = root.getChildWithName ("CONTROLLERS");
    for (int i = 0; i < ctl.getNumChildren(); ++i)
    {
        auto v = ctl.getChild (i);
        controllerMaps.push_back ({ v.getProperty ("source").toString(), v.getProperty ("target").toString(),
                                    (float) (double) v.getProperty ("lo", 0.0), (float) (double) v.getProperty ("hi", 1.0),
                                    (bool) v.getProperty ("bypass", false) });
    }

    transport.setBpm ((double) root.getProperty ("bpm", 128.0));
    transport.setTimeSignature ((int) root.getProperty ("tsnum", 4), (int) root.getProperty ("tsden", 4));
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
    projectTuning.fill (0.0);
    if (root.hasProperty ("tuningCents"))
    {
        auto tv = juce::StringArray::fromTokens (root.getProperty ("tuningCents").toString(), ",", "");
        for (int i = 0; i < 12 && i < tv.size(); ++i) projectTuning[(size_t) i] = tv[i].getDoubleValue();
    }
    applyTuningToSynths();   // push the loaded tuning into the (just-loaded) synth voices
}

void MainComponent::refreshUiAfterLoad()
{
    for (auto& t : tracks) t->liveMidi.reset (currentSampleRate);
    { const juce::ScopedLock sl (engineLock); for (auto& t : tracks) applyArpToTrack (*t); }   // rebuild live-arp expansions
    refreshTrackIds();

    selTrack = selClip = -1;
    editorPanel.roll.setScale (scaleRoot, scaleIntervals);   // reflect a loaded project's scale
    refreshScaleToolbar();                                   // and the toolbar combos
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
