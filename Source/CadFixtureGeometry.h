#pragma once
#include "CadDocument.h"

namespace djehuti::cad::fixturegeom
{
constexpr double attachmentToleranceMm=1.0e-5;
// Exactly one face carrying the persistent token, and distance to the trimmed face (not its infinite surface).
bool face(const CadDocument&, const juce::String& feature, const juce::String& token,
          TopoDS_Face&, gp_Pnt& centre, bool& planar, juce::String& error);
bool onFace(const TopoDS_Face&, const gp_Pnt&);
gp_Trsf featurePlacement(const CadFeature&);
}
