#pragma once
#include "CadComponents.h"

namespace djehuti::cad
{
class CadDocument;
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
}
namespace djehuti::cad
{
bool runCadCraftCheck(juce::var& report);
bool runCadCraftPhase2Check(juce::var& report);
}
