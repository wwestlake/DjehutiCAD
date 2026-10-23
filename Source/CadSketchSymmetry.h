#pragma once
#include "CadDocument.h"
namespace djehuti::cad
{
juce::var sketchSymmetryToJson(const CadSketch&);
bool readSketchSymmetry(CadSketch&,const juce::var&,juce::String&);
std::vector<CadSketchEntity> reflectedSketchEntities(const CadSketch&,const CadSketchMirror&,juce::String&);
}
