// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#include <JuceHeader.h>
#include <iostream>
#include <sstream>
#include "MainComponent.h"
#include "PluginHost.h"
#include "SurgePatchName.h"

// Redirects std::cout to a throwaway buffer for its lifetime, so the runtime's
// diagnostic chatter (`[composition] loaded …`) during a CLI load/save doesn't
// pollute the JSON/result stdout the tool is supposed to emit. std::cerr is left
// alone for real errors.
struct CoutSilencer
{
    std::streambuf* saved; std::ostringstream sink;
    CoutSilencer() : saved (std::cout.rdbuf()) { std::cout.rdbuf (sink.rdbuf()); }
    ~CoutSilencer() { std::cout.rdbuf (saved); }
};

/** Application entry point + the main window that hosts MainComponent. */
class GloopyApplication : public juce::JUCEApplication
{
public:
    GloopyApplication() = default;

    const juce::String getApplicationName() override    { return "Gloopy"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    static juce::File resolve (const juce::String& path)
    {
        return juce::File::isAbsolutePath (path)
                    ? juce::File (path)
                    : juce::File::getCurrentWorkingDirectory().getChildFile (path);
    }

    void initialise (const juce::String& commandLine) override
    {
        auto args = getCommandLineParameterArray();

        if (args.contains ("--version") || args.contains ("-v"))
        {
           #ifdef JUCE_APPLICATION_VERSION_STRING
            std::cout << "gloopy " << JUCE_APPLICATION_VERSION_STRING << std::endl;
           #else
            std::cout << "gloopy" << std::endl;
           #endif
            setApplicationReturnValue (0);
            quit();
            return;
        }

        if (args.contains ("--help") || args.contains ("-h"))
        {
            std::cout <<
                "gloopy \xe2\x80\x94 a scriptable, composition-as-repo DAW (JUCE 8 / C++17)\n"
                "\n"
                "Usage:\n"
                "  gloopy                          Launch the GUI\n"
                "  gloopy <project.gloopy | dir>   Launch the GUI, opening a project\n"
                "  gloopy --version                Print the version\n"
                "  gloopy --help                   Show this help\n"
                "\n"
                "Headless commands (no GUI; results on stdout):\n"
                "  gloopy render <project> [out.wav] [--range <startBeat> <endBeat>]\n"
                "                                          Bounce the mix (or a beat range) to a WAV\n"
                "  gloopy export-stems <project> [outdir]  One WAV per instrument track\n"
                "  gloopy analyze <file.wav>               Loudness report (peak/LUFS/...) as JSON\n"
                "  gloopy inspect <project>                Project summary as JSON\n"
                "  gloopy validate <project> [--loudness]  Validate (optionally render + measure)\n"
                "  gloopy pack <project> <out.zip>         Zip a project as a composition\n"
                "  gloopy scan [--force]                   List installed VST3/LV2 plugins as JSON\n"
                "\n"
                "While the GUI runs, gloopy exposes an OSC (UDP 9000) and gRPC\n"
                "(127.0.0.1:50051) control API. See the README for the client libraries.\n";
            setApplicationReturnValue (0);
            quit();
            return;
        }

        // Headless plugin diagnostics.
        if (args.contains ("--scan") || args.contains ("--plugintest") || args.contains ("--plugindesc")
              || args.contains ("--plugparams") || args.contains ("--bakestate")
              || args.contains ("--surgepatch"))
        {
            PluginHost host;
            if (args.contains ("--scan") || ! host.loadCache())   // cache is instant; --scan forces fresh
                host.scanAll();
            const auto types = host.knownList.getTypes();

            if (args.contains ("--scan"))
                for (const auto& d : types)
                    std::cout << (d.isInstrument ? "[instr] " : "[fx]    ")
                              << d.name << "  (" << d.pluginFormatName << ")\n";

            const int pd = args.indexOf ("--plugindesc");
            if (pd >= 0 && pd + 1 < args.size())
                for (const auto& d : types)
                    if (d.name.containsIgnoreCase (args[pd + 1]))
                        { if (auto xml = d.createXml()) std::cout << xml->toString() << "\n"; break; }

            // --plugparams <name>: dump a plugin's parameters (idx | name | value | text)
            const int ppr = args.indexOf ("--plugparams");
            if (ppr >= 0 && ppr + 1 < args.size())
                for (const auto& d : types)
                {
                    if (! d.name.containsIgnoreCase (args[ppr + 1]) || ! d.isInstrument) continue;
                    juce::String err;
                    auto inst = host.create (d, 44100.0, 512, err);
                    if (inst == nullptr) { std::cout << "create failed: " << err << "\n"; break; }
                    const auto& params = inst->getParameters();
                    for (int i = 0; i < params.size(); ++i)
                        std::cout << i << " | " << params[i]->getName (64) << " | "
                                  << params[i]->getValue() << " | "
                                  << params[i]->getText (params[i]->getValue(), 32) << "\n";
                    break;
                }

            // --bakestate <name> "<Sub>=<0..1>;..." <out>: set params, save state (+ .desc)
            const int bs = args.indexOf ("--bakestate");
            if (bs >= 0 && bs + 3 < args.size())
                for (const auto& d : types)
                {
                    if (! d.name.containsIgnoreCase (args[bs + 1]) || ! d.isInstrument
                          || d.pluginFormatName != "VST3") continue;
                    juce::String err;
                    auto inst = host.create (d, 44100.0, 512, err);
                    if (inst == nullptr) { std::cout << "create failed: " << err << "\n"; break; }
                    inst->setPlayConfigDetails (0, 2, 44100.0, 512);
                    inst->prepareToPlay (44100.0, 512);

                    const auto& params = inst->getParameters();
                    juce::StringArray entries;
                    entries.addTokens (args[bs + 2], ";", "");
                    for (const auto& e : entries)
                    {
                        auto kv = juce::StringArray::fromTokens (e, "=", "");
                        if (kv.size() != 2) continue;
                        const auto pn = kv[0].trim();
                        const float v = juce::jlimit (0.0f, 1.0f, kv[1].getFloatValue());
                        for (auto* p : params)
                            if (p->getName (64).containsIgnoreCase (pn))
                            {
                                p->setValueNotifyingHost (v);
                                std::cout << "set [" << p->getName (64) << "] = " << v << "\n";
                                break;
                            }
                    }

                    juce::AudioBuffer<float> buf (2, 512);
                    float peak = 0.0f;
                    for (int blk = 0; blk < 25; ++blk)
                    {
                        buf.clear(); juce::MidiBuffer m;
                        if (blk == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);
                        inst->processBlock (buf, m);
                        peak = juce::jmax (peak, buf.getMagnitude (0, 0, 512));
                    }
                    juce::MemoryBlock st; inst->getStateInformation (st);
                    juce::File (args[bs + 3]).replaceWithText (st.toBase64Encoding());
                    if (auto xml = d.createXml()) juce::File (args[bs + 3] + ".desc").replaceWithText (xml->toString());
                    std::cout << "BAKED " << d.name << " state=" << st.getSize()
                              << " bytes, peak=" << peak << " -> " << args[bs + 3] << "\n";
                    break;
                }

            // --surgepatch <fxp>: probe the migration linchpin — load a factory Surge patch into the
            // *hosted* Surge XT (LV2 preferred) via setStateInformation. On-disk .fxp files are
            // FXP-wrapped (fxChunkSetCustom, 60-byte header); loadRaw wants the raw chunk, so we strip
            // the header and pass the trailing chunk. Reports how many params changed vs INIT (>0 ⇒
            // the patch loaded across the LV2 state boundary) and the rendered peak.
            const int spx = args.indexOf ("--surgepatch");
            if (spx >= 0 && spx + 1 < args.size())
            {
                // Preferred format defaults to LV2 (what we bundle); GLOOPY_SURGE_FMT overrides (probe).
                const auto prefFmt = juce::SystemStats::getEnvironmentVariable ("GLOOPY_SURGE_FMT", "LV2");
                juce::PluginDescription surgeDesc; bool found = false;
                for (const auto& d : types)
                    if (d.isInstrument && d.name.containsIgnoreCase ("Surge XT"))
                    { surgeDesc = d; found = true; if (d.pluginFormatName == prefFmt) break; }
                juce::String err;
                auto inst = found ? host.create (surgeDesc, 44100.0, 512, err) : nullptr;
                if (inst == nullptr) { std::cout << "surgepatch: Surge XT unavailable (" << err << ")\n"; }
                else
                {
                    inst->setPlayConfigDetails (0, 2, 44100.0, 512);
                    inst->prepareToPlay (44100.0, 512);
                    juce::MemoryBlock stBefore; inst->getStateInformation (stBefore);   // INIT plugin state
                    std::cout << "PATCHNAME=[" << surgePatchName (*inst) << "]\n";       // regression check
                    // Render a held middle-C and return the energy (sum of squares) + peak. A patch that
                    // loads changes the timbre -> a different energy signature, which survives the LV2
                    // boundary even when the host-side param cache does NOT refresh on patch load.
                    auto renderSig = [&] (double& energy) {
                        float peak = 0.0f; energy = 0.0;
                        for (int blk = 0; blk < 40; ++blk)
                        {
                            juce::AudioBuffer<float> a (2, 512); a.clear(); juce::MidiBuffer m;
                            if (blk == 0)  m.addEvent (juce::MidiMessage::noteOn  (1, 60, (juce::uint8) 110), 0);
                            if (blk == 32) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                            inst->processBlock (a, m);
                            peak = juce::jmax (peak, a.getMagnitude (0, 0, 512));
                            for (int ch = 0; ch < 2; ++ch)
                            { auto* s = a.getReadPointer (ch); for (int i = 0; i < 512; ++i) energy += (double) s[i] * s[i]; }
                        }
                        return peak;
                    };
                    double eInit = 0.0; renderSig (eInit);   // INIT patch baseline (energy via out-param)

                    juce::MemoryBlock raw; juce::File (args[spx + 1]).loadFileAsData (raw);
                    const int hdr = 60;   // sizeof fxChunkSetCustom (8 ints + prgName[28])
                    if ((int) raw.getSize() <= hdr) { std::cout << "surgepatch: fxp too small / missing\n"; }
                    else
                    {
                        auto* b = static_cast<const juce::uint8*> (raw.getData());
                        const int chunkSize = (b[56] << 24) | (b[57] << 16) | (b[58] << 8) | b[59];   // big-endian
                        const int avail = (int) raw.getSize() - hdr;
                        const int useSize = (chunkSize > 0 && chunkSize <= avail) ? chunkSize : avail;
                        inst->setStateInformation (b + hdr, useSize);
                        for (int blk = 0; blk < 8; ++blk)                     // pump silent blocks to apply the enqueued load
                        { juce::AudioBuffer<float> a (2, 512); a.clear(); juce::MidiBuffer m; inst->processBlock (a, m); }

                        // Decisive: did the plugin's OWN state change, and does it now name the patch?
                        juce::MemoryBlock stAfter; inst->getStateInformation (stAfter);
                        const bool stateChanged = (stBefore.getSize() != stAfter.getSize())
                                                  || (std::memcmp (stBefore.getData(), stAfter.getData(),
                                                                   juce::jmin (stBefore.getSize(), stAfter.getSize())) != 0);
                        const auto stem = juce::File (args[spx + 1]).getFileNameWithoutExtension();
                        juce::String hay (static_cast<const char*> (stAfter.getData()),
                                          (size_t) juce::jmin ((int) stAfter.getSize(), 1 << 20));
                        const bool namesPatch = hay.contains (stem);

                        double eLoaded = 0.0; renderSig (eLoaded);
                        const double rel = (eInit > 0.0) ? std::abs (eLoaded - eInit) / eInit
                                                         : (eLoaded > 0.0 ? 1.0 : 0.0);
                        std::cout << "SURGEPATCH " << juce::File (args[spx + 1]).getFileName()
                                  << " format=" << surgeDesc.pluginFormatName << " chunk=" << useSize
                                  << " stBefore=" << stBefore.getSize() << " stAfter=" << stAfter.getSize()
                                  << " stateChanged=" << (stateChanged ? 1 : 0)
                                  << " namesPatch=" << (namesPatch ? 1 : 0)
                                  << " relDiff=" << rel
                                  << (rel > 0.05 ? "  => PATCH LOADED" : "  => NO CHANGE") << "\n";
                    }
                }
            }

            const int pt = args.indexOf ("--plugintest");
            if (pt >= 0 && pt + 1 < args.size())
            {
                const auto want = args[pt + 1];
                for (const auto& d : types)
                {
                    if (! (d.isInstrument && d.name.containsIgnoreCase (want))) continue;
                    juce::String err;
                    auto inst = host.create (d, 44100.0, 512, err);
                    if (inst == nullptr) { std::cout << "create failed: " << err << "\n"; break; }
                    inst->setPlayConfigDetails (0, 2, 44100.0, 512);
                    inst->prepareToPlay (44100.0, 512);
                    juce::AudioBuffer<float> buf (2, 512);
                    float peak = 0.0f;
                    for (int blk = 0; blk < 25; ++blk)
                    {
                        buf.clear();
                        juce::MidiBuffer m;
                        if (blk == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);
                        inst->processBlock (buf, m);
                        peak = juce::jmax (peak, buf.getMagnitude (0, 0, 512));
                    }
                    std::cout << "PLUGIN " << d.name << " peak=" << peak
                              << (peak > 0.0001f ? "  OK-AUDIO" : "  SILENT") << "\n";
                    break;
                }
            }
            quit();
            return;
        }

        // Headless composition utilities: gloopy <inspect|validate|pack> <project> [out]
        // Reuse the GUI/gRPC load paths; emit stable JSON on stdout; no control ports.
        // Headless plugin scan/list: gloopy scan [--force] -> JSON array of plugins
        if (args.size() >= 1 && args[0] == "scan")
        {
            const bool force = args.contains ("--force");
            std::unique_ptr<MainComponent> comp;
            std::vector<MainComponent::PluginSnap> plugs;
            { CoutSilencer s; comp = std::make_unique<MainComponent> (true);
              plugs = force ? comp->apiScanPlugins (true) : comp->apiListPlugins(); }
            juce::Array<juce::var> arr;
            for (auto& p : plugs)
            {
                juce::DynamicObject::Ptr o = new juce::DynamicObject();
                o->setProperty ("name", p.name);           o->setProperty ("format", p.format);
                o->setProperty ("is_instrument", p.isInstrument);
                o->setProperty ("identifier", p.identifier); o->setProperty ("vendor", p.vendor);
                o->setProperty ("category", p.category);   o->setProperty ("version", p.version);
                o->setProperty ("num_inputs", p.numInputs); o->setProperty ("num_outputs", p.numOutputs);
                arr.add (juce::var (o.get()));
            }
            std::cout << juce::JSON::toString (juce::var (arr)) << std::endl;
            setApplicationReturnValue (0);
            quit();
            return;
        }

        // Headless loudness analysis: gloopy analyze <file.wav> -> JSON
        if (args.size() >= 2 && args[0] == "analyze")
        {
            std::unique_ptr<MainComponent> comp;
            MainComponent::LoudnessReport rep;
            bool ok; { CoutSilencer s; comp = std::make_unique<MainComponent> (true);   // headless CLI mode
                       ok = comp->apiAnalyzeFile (resolve (args[1]).getFullPathName(), rep); }
            int rc = 0;
            if (ok)
                std::cout << "{ \"peak_dbfs\": " << rep.peakDbfs << ", \"true_peak_dbtp\": " << rep.truePeakDbtp
                          << ", \"rms_dbfs\": " << rep.rmsDbfs << ", \"lufs\": " << rep.lufs
                          << ", \"momentary_lufs\": " << rep.momentaryLufs << ", \"short_term_lufs\": " << rep.shortTermLufs
                          << ", \"lra\": " << rep.lra << " }" << std::endl;
            else { std::cerr << "analyze: cannot read " << args[1] << "\n"; rc = 1; }
            setApplicationReturnValue (rc);
            quit();
            return;
        }

        if (args.size() >= 2 && (args[0] == "inspect" || args[0] == "validate" || args[0] == "pack"
                                 || args[0] == "render" || args[0] == "export-stems"))
        {
            std::unique_ptr<MainComponent> comp;
            { CoutSilencer s; comp = std::make_unique<MainComponent> (true);   // headless CLI mode
              comp->openProjectFile (resolve (args[1])); }
            int rc = 0;

            if (args[0] == "inspect")
                std::cout << comp->apiInspectJson() << std::endl;
            else if (args[0] == "validate")
            {
                bool ok = true;
                const bool loud = args.contains ("--loudness");   // also render + measure the mix
                // The render/analyze pass emits diagnostic chatter; keep stdout pure JSON.
                juce::String out; { CoutSilencer s; out = comp->apiValidateJson (ok, loud); }
                std::cout << out << std::endl;
                rc = ok ? 0 : 1;
            }
            else if (args[0] == "render")   // render <project> [out.wav] [--range <startBeat> <endBeat>]
            {
                const auto in  = resolve (args[1]);
                // Positional out path only if present and not a --flag; else <project>.wav.
                const juce::String outArg = (args.size() >= 3 && ! args[2].startsWith ("--")) ? args[2] : juce::String();
                const auto out = outArg.isNotEmpty() ? resolve (outArg)
                                   : in.getParentDirectory().getChildFile (in.getFileNameWithoutExtension() + ".wav");
                // --range <startBeat> <endBeat>: bounce just that beat window (else the whole project).
                double startBeat = 0.0, endBeat = 0.0;
                const int rr = args.indexOf ("--range");
                if (rr >= 0 && rr + 2 < args.size())
                { startBeat = args[rr + 1].getDoubleValue(); endBeat = args[rr + 2].getDoubleValue(); }
                bool ok; { CoutSilencer s; comp->prepareToPlay (512, 44100.0);   // headless: prep the generators
                           ok = comp->apiRenderToFile (out.getFullPathName(), 2.0, startBeat, endBeat, false, 0); }
                if (ok) std::cout << out.getFullPathName() << std::endl;
                else    std::cerr << "render: failed to write " << out.getFullPathName() << "\n";
                rc = ok ? 0 : 1;
            }
            else if (args[0] == "export-stems")   // export-stems <project> [outdir]: one WAV per instrument track
            {
                auto dir = args.size() >= 3 ? resolve (args[2])
                             : resolve (args[1]).getParentDirectory().getChildFile ("stems");
                juce::Array<juce::var> files;
                { CoutSilencer s; comp->prepareToPlay (512, 44100.0);
                  for (auto& p : comp->apiExportStems (dir.getFullPathName())) files.add (p); }
                juce::DynamicObject::Ptr o = new juce::DynamicObject();
                o->setProperty ("stems", juce::var (files));
                std::cout << juce::JSON::toString (juce::var (o.get())) << std::endl;
            }
            else   // pack <project> <out.zip>: normalise to a composition, then zip it
            {
                if (args.size() < 3) { std::cerr << "usage: gloopy pack <project> <out.zip>\n"; rc = 2; }
                else
                {
                    const auto out = resolve (args[2]);
                    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("gloopy-pack-" + juce::String (juce::Time::getMillisecondCounterHiRes()));
                    const auto compDir = tmp.getChildFile (out.getFileNameWithoutExtension());
                    compDir.createDirectory();
                    bool savedOk; { CoutSilencer s; savedOk = comp->apiSaveComposition (compDir.getFullPathName()); }
                    if (! savedOk) rc = 1;
                    else
                    {
                        juce::ZipFile::Builder b;
                        for (auto& f : compDir.findChildFiles (juce::File::findFiles, true))
                            b.addFile (f, 9, f.getRelativePathFrom (tmp));
                        out.deleteFile();
                        if (auto os = out.createOutputStream())
                        {
                            b.writeToStream (*os, nullptr);
                            std::cout << "packed " << args[1] << " -> " << out.getFullPathName() << std::endl;
                        }
                        else { std::cerr << "pack: cannot write " << out.getFullPathName() << "\n"; rc = 1; }
                    }
                    tmp.deleteRecursively();
                }
            }

            setApplicationReturnValue (rc);
            quit();
            return;
        }

        // Headless render: --render <in.gloopy> <out.wav>
        const int r = args.indexOf ("--render");
        if (r >= 0 && r + 2 < args.size())
        {
            renderComp = std::make_unique<MainComponent>();
            renderComp->openProjectFile (resolve (args[r + 1]));
            renderComp->beginRenderMode (resolve (args[r + 2]));
            return;   // no window
        }

        auto* comp = new MainComponent();
        mainWindow.reset (new MainWindow ("Gloopy", comp));

        const auto arg = commandLine.trim().unquoted();
        if (arg.isNotEmpty())
            comp->openProjectFile (resolve (arg));
    }

    void shutdown() override { mainWindow = nullptr; renderComp = nullptr; }

    void systemRequestedQuit() override { quit(); }

    // ---------------------------------------------------------------
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name, juce::Component* c)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (c, true);
            setResizable (true, true);
            setResizeLimits (720, 480, 4000, 3000);
            // centreWithSize dereferences the display list, which is empty on a bare
            // X server (e.g. Xvfb) — guard it so the app doesn't segfault at startup.
            if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
                setTopLeftPosition (40, 40);
            else
                centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow>    mainWindow;
    std::unique_ptr<MainComponent> renderComp;   // headless render mode
};

START_JUCE_APPLICATION (GloopyApplication)
