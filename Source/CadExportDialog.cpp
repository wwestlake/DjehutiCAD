#include "CadExportDialog.h"
#include "CadDocument.h"

#include <atomic>

namespace
{
using namespace djehuti::cad;

const juce::Colour kBackground(0xff171b20), kText(0xffd7dde3), kDim(0xff8b97a3), kError(0xffff8a80), kGood(0xff8fd19e);

// Runs the writer on a worker thread from the plan's private geometry copies; never touches the document.
class ExportJob final : public juce::Thread
{
public:
    ExportJob(CadExportPlan planIn, std::function<void()> doneIn) : juce::Thread("CAD export"), plan(std::move(planIn)), done(std::move(doneIn)) {}
    ~ExportJob() override { cancel = true; stopThread(60000); }
    void run() override
    {
        result = CadCommandService::runExport(plan, [this](double fraction, const juce::String& text)
        {
            progress = fraction;
            { const juce::ScopedLock lock(stageLock); stage = text; }
            return !cancel.load() && !threadShouldExit();
        });
        juce::MessageManager::callAsync(done);
    }
    juce::String currentStage() const { const juce::ScopedLock lock(stageLock); return stage; }

    CadExportPlan plan;
    CadExportResult result;
    std::atomic<bool> cancel { false };
    std::atomic<double> progress { 0.0 };
private:
    std::function<void()> done;
    juce::CriticalSection stageLock;
    juce::String stage;
};

class ExportDialog final : public juce::Component, private juce::Timer
{
public:
    ExportDialog(std::shared_ptr<CadCommandService> serviceIn, const juce::StringArray& preselected) : service(std::move(serviceIn))
    {
        for (auto* l : { &formatLabel, &partsLabel, &unitsLabel, &placementLabel, &qualityLabel, &outputLabel }) { l->setColour(juce::Label::textColourId, kDim); addAndMakeVisible(*l); }
        for (auto* l : { &summary, &placementNote, &formatNote, &status }) { l->setColour(juce::Label::textColourId, kText); l->setJustificationType(juce::Justification::topLeft); addAndMakeVisible(*l); }
        placementNote.setText("Reference configuration: authored body and instance placements. Simulation and IK poses are not exported.", juce::dontSendNotification);

        format.addItem("3MF - preferred for 3-D printing", 1);
        format.addItem("Binary STL - one file per part", 2);
        format.addItem("STEP AP214 - exact solids for CAD exchange", 3);
        format.addItem("glTF 2.0 Binary (GLB) - game-ready mesh export", 4);
        format.setSelectedId(1, juce::dontSendNotification);
        format.onChange = [this] { formatChanged(); };
        addAndMakeVisible(format);

        quality.addItem("Standard - surface 0.05 mm, angle 15 deg", 1);
        quality.addItem("Fine - surface 0.01 mm, angle 5 deg", 2);
        quality.addItem("Custom", 3);
        quality.setSelectedId(1, juce::dontSendNotification);
        quality.onChange = [this] { qualityChanged(); };
        addAndMakeVisible(quality);
        for (auto* e : { &chordal, &angular }) { e->setInputRestrictions(8, "0123456789."); e->onTextChange = [this] { refreshSummary(); }; addAndMakeVisible(*e); }
        chordalUnit.setText("mm surface (chordal)", juce::dontSendNotification); angularUnit.setText("deg between facets", juce::dontSendNotification);
        for (auto* l : { &chordalUnit, &angularUnit }) { l->setColour(juce::Label::textColourId, kDim); addAndMakeVisible(*l); }

        // Exportable parts: the visible modeled assembly (bodies, or the instances of instanced bodies).
        CadExportPlan preview; juce::String error;
        CadExportSettings all; all.format = CadExportFormat::step;
        if (service->planExport(all, preview, error))
            for (const auto& part : preview.parts)
            {
                auto* toggle = partToggles.add(new juce::ToggleButton(part.name + (part.instance ? "  (instance)" : "") + "   "
                                                                     + juce::String(part.max[0] - part.min[0], 1) + " x " + juce::String(part.max[1] - part.min[1], 1) + " x " + juce::String(part.max[2] - part.min[2], 1) + " mm"));
                toggle->setComponentID(part.id);
                toggle->setColour(juce::ToggleButton::textColourId, kText);
                toggle->setToggleState(preselected.isEmpty() || preselected.contains(part.id), juce::dontSendNotification);
                toggle->onClick = [this] { refreshSummary(); };
                partsContent.addAndMakeVisible(toggle);
            }
        else status.setText(error, juce::dontSendNotification);
        // A preselected hidden or otherwise non-listed id still exports if asked for explicitly.
        for (const auto& id : preselected) { bool listed = false; for (auto* t : partToggles) listed = listed || t->getComponentID() == id; if (!listed) extraIds.add(id); }
        partsView.setViewedComponent(&partsContent, false);
        partsView.setScrollBarsShown(true, false);
        addAndMakeVisible(partsView);
        for (auto* b : { &allButton, &noneButton }) addAndMakeVisible(*b);
        allButton.onClick = [this] { for (auto* t : partToggles) t->setToggleState(true, juce::dontSendNotification); refreshSummary(); };
        noneButton.onClick = [this] { for (auto* t : partToggles) t->setToggleState(false, juce::dontSendNotification); refreshSummary(); };

        const auto file = service->document().file();
        const auto folder = file.existsAsFile() ? file.getParentDirectory() : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        path.setText(folder.getChildFile(file.existsAsFile() ? file.getFileNameWithoutExtension() : juce::String("model")).withFileExtension(".3mf").getFullPathName(), juce::dontSendNotification);
        addAndMakeVisible(path);
        chooseButton.onClick = [this] { choose(false); };
        exportButton.onClick = [this] { startExport(); };
        closeButton.onClick = [this] { if (job != nullptr) { job->cancel = true; status.setText("Cancelling...", juce::dontSendNotification); } else close(); };
        for (auto* b : { &chooseButton, &exportButton, &closeButton }) addAndMakeVisible(*b);
        addAndMakeVisible(bar);
        bar.setVisible(false);
        qualityChanged();
        formatChanged();
        setSize(620, 640);
    }

    ~ExportDialog() override { stopTimer(); job.reset(); }

    void paint(juce::Graphics& g) override { g.fillAll(kBackground); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        const auto row = [&](juce::Label& label, int height) { auto r = area.removeFromTop(height); label.setBounds(r.removeFromLeft(110)); area.removeFromTop(8); return r; };
        format.setBounds(row(formatLabel, 26));
        formatNote.setBounds(area.removeFromTop(34).withTrimmedLeft(110)); area.removeFromTop(4);
        auto parts = row(partsLabel, 150);
        auto buttons = parts.removeFromBottom(26); parts.removeFromBottom(4);
        allButton.setBounds(buttons.removeFromLeft(70)); buttons.removeFromLeft(6); noneButton.setBounds(buttons.removeFromLeft(70));
        partsView.setBounds(parts);
        int y = 0; for (auto* t : partToggles) { t->setBounds(0, y, parts.getWidth() - 14, 24); y += 24; }
        partsContent.setSize(parts.getWidth() - 14, juce::jmax(y, 1));
        summary.setBounds(row(unitsLabel, 36));
        placementNote.setBounds(row(placementLabel, 36));
        auto q = row(qualityLabel, 26); quality.setBounds(q);
        auto tolerances = area.removeFromTop(26).withTrimmedLeft(110); area.removeFromTop(8);
        chordal.setBounds(tolerances.removeFromLeft(60)); chordalUnit.setBounds(tolerances.removeFromLeft(150));
        angular.setBounds(tolerances.removeFromLeft(60)); angularUnit.setBounds(tolerances);
        auto out = row(outputLabel, 26);
        chooseButton.setBounds(out.removeFromRight(90)); out.removeFromRight(6); path.setBounds(out);
        auto bottom = area.removeFromBottom(30);
        closeButton.setBounds(bottom.removeFromRight(100)); bottom.removeFromRight(8); exportButton.setBounds(bottom.removeFromRight(100));
        area.removeFromBottom(8);
        bar.setBounds(area.removeFromBottom(18));
        area.removeFromBottom(6);
        status.setBounds(area);
    }

private:
    CadExportFormat currentFormat() const { return format.getSelectedId() == 2 ? CadExportFormat::stl : format.getSelectedId() == 3 ? CadExportFormat::step : format.getSelectedId() == 4 ? CadExportFormat::glb : CadExportFormat::threeMF; }

    static double number(const juce::TextEditor& editor)
    {
        const auto text = editor.getText().trim();
        return text.isEmpty() || text.containsAnyOf("eE") || text.indexOfChar('.') != text.lastIndexOfChar('.') ? std::nan("") : text.getDoubleValue();
    }

    CadExportSettings settings() const
    {
        CadExportSettings s;
        s.format = currentFormat();
        bool everything = extraIds.isEmpty();
        for (auto* t : partToggles) { if (t->getToggleState()) s.partIds.add(t->getComponentID()); else everything = false; }
        s.partIds.addArray(extraIds);
        if (everything) s.partIds.clear();   // the whole visible model
        juce::String ignored;
        exporting::qualityPreset(quality.getSelectedId() == 2 ? "fine" : quality.getSelectedId() == 3 ? "custom" : "standard", s.quality, ignored);
        if (quality.getSelectedId() == 3) { s.quality.chordalMm = number(chordal); s.quality.angularDeg = number(angular); }
        return s;
    }

    void formatChanged()
    {
        const auto f = currentFormat();
        const bool mesh = f != CadExportFormat::step;
        for (juce::Component* c : { (juce::Component*) &quality, (juce::Component*) &qualityLabel, (juce::Component*) &chordal, (juce::Component*) &angular, (juce::Component*) &chordalUnit, (juce::Component*) &angularUnit }) c->setVisible(mesh);
        formatNote.setText(f == CadExportFormat::threeMF ? "Millimetre units; one named object per part, placed by its build transform. Slicer settings stay in the slicer."
                           : f == CadExportFormat::stl ? "Binary STL has no units: coordinates are millimetres. Several parts give one file per part (<name>-<part>.stl); use 3MF to keep an assembly in one file."
                           : f == CadExportFormat::glb ? "glTF 2.0 Binary: metre units (converted from mm), indexed triangles, vertex normals, separate named nodes, assembly hierarchy and joint pivot frames preserved."
                           : "Exact B-rep solids in millimetres with part names and placements. Feature history, scripts, constraints and simulation are not transferred.", juce::dontSendNotification);
        const auto current = juce::File::isAbsolutePath(path.getText().trim()) ? juce::File(path.getText().trim()) : juce::File();
        if (current != juce::File()) path.setText(current.withFileExtension(exporting::extension(f)).getFullPathName(), juce::dontSendNotification);
        refreshSummary();
    }

    void qualityChanged()
    {
        const bool custom = quality.getSelectedId() == 3;
        if (!custom)
        {
            CadMeshQuality q; juce::String ignored;
            exporting::qualityPreset(quality.getSelectedId() == 2 ? "fine" : "standard", q, ignored);
            chordal.setText(juce::String(q.chordalMm), juce::dontSendNotification); angular.setText(juce::String(q.angularDeg), juce::dontSendNotification);
        }
        chordal.setEnabled(custom); angular.setEnabled(custom);
        refreshSummary();
    }

    void refreshSummary()
    {
        if (job != nullptr) return;
        CadExportPlan preview; juce::String error;
        auto s = settings();
        int checked = extraIds.size();
        for (auto* t : partToggles) if (t->getToggleState()) ++checked;
        if (checked == 0) { summary.setText("Millimetres (mm). No parts checked.", juce::dontSendNotification); exportButton.setEnabled(false); return; }
        if (!service->planExport(s, preview, error)) { summary.setText("Millimetres (mm).", juce::dontSendNotification); status.setText(error, juce::dontSendNotification); status.setColour(juce::Label::textColourId, kError); exportButton.setEnabled(false); return; }
        summary.setText("Millimetres (mm). " + preview.scope + ", overall " + juce::String(preview.max[0] - preview.min[0], 2) + " x " + juce::String(preview.max[1] - preview.min[1], 2)
                        + " x " + juce::String(preview.max[2] - preview.min[2], 2) + " mm (X x Y x Z).", juce::dontSendNotification);
        status.setText({}, juce::dontSendNotification);
        exportButton.setEnabled(true);
    }

    void choose(bool thenExport)
    {
        const auto f = currentFormat();
        const auto current = juce::File::isAbsolutePath(path.getText().trim()) ? juce::File(path.getText().trim()) : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("model");
        chooser = std::make_unique<juce::FileChooser>("Export " + exporting::formatName(f), current.withFileExtension(exporting::extension(f)), "*" + exporting::extension(f));
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,   // replacing is confirmed in startExport
                             [safe = juce::Component::SafePointer<ExportDialog>(this), thenExport](const juce::FileChooser& c)
                             {
                                 if (safe == nullptr || c.getResult() == juce::File()) return;
                                 safe->path.setText(c.getResult().getFullPathName(), juce::dontSendNotification);
                                 if (thenExport) safe->startExport();
                             });
    }

    void startExport()
    {
        if (job != nullptr) return;
        const auto text = path.getText().trim();
        if (!juce::File::isAbsolutePath(text)) { choose(true); return; }
        auto s = settings();
        s.output = juce::File(text);
        s.overwrite = true;   // existing destinations are listed and confirmed below
        CadExportPlan plan; juce::String error;
        if (!service->planExport(s, plan, error)) { showStatus(error, kError); return; }
        juce::StringArray existing;
        for (const auto& file : plan.outputs) if (file.exists()) existing.add(file.getFullPathName());
        if (!existing.isEmpty())
        {
            auto shared = std::make_shared<CadExportPlan>(std::move(plan));
            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon, "Replace existing files?",
                                               "These files already exist and will be replaced only after the new export completes:\n" + existing.joinIntoString("\n"),
                                               "Replace", "Cancel", this,
                                               juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<ExportDialog>(this), shared](int choice)
                                               {
                                                   if (safe != nullptr && choice == 1) safe->run(std::move(*shared));
                                               }));
            return;
        }
        run(std::move(plan));
    }

    void run(CadExportPlan plan)
    {
        if (job != nullptr) return;
        setRunning(true);
        job = std::make_unique<ExportJob>(std::move(plan), [safe = juce::Component::SafePointer<ExportDialog>(this)] { if (safe != nullptr) safe->finished(); });
        job->startThread();
        startTimerHz(10);
    }

    void finished()
    {
        stopTimer();
        const auto result = job->result;
        job.reset();
        setRunning(false);
        if (result.ok)
        {
            auto text = "Saved " + juce::String(result.files.size()) + (result.files.size() == 1 ? " file:\n" : " files:\n") + result.files.joinIntoString("\n");
            if (!result.warnings.isEmpty()) text += "\nWarnings:\n" + result.warnings.joinIntoString("\n");
            showStatus(text, result.warnings.isEmpty() ? kGood : kText);
        }
        else showStatus(result.error, result.cancelled ? kText : kError);
    }

    void timerCallback() override
    {
        if (job == nullptr) return;
        progressValue = job->progress.load();
        if (!job->cancel) status.setText(job->currentStage(), juce::dontSendNotification);
    }

    void setRunning(bool running)
    {
        for (juce::Component* c : { (juce::Component*) &format, (juce::Component*) &quality, (juce::Component*) &chordal, (juce::Component*) &angular, (juce::Component*) &path,
                                    (juce::Component*) &chooseButton, (juce::Component*) &exportButton, (juce::Component*) &partsView, (juce::Component*) &allButton, (juce::Component*) &noneButton })
            c->setEnabled(!running);
        if (!running) qualityChanged();
        closeButton.setButtonText(running ? "Cancel" : "Close");
        progressValue = 0.0;
        bar.setVisible(running);
        if (running) showStatus("Preparing...", kText);
    }

    void showStatus(const juce::String& text, juce::Colour colour) { status.setText(text, juce::dontSendNotification); status.setColour(juce::Label::textColourId, colour); }

    void close() { if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState(0); }   // deletes the window

    std::shared_ptr<CadCommandService> service;
    juce::Label formatLabel { {}, "Format" }, partsLabel { {}, "Parts" }, unitsLabel { {}, "Units and size" }, placementLabel { {}, "Placement" },
                qualityLabel { {}, "Mesh quality" }, outputLabel { {}, "Output" }, summary, placementNote, formatNote, status, chordalUnit, angularUnit;
    juce::ComboBox format, quality;
    juce::TextEditor chordal, angular, path;
    juce::OwnedArray<juce::ToggleButton> partToggles;
    juce::StringArray extraIds;
    juce::Component partsContent;
    juce::Viewport partsView;
    juce::TextButton allButton { "All" }, noneButton { "None" }, chooseButton { "Choose..." }, exportButton { "Export" }, closeButton { "Close" };
    double progressValue = 0.0;
    juce::ProgressBar bar { progressValue };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<ExportJob> job;
};
}

void showCadExportDialog(std::shared_ptr<djehuti::cad::CadCommandService> service, const juce::StringArray& preselected)
{
    if (service == nullptr) return;
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new ExportDialog(std::move(service), preselected));
    options.dialogTitle = "Export";
    options.dialogBackgroundColour = kBackground;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}
