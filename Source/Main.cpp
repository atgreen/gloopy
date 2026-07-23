#include <JuceHeader.h>
#include <iostream>
#include "MainComponent.h"
#include "PluginHost.h"

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

        // Headless plugin diagnostics.
        if (args.contains ("--scan") || args.contains ("--plugintest") || args.contains ("--plugindesc")
              || args.contains ("--plugparams") || args.contains ("--bakestate"))
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
