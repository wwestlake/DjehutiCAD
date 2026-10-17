#pragma once

#include <JuceHeader.h>
#include "CadDocument.h"
#include "Viewport3D.h"

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
    juce::String layoutState() const;
    void restoreLayoutState(const juce::String& state);
    std::function<void()> onOpenViews;

private:
    enum class Task { none, body, sketch, createSketch, sketchEdit, sketchEntity, newBox, newCylinder, extrude, cut, boolean };
    class FeatureItem;
    class GroupItem;
    class SketchCanvas;
    class TaskForm;

    void timerCallback() override;
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

    std::shared_ptr<djehuti::cad::CadCommandService> service;
    juce::TooltipWindow tooltipWindow { this, 600 };
    juce::Label modeLabel, statusLabel, unitsLabel, treeHeading;
    juce::TextButton sketchButton { "Create Sketch" }, boxButton { "Box" }, cylinderButton { "Cylinder" }, extrudeButton { "Extrude" }, cutButton { "Cut Sketch" },
        subtractButton { "Subtract" }, unionButton { "Union" }, intersectButton { "Intersect" }, undoButton { "Undo" }, redoButton { "Redo" },
        fitButton { "Fit All" }, resetViewButton { "Reset View" }, viewsButton { "3D Views" },
        selectToolButton { "Select" }, lineButton { "Line" }, rectangleButton { "Rectangle" }, circleButton { "Circle" }, constrainButton { "Constrain" },
        fitSketchButton { "Fit Sketch" }, finishSketchButton { "Finish Sketch" };
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
    int activeRole = 0;
    juce::String commandMessage, validationMessage;
    int commandSeverity = 0, validationSeverity = 0;
    bool pendingKeepValues = true, updatingTree = false;
    double boxSize[3] { 20.0, 20.0, 10.0 }, cylinderSize[2] { 5.0, 20.0 }, extrudeDistance = 10.0, cutDepth = 10.0;
    std::uint64_t shownRevision = 0, frameSerial = 0;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CadWorkspacePanel)
};
