#pragma once

#include <JuceHeader.h>
#include <TopoDS_Shape.hxx>

#include <utility>
#include <vector>

// Parametric DIN-rail system. Four document feature types, all stored through the shared modeling command boundary
// (CadModeling.cpp) so OCAF stays authoritative for definitions, undo and files:
//   din_rail    TS35 x 7.5 top-hat rail; parameter lengthMm.
//   din_bottom  The actual bottom of a DIN-rail module: lengthMm (along the rail), widthMm (across the rail),
//               thicknessMm, optional edge tabs, and the common clip pocket. Nothing above the bottom is generated.
//   din_clip    The replaceable mounting foot: fixed hook, spring-clip arm, release lug and friction-screw block.
//   din_screw   The nylon friction-adjustment screw; parameter advanceMm.
// Common reference (module frame): origin at the alignment corner, +X along the rail, +Y across it, +Z up. The
// rail-contact (flange) plane is z = 0 and the rail axis lies railCentreFromEdge in front of the alignment edge y = 0.
// The clip pocket, clip and screw sit at fixed coordinates in this frame, so resizing a bottom never changes the
// mounting geometry. The rail is modelled with its axis on y = 0; a module is placed at y = railY - railCentreFromEdge.
namespace djehuti::cad::din
{
inline bool isDinType(const juce::String& type) { return type == "din_rail" || type == "din_bottom" || type == "din_clip" || type == "din_screw"; }

constexpr double railCentreFromEdge = 31.0;   // alignment edge (y = 0) to the rail axis, mm
constexpr double minLength = 28.0, minWidth = 60.0, minThickness = 3.0;

juce::var defaults(const juce::String& type);
bool validate(const juce::String& type, const juce::var& parameters, juce::String& error);

using FaceNames = std::vector<std::pair<TopoDS_Shape, juce::StringArray>>;
// Builds the solid in its local frame. Face tokens: "<owner>/top|bottom|xmin|xmax|ymin|ymax" on the bottom plate,
// "<owner>/start|end|top" on the rail.
TopoDS_Shape build(const juce::String& type, const juce::var& parameters, const juce::String& owner, FaceNames& names, juce::String& error);

// Rail dimensions, mounting reference, supported ranges and the provenance of every dimension.
juce::var standard();
}
