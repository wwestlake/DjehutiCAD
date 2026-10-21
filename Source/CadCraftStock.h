#pragma once
#include "CadComponents.h"
#include "CadMaterials.h"

namespace djehuti::cad
{
class CadDocument;
struct CadBondedJoint;
}

namespace djehuti::cad::craft
{
bool isStock(const juce::var& record);
juce::StringArray dimensionKeys(const juce::String& profile);
juce::String dimensionLabel(const juce::String& key);
bool validateGeometry(const juce::var& geometry, juce::String& error);
TopoDS_Shape shape(const juce::var& geometry, juce::String& error);
juce::Array<juce::var> templates();

// Phase 2: Cutting, alignment, and cut lists
bool validateCut(const juce::var& cut, const juce::var& geometry, juce::String& error);
juce::var cutList(const CadDocument& document);

// Phase 3: Adhesives, bonded joints, and connectivity
bool evaluateBondedJoint(const CadDocument& document, CadBondedJoint& joint);
bool reevaluateAllBondedJoints(CadDocument& document, juce::String& error);
std::vector<juce::String> getConnectedBondedGroup(const CadDocument& document, const juce::String& instanceId);
juce::var adhesiveBom(const CadDocument& document);
TopoDS_Shape createFilletBeadSolid(const CadDocument& document, const CadBondedJoint& joint, juce::String& error);

// Phase 4: Structural materials & analysis
std::vector<CadMaterial> illustrativeCraftMaterials();
}
namespace djehuti::cad
{
bool runCadCraftCheck(juce::var& report);
bool runCadCraftPhase2Check(juce::var& report);
bool runCadCraftPhase3Check(juce::var& report);
bool runCadCraftPhase4Check(juce::var& report);
}
