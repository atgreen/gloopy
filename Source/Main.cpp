#include <JuceHeader.h>
#include "MainComponent.h"

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
