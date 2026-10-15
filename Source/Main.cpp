#include <JuceHeader.h>

#include "DjehutiCAD.h"
#include "FrustExecution.h"

extern "C" void (*g_frust_dbg_callback)(int, int) = nullptr;

class DjehutiCADApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "DjehutiCAD"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow = nullptr; }

    void systemRequestedQuit() override
    {
        frust_exec::Executor::instance().shutdown([this] { quit(); }, 3000);
    }

    void anotherInstanceStarted(const juce::String&) override
    {
        if (mainWindow != nullptr)
        {
            mainWindow->setMinimised(false);
            mainWindow->toFront(true);
        }
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xff171b20), allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new DjehutiCAD(), true);
            setResizable(true, true);
            setResizeLimits(1100, 720, 3840, 2160);
            centreWithSize(1500, 920);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            if (auto* cad = dynamic_cast<DjehutiCAD*>(getContentComponent()))
                cad->confirmCloseThen([this] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
            else
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(DjehutiCADApplication)
