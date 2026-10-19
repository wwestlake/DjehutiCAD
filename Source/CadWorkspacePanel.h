#pragma once

#include <JuceHeader.h>
#include "CadDocument.h"
#include "Viewport3D.h"
#include "CadMotionRuntime.h"
#include "MotionLink.h"
#include "CadSelection.h"
#include "CadInterference.h"
#include "WorkspaceMenu.h"

// Mechanical CAD workspace: model tree on the left, the model view or active sketch in the centre, and a
// contextual task panel on the right. All modeling goes through the shared CadCommandService.
class CadWorkspacePanel final : public juce::Component,
                                private juce::Timer,
                                private juce::AsyncUpdater
{
public:
    explicit CadWorkspacePanel(std::shared_ptr<djehuti::cad::CadCommandService> service);
    ~CadWorkspacePanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void newDocument();
    void openDocument();
    void saveDocument();
    void saveDocumentAs();
    // Shared Export... dialog; the selected body or instance is checked initially, otherwise the whole visible model.
    void showExportDialog();
    juce::String layoutState() const;
    void restoreLayoutState(const juce::String& state);
    void setMotion(std::shared_ptr<djehuti::cad::CadMotionRuntime> runtime, djehuti::motion::MotionLink* link);
    void setSelection(std::shared_ptr<djehuti::cad::CadSelection> shared);
    std::function<void()> onOpenViews;
    WorkspaceMenu workspaceMenu;
    juce::String viewState() const { return juce::JSON::toString(modelView.viewState()); }
    void restoreViewState(const juce::String& json) { modelView.restoreViewState(juce::JSON::parse(json)); }
    void viewContextCommand(const juce::String&, const djehuti::cad::CadPick&);
    bool executeUiCommand(const juce::String& command);
    bool uiToolActive(const juce::String& command) const;
    bool uiCommandEnabled(const juce::String& command) const;
    // Shared by HTTP, BYOK and the FRust/node workspace command. Message thread only.
    juce::var automation(const juce::var& arguments);
    juce::var automationState();
    // Shared interference inspection (UI command and cad_check_interference). referencePose inspects the saved
    // placements instead of the pose shown and is not displayed. Returns the structured report.
    juce::var checkInterference(bool referencePose);
    void clearInterference();

private:
    bool automationInProgress = false;
    void postUiAction(std::function<void()> action) { if (automationInProgress) action(); else juce::MessageManager::callAsync(std::move(action)); }
    enum class Task { none, body, sketch, createSketch, sketchEdit, sketchEntity, newBox, newCylinder, extrude, cut, fillet, boolean, instance, joint, newInstance, newJoint, link, coupling, newCoupling, ik, subshape, interference };
    class FeatureItem;
    class GroupItem;
    class SketchCanvas;
    class TaskForm;

    void timerCallback() override;
    void wakeFrames();
    void handleAsyncUpdate() override;
    void refreshFromDocument(bool force = false);
    void refreshTree();
    void syncTreeSelection();
    void featureClicked(const juce::String& id);
    void bodyPicked(const juce::String& id);
    void selectFeature(const juce::String& id);
    void selectSketchEntity(const juce::String& id, bool second);
    void startTask(Task next, const juce::String& kind = {});
    void requestRebuild(bool keepValues);
    void rebuildTaskPanel(bool keepValues);
    void assignRole(const juce::String& id);
    void validateTask();
    void acceptTask();
    void cancelTask();
    void enterSketch(const juce::String& sketchId);
    void finishSketch();
    void setSketchTool(const juce::String& tool);
    void chooseConstraint();
    void addConstraint();
    void sketchGeometryAdded(const juce::String& what);
    void updateToolbar();
    void updateHighlights();
    void setMessage(const juce::String& text, int severity);
    void showError(const juce::String& error) { setMessage(error, 2); }
    void showFeatureMenu(const juce::String& id);
    void toggleVisibility(const juce::String& id);
    void deleteFeature(const juce::String& id);
    bool isSolid(const juce::String& id) const;
    juce::String featureName(const juce::String& id) const;
    juce::Colour colourFor(const juce::String& id) const;
    juce::String applicableConstraintTypes() const;
    bool isInstance(const juce::String& id) const;
    bool isJoint(const juce::String& id) const;
    bool isCoupling(const juce::String& id) const;
    void addCouplingFields(const djehuti::cad::CadCoupling* existing);
    bool readCoupling(djehuti::cad::CadCoupling& coupling, juce::String& error) const;
    void addMechanismStatus();
    void mechanismChanged();
    bool isCommandTask() const;
    bool jointReferenceFrame(const juce::String& jointId, gp_Trsf& world) const;
    void addMotionPanels(juce::String& title, juce::String& instruction, juce::String& accept, bool& showAccept, bool& showCancel);
    bool acceptMotionTask();
    void validateMotionTask(juce::String& problem, int& severity);
    void saveMapping();
    void previewJoint(bool reset);
    void sendLifecycle(const juce::String& command);
    void linkChanged();
    // Inverse kinematics task panel. The draft is the edited (possibly unsaved) task; Save stores it.
    bool isIkTask(const juce::String& id) const;
    juce::String supportText(const djehuti::cad::CadSketch& sketch) const;
    // The selected face, if it can support a sketch (planar and persistently named); otherwise why not.
    bool selectedSupport(djehuti::cad::CadFaceInfo& face, juce::String& reason) const;
    void selectionChanged();
    void addSubshapePanel(juce::String& title, juce::String& instruction);
    void loadIkDraft(const juce::String& taskId);
    void addIkPanel(juce::String& title, juce::String& instruction, juce::String& accept, bool& showAccept, bool& showCancel);
    void addInterferencePanel(juce::String& title, juce::String& instruction);
    void markInterferenceStale();
    void validateIkPanel(juce::String& problem, int& severity);
    void readIkForm();
    void showIkMarkers();
    void solveIkDraft();
    bool saveIkDraft();
    void captureIkTarget();
    void ikTargetDragged(const juce::String& targetId, const std::array<float, 3>& point, bool finished);

    std::shared_ptr<djehuti::cad::CadCommandService> service;
    juce::TooltipWindow tooltipWindow { this, 600 };
    juce::Label modeLabel, statusLabel, unitsLabel, treeHeading;
    juce::TextButton sketchButton { "Create Sketch" }, boxButton { "Box" }, cylinderButton { "Cylinder" }, extrudeButton { "Extrude" }, cutButton { "Cut Sketch" },
        filletButton { "Fillet" },
        subtractButton { "Subtract" }, unionButton { "Union" }, intersectButton { "Intersect" }, undoButton { "Undo" }, redoButton { "Redo" },
        fitButton { "Fit All" }, resetViewButton { "Reset View" }, viewsButton { "3D Views" },
        selectToolButton { "Select" }, lineButton { "Line" }, rectangleButton { "Rectangle" }, circleButton { "Circle" }, constrainButton { "Constrain" },
        fitSketchButton { "Fit Sketch" }, finishSketchButton { "Finish Sketch" },
        instanceButton { "Instance" }, jointButton { "Joint" }, couplingButton { "Coupling" }, linkButton { "Motion Link" }, ikButton { "IK" },
        bodyModeButton { "Body" }, faceModeButton { "Face" }, edgeModeButton { "Edge" };
    juce::TreeView tree { "Model" };
    std::unique_ptr<juce::TreeViewItem> rootItem;
    juce::Component centre, side;
    Viewport3D modelView { 0 };
    std::unique_ptr<SketchCanvas> sketchCanvas;
    juce::Label taskTitle, taskInstruction, messageLabel;
    juce::Viewport taskViewport { "CAD Task" };
    std::unique_ptr<TaskForm> form;
    juce::TextButton acceptButton { "Apply" }, cancelButton { "Cancel" };
    juce::StretchableLayoutManager columns;
    juce::StretchableLayoutResizerBar leftBar { &columns, 1, true }, rightBar { &columns, 3, true };

    Task task = Task::none;
    juce::String booleanKind, sketchPlane, formContext;
    juce::String selectedId, editingSketchId, sketchEntityId, sketchSecondEntityId;
    juce::String profileId, targetId, toolId;
    std::vector<int> filletEdges;
    double filletRadius = 2.0;
    juce::String instanceBodyId, jointParentId, jointChildId, couplingJointA, couplingJointB, shownMechanismSummary;
    std::shared_ptr<djehuti::cad::CadMotionRuntime> motionRuntime;
    djehuti::cad::CadInterferenceReport interferenceReport;   // last displayed inspection (pose shown)
    bool interferenceActive = false, interferenceStale = false;
    djehuti::motion::MotionLink* motionLink = nullptr;
    std::uint64_t shownLinkSerial = 0;
    std::shared_ptr<djehuti::cad::CadSelection> selection;
    std::unique_ptr<djehuti::cad::CadIkTask> ikDraft;
    int ikTargetIndex = 0;
    bool ikDirty = false, ikDragEnabled = false, ikMarkersShown = false, ikFormStale = false;   // stale: the form still shows another task or target
    juce::String ikDragPlane = "XY";
    bool ikSolvePending = false;   // a Solve or drag request awaits its outcome message
    std::uint64_t ikDocument = 0;   // document instance the draft belongs to; New/Open discards it
    int activeRole = 0;
    juce::String commandMessage, validationMessage;
    int commandSeverity = 0, validationSeverity = 0;
    bool pendingKeepValues = true, updatingTree = false;
    double boxSize[3] { 20.0, 20.0, 10.0 }, cylinderSize[2] { 5.0, 20.0 }, extrudeDistance = 10.0, cutDepth = 10.0;
    std::uint64_t shownRevision = 0, shownDocument = 0, frameSerial = 0;
    int idleFrameTicks = 0;
    bool fastFrames = true;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CadWorkspacePanel)
};
