#pragma once
#include "CadDocument.h"

namespace djehuti::cad
{
// Shared exact drilling tool for an authored Hole and its transformed feature instances.
TopoDS_Shape makeHoleCutter(const CadHoleParameters&, const CadSketchFrame&, const TopoDS_Shape& extent,
                           const juce::String& owner, CadFaceOrigins&, juce::String& error);
}
