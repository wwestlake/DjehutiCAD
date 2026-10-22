#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <optional>
#include <map>

namespace djehuti::cad
{
class CadDocument;

namespace structural
{
struct StructuralResult;
struct ExperimentComparison;
}

namespace kit
{

struct CadCraftKitMetadata
{
    juce::String name = "Craft Construction Kit";
    juce::String description = "Parametric craft construction kit with fabrication and testing documentation";
    juce::String author = "DjehutiCAD Educational Designer";
    juce::String audience = "STEM Workshop & Classroom Mechanics";
    juce::String revision = "1.0";
    juce::String notes;
    std::vector<juce::String> customAssemblySteps;

    juce::var toVar() const;
    static CadCraftKitMetadata fromVar(const juce::var& v);
};

juce::var toJson(const CadCraftKitMetadata& meta);
bool fromJson(const juce::var& json, CadCraftKitMetadata& meta, juce::String& error);

// ---------------------------------------------------------------------------------------------------------------
// Consolidated Bill of Materials
// ---------------------------------------------------------------------------------------------------------------
struct CadKitBOMItem
{
    juce::String itemType; // "fabricatedStock", "purchasedPart", "consumableAdhesive"
    juce::String id;
    juce::String name;
    juce::String material;
    juce::String profile;
    int quantity = 0;
    double lengthMm = 0.0;
    double widthMm = 0.0;
    double thicknessMm = 0.0;
    double totalLengthMm = 0.0;
    double totalVolumeMm3 = 0.0;
    double estimatedMassG = 0.0;
    juce::String notes;

    juce::var toVar() const;
};

struct CadKitBOM
{
    std::vector<CadKitBOMItem> items;
    int totalFabricatedParts = 0;
    int totalPurchasedComponents = 0;
    int totalAdhesiveJoints = 0;
    double totalAdhesiveVolumeMl = 0.0;
    double totalStructureMassG = 0.0;

    juce::var toVar() const;
    juce::String toMarkdown() const;
    juce::String toCSV() const;
};

// ---------------------------------------------------------------------------------------------------------------
// Stock Cutting Optimization & Nesting
// ---------------------------------------------------------------------------------------------------------------
struct StockPieceCut
{
    juce::String instanceId;
    juce::String partName;
    double cutLengthMm = 0.0;
    double positionOnStockMm = 0.0;
    juce::String cutStartNote;
    juce::String cutEndNote;
};

struct StockPieceNesting
{
    int pieceIndex = 1;
    juce::String material;
    juce::String profile;
    double rawStockLengthMm = 0.0;
    double widthMm = 0.0;
    double thicknessMm = 0.0;
    std::vector<StockPieceCut> cuts;
    double usedLengthMm = 0.0;
    double kerfAllowanceMm = 0.0;
    double offcutWasteMm = 0.0;
    double utilizationPercent = 0.0;
};

struct CadStockNestingPlan
{
    std::vector<StockPieceNesting> pieces;
    int totalRawPiecesRequired = 0;
    double totalRawLengthMm = 0.0;
    double totalCutLengthMm = 0.0;
    double totalWasteLengthMm = 0.0;
    double overallUtilizationPercent = 0.0;

    juce::var toVar() const;
    juce::String toMarkdown() const;
    juce::String toCSV() const;
};

// ---------------------------------------------------------------------------------------------------------------
// Assembly Guide & Construction Sequence
// ---------------------------------------------------------------------------------------------------------------
struct CadAssemblyStep
{
    int stepNumber = 1;
    juce::String title;
    juce::String phase; // "Preparation", "Subassembly", "Bonding", "Final Assembly", "Testing"
    std::vector<juce::String> targetInstances;
    std::vector<juce::String> targetJoints;
    juce::String instructions;
    juce::String safetyNotes;

    juce::var toVar() const;
};

struct CadAssemblyGuide
{
    std::vector<CadAssemblyStep> steps;
    int totalSteps = 0;

    juce::var toVar() const;
    juce::String toMarkdown() const;
};

// ---------------------------------------------------------------------------------------------------------------
// Generator Functions
// ---------------------------------------------------------------------------------------------------------------
CadKitBOM generateBOM(const CadDocument& doc);
CadStockNestingPlan generateCuttingOptimization(const CadDocument& doc, double sawKerfMm = 1.0);
CadAssemblyGuide generateAssemblyGuide(const CadDocument& doc, const CadCraftKitMetadata* meta = nullptr);
juce::String generateFabricationTemplateSVG(const CadDocument& doc, const CadStockNestingPlan& plan);
juce::String generateEducationalProjectReport(const CadDocument& doc,
                                              const CadCraftKitMetadata& kitMeta,
                                              const structural::StructuralResult* analysisResult = nullptr,
                                              const structural::ExperimentComparison* labComparison = nullptr);

bool exportCompleteProjectKit(const CadDocument& doc,
                              const CadCraftKitMetadata& kitMeta,
                              const juce::File& outputFolder,
                              juce::String& error,
                              const structural::StructuralResult* analysisResult = nullptr,
                              const structural::ExperimentComparison* labComparison = nullptr);

} // namespace kit
} // namespace djehuti::cad
