#pragma once
#include "CadDocument.h"
#include "WorkspaceMenu.h"

// Craft controls author the same component definitions/materials used by the CAD workspace.
class CadCraftBrowser final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit CadCraftBrowser(std::shared_ptr<djehuti::cad::CadCommandService>);
    ~CadCraftBrowser() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    WorkspaceMenu workspaceMenu;
    std::function<juce::String()> selectedOccurrence;
    std::function<void(const juce::String&)> onInserted;
    std::function<void()> onMaterials;
    bool craftCheck(juce::var& report, const juce::File& folder);
    bool craftPhase2Check(juce::var& report, const juce::File& folder);
    bool craftPhase3Check(juce::var& report, const juce::File& folder);
private:
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged(int) override;
    void timerCallback() override;
    void reload(const juce::String& select = {});
    void filter();
    void loadTemplate();
    void editSelected();
    void fill(const juce::var&, const juce::String& materialId = {});
    bool readGeometry(juce::var&);
    bool readPlacement(djehuti::cad::CadFrame&);
    void insertPiece();
    void saveTemplate(bool duplicate);
    void applyDimensions();
    void applyMaterial();
    void applyPlacement();
    void linkedCopy();
    void applyCut();
    void clearCuts();
    void applyAlign();
    void groupAssembly();
    void moveAssembly();
    void showCutList();
    void refreshAlignTargets();
    void applyBond();
    void moveBonded();
    void showAdhesiveBom();
    void refreshBondTargets();
    void loadMaterials(const juce::String&);
    juce::var selectedMaterial() const;
    void message(const juce::String&, bool error = false);
    std::shared_ptr<djehuti::cad::CadCommandService> service;
    juce::Viewport scroll;
    juce::Component content;
    juce::ComboBox category, material;
    juce::TextEditor search, details;
    juce::ListBox list {"Craft templates", this};
    juce::Label heading, mode, materialLabel, placementLabel, status;
    juce::OwnedArray<juce::Label> dimensionLabels, placementLabels;
    juce::OwnedArray<juce::TextEditor> dimensions, placements;
    juce::TextButton editPiece {"Edit selected piece"}, templatesButton {"Templates"}, insert {"Insert independent piece"},
        apply {"Apply dimensions"}, assign {"Apply material"}, move {"Apply placement"}, linked {"Linked copy of selected"},
        saveDefaults {"Save template defaults"}, duplicate {"Duplicate template"}, materialsButton {"Materials..."}, reloadButton {"Reload catalog"};
    // Phase 2: Cutting, alignment, assembly, cut list controls
    juce::Label cutHeading, alignHeading, assemblyHeading;
    juce::ComboBox cutType, cutSide, alignTarget, alignType;
    juce::TextEditor cutPosition, cutAngle, assemblyName;
    juce::OwnedArray<juce::TextEditor> alignOffsets;
    juce::TextButton applyCutButton {"Apply cut"}, clearCutsButton {"Clear cuts"},
        alignButton {"Align to target"}, groupButton {"Group assembly"},
        moveAssemblyButton {"Move assembly"}, cutListButton {"Generate cut list"};
    // Phase 3: Adhesives, bonded joints, and connectivity controls
    juce::Label bondHeading;
    juce::ComboBox bondType, bondAdhesive, bondTarget;
    juce::TextEditor bondThickness, bondBeadLeg;
    juce::TextButton createBondButton {"Create bond"}, moveBondedButton {"Move bonded group"},
        reevaluateBondsButton {"Reevaluate bonds"}, adhesiveBomButton {"Adhesive BOM"};
    juce::Array<juce::var> records, materialRecords;
    std::vector<int> shown;
    juce::var draft;
    juce::String editingFeature, editingInstance;
    std::uint64_t documentInstance = 0, materialRevision = 0;
};
namespace djehuti::cad
{
bool runCadCraftUiCheck(juce::var& report, const juce::File& folder, bool interactive = false);
bool runCadCraftPhase2UiCheck(juce::var& report, const juce::File& folder, bool interactive = false);
bool runCadCraftPhase3UiCheck(juce::var& report, const juce::File& folder, bool interactive = false);
}
