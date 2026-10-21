#pragma once
#include "CadComponents.h"

namespace djehuti::cad::craft
{
bool isStock(const juce::var& record);
juce::StringArray dimensionKeys(const juce::String& profile);
juce::String dimensionLabel(const juce::String& key);
bool validateGeometry(const juce::var& geometry, juce::String& error);
TopoDS_Shape shape(const juce::var& geometry, juce::String& error);
juce::Array<juce::var> templates();
}
namespace djehuti::cad { bool runCadCraftCheck(juce::var& report); }
