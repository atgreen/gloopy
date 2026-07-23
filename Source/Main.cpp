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

    void initialise (const juce::String& commandLine) override
    {
        auto* comp = new MainComponent();
        mainWindow.reset (new MainWindow ("Gloopy", comp));

        const auto arg = commandLine.trim().unquoted();
        if (arg.isNotEmpty())
        {
            auto f = juce::File::isAbsolutePath (arg)
                        ? juce::File (arg)
                        : juce::File::getCurrentWorkingDirectory().getChildFile (arg);
            comp->openProjectFile (f);
        }
    }

    void shutdown() override { mainWindow = nullptr; }

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
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (GloopyApplication)
