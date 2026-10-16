#pragma once

#include <JuceHeader.h>
#include "CadDocument.h"

class CadWorkspacePanel final : public juce::Component
{
public:
    explicit CadWorkspacePanel(std::shared_ptr<djehuti::cad::CadCommandService> service);
    void paint(juce::Graphics&) override;
    void resized() override;
    void newDocument();
    void openDocument();
    void saveDocument();
    void saveDocumentAs();

private:
    class FeatureItem;
    void refreshTree();
    void selectFeature(const juce::String& id);
    void applyProperties();
    void showError(const juce::String& error);
    bool selectedPair(juce::String& first, juce::String& second) const;

    std::shared_ptr<djehuti::cad::CadCommandService> service;
    juce::TreeView tree { "Model" };
    juce::TextEditor name, width, depth, height, radius, x, y, z, firstOperand, secondOperand;
    juce::Label title, status, units;
    juce::TextButton boxButton { "Box" }, cylinderButton { "Cylinder" }, subtractButton { "Subtract" }, unionButton { "Union" }, intersectButton { "Intersect" }, applyButton { "Apply" }, undoButton { "Undo" }, redoButton { "Redo" }, hideButton { "Hide/Show" }, deleteButton { "Delete" }, useFirstButton { "Use Selected as First" }, useSecondButton { "Use Selected as Second" };
    juce::String selectedId;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CadWorkspacePanel)
};
