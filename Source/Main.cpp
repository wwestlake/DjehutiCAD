#include <JuceHeader.h>

#include "DjehutiCAD.h"
#include "FrustExecution.h"
#include "CadDocument.h"

#include <cstdlib>

namespace
{
bool runCadFoundationCheck(juce::String& detail)
{
    djehuti::cad::CadCommandService service;
    juce::String error, boxId, cylinderId, subtractId;
    const auto check=[&](bool ok,const char* step){if(!ok){detail=juce::String(step)+": "+error;return false;}return true;};
    if (!check(service.createBox(10.0, 10.0, 10.0, boxId, error), "create box")) return false;
    if (!check(service.createCylinder(2.0, 12.0, cylinderId, error), "create cylinder")) return false;
    if (!check(service.setPlacement(cylinderId, 4.0, 4.0, -1.0, 0.0, 0.0, 0.0, error), "place cylinder")) return false;
    if (!check(service.booleanOp("subtract", boxId, cylinderId, subtractId, error), "subtract cylinder")) return false;

    djehuti::cad::CadPersistentReference reference;
    if (!check(service.makeRepresentativeReference(boxId, reference, error), "create representative reference")) return false;
    if (!check(service.setDimensions(boxId, 12.0, 10.0, 10.0, 0.0, error), "edit box dimensions")) return false;
    if (!check(service.resolveReference(reference, error), "resolve after edit")) return false;
    if (!check(service.undo(error), "undo dimension edit")) return false;
    if (!check(service.resolveReference(reference, error), "resolve after undo")) return false;
    if (!check(service.redo(error), "redo dimension edit")) return false;
    if (!check(service.resolveReference(reference, error), "resolve after redo")) return false;

    const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto native = temp.getChildFile("djehuti_cad_foundation_check.djo");
    const auto json = temp.getChildFile("djehuti_cad_foundation_check.djc");
    native.deleteFile();
    json.deleteFile();
    if (!check(service.save(native, error), "save native document")) return false;

    djehuti::cad::CadCommandService reopened;
    if (!check(reopened.load(native, error), "reopen native document")) return false;
    if (!check(reopened.resolveReference(reference, error), "resolve after native reopen")) return false;
    if (!check(reopened.document().exportJson(json, error) && json.existsAsFile() && json.getSize() > 0, "export JSON")) return false;
    native.deleteFile();
    json.deleteFile();
    return true;
}
}

extern "C" void (*g_frust_dbg_callback)(int, int) = nullptr;

class DjehutiCADApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "DjehutiCAD"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override
    {
        if (commandLine.contains("--cad-foundation-check"))
        {
            juce::String detail;
            const bool ok = runCadFoundationCheck(detail);
            const auto result = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("djehuti_cad_foundation_check.result");
            result.replaceWithText(ok ? "CAD_FOUNDATION_CHECK_OK" : "CAD_FOUNDATION_CHECK_FAILED\n" + detail);
            std::exit(ok ? 0 : 1);
        }
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
