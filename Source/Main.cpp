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
    juce::String error, sketchId, plateId, holeSketchId, cutId;
    const auto check=[&](bool ok,const char* step){if(!ok){detail=juce::String(step)+": "+error;return false;}return true;};
    if (!check(service.createSketch("XY", 0.0, sketchId, error), "create plate sketch")) return false;
    juce::String entityId;
    if (!check(service.addSketchRectangle(sketchId, -60.0, -40.0, 120.0, 80.0, entityId, error), "add plate rectangle")) return false;
    if (!check(service.createExtrude(sketchId, 8.0, false, plateId, error), "extrude plate")) return false;
    if (!check(service.createSketch("XY", 0.0, holeSketchId, error), "create hole sketch")) return false;
    if (!check(service.addSketchCircle(holeSketchId, -45.0, -25.0, 5.0, entityId, error), "add hole 1")) return false;
    if (!check(service.addSketchCircle(holeSketchId, 45.0, -25.0, 5.0, entityId, error), "add hole 2")) return false;
    if (!check(service.addSketchCircle(holeSketchId, -45.0, 25.0, 5.0, entityId, error), "add hole 3")) return false;
    if (!check(service.addSketchCircle(holeSketchId, 45.0, 25.0, 5.0, entityId, error), "add hole 4")) return false;
    if (!check(service.createCut(holeSketchId, plateId, 0.0, true, false, cutId, error), "cut through holes")) return false;
    const auto* plateSketch = service.document().findSketch(sketchId);
    if (plateSketch == nullptr || plateSketch->entities.empty()) { detail = "find plate driving entity: missing"; return false; }
    if (!check(service.setSketchEntityDimension(sketchId, plateSketch->entities.front().id, 120.0, error), "edit driving dimension")) return false;
    if (!check(service.undo(error), "undo driving dimension")) return false;
    if (!check(service.redo(error), "redo driving dimension")) return false;

    djehuti::cad::CadPersistentReference reference;
    const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto native = temp.getChildFile("djehuti_cad_foundation_check.djo");
    const auto json = temp.getChildFile("djehuti_cad_foundation_check.djc");
    native.deleteFile();
    json.deleteFile();
    if (!check(service.save(native, error), "save native document")) return false;

    djehuti::cad::CadCommandService reopened;
    if (!check(reopened.load(native, error), "reopen native document")) return false;
    if (reopened.document().findSketch(sketchId) == nullptr || reopened.document().findSketch(holeSketchId) == nullptr || reopened.document().find(cutId) == nullptr) { detail = "reopen native document: sketch dependency graph missing"; return false; }
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
            result.replaceWithText(ok ? "CAD_SKETCH_FOUNDATION_CHECK_OK" : "CAD_SKETCH_FOUNDATION_CHECK_FAILED\n" + detail);
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
