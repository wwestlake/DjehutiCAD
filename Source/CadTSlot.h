#pragma once

#include <JuceHeader.h>
#include <TopoDS_Shape.hxx>

#include <utility>
#include <vector>

// Parametric T-slot structural system. Features are stored through the shared modeling command boundary
// (CadModeling.cpp) so OCAF stays authoritative for definitions, undo and files. Current types:
//   tslot_half  One printed piece of a two-half rail (top or bottom half, x0Mm..x1Mm), generated from a tslot_rail
//               whose construction is twoHalf. Pieces carry the snap arms (top) and sockets (bottom).
//   tslot_rail  A rail running along +X from x = 0 to lengthMm, cross-section centred on the rail axis (y = z = 0).
//               Width is along Y, height along Z. Faces: top +Z, bottom -Z, left -Y, right +Y.
namespace djehuti::cad::tslot
{
inline bool isTSlotType(const juce::String& type) { return type == "tslot_rail" || type == "tslot_half"; }

constexpr double minWeb = 1.6;   // least material kept between any two cavities and between a cavity and the outside, mm

juce::var defaults(const juce::String& type);
bool validate(const juce::String& type, const juce::var& parameters, juce::String& error);

using FaceNames = std::vector<std::pair<TopoDS_Shape, juce::StringArray>>;
// Face tokens: "<owner>/start" and "<owner>/end" (rail ends), "<owner>/slot:<face>:<i>/floor" (slot floors).
TopoDS_Shape build(const juce::String& type, const juce::var& parameters, const juce::String& owner, FaceNames& names, juce::String& error);

// Documented geometry rules, ranges, defaults and the cross-section properties of the generated profile.
// One printed piece of a two-half rail, with the snap-window start positions (arm roots) that fall inside it.
struct Piece { juce::String half; int index = 0; double x0 = 0, x1 = 0; std::vector<double> windows; };
// Plans the pieces for a twoHalf rail: staggered joints, the end filler and the snap windows. summary reports the section
// count, piece length, overlap and the window length. Fails with a reason when the arrangement cannot hold snap arms.
bool planPieces(const juce::var& railParameters, std::vector<Piece>& pieces, juce::var& summary, juce::String& error);
// Self-contained parameters of one piece (the rail's profile and snap parameters plus half, x0Mm, x1Mm, windows).
juce::var pieceParameters(const juce::var& railParameters, const Piece& piece, const juce::String& railId);

juce::var standard();
// Section properties of a parameter set (area, centroid, second moments about the centroidal axes), computed from the
// actual cross-section outline; fails with an error string when the parameters are invalid.
juce::var sectionProperties(const juce::var& parameters, juce::String& error);
}
