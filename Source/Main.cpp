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
        if (args.contains ("--scan") || args.contains ("--plugintest"))
        {
            PluginHost host;
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
