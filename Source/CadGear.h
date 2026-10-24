#pragma once

#include <JuceHeader.h>
#include <TopoDS_Shape.hxx>

#include <array>
#include <utility>
#include <vector>

// Parametric gear geometry. Every gear family is one document feature type ("gear") stored through the shared
// modeling command boundary (CadModeling.cpp), so OCAF stays authoritative for definitions, undo and files.
// parameters.family selects the tooth-geometry builder. Only "spur" (external involute spur gears) exists;
// bevel and worm families need their own builders and are refused by validate() until they are implemented.
namespace djehuti::cad::gear
{
inline bool isGearType(const juce::String& type) { return type == "gear"; }

// Default single spur gear definition (millimetres, degrees, dimensionless coefficients).
juce::var defaults();
bool validate(const juce::var& parameters, juce::String& error);

// Derived geometry of an external involute spur gear. Lengths in millimetres, angles in radians.
struct Spur
{
    int teeth = 0;
    double module = 0, pressureAngle = 0, faceWidth = 0, boreDiameter = 0, backlash = 0, profileShift = 0;
    double addendumCoefficient = 1, dedendumCoefficient = 1.25, rootFilletCoefficient = 0;
    double pitchRadius = 0, baseRadius = 0, tipRadius = 0, rootRadius = 0;
    double toothThickness = 0;         // circular thickness on the reference pitch circle, after backlash
    double baseHalfAngle = 0;          // tooth half-thickness angle on the base circle
    double involuteStartRadius = 0;    // max(base, root): the involute flank runs from here to the tip
    double tipLand = 0;                // circular tooth thickness on the tip circle
    double rootGap = 0;                // circular space width on the root circle
    double rootFillet = 0;             // effective root fillet radius actually applied
    double undercutTeeth = 0;          // tooth count below which a generated tooth would be undercut
    juce::StringArray warnings;
    // Tooth half-thickness angle at a radius: constant (radial flank) below the base circle.
    double halfAngle(double radius) const;
};
double involute(double angle);
bool spur(const juce::var& parameters, Spur& result, juce::String& error);
juce::var describe(const Spur& gear);

using FaceNames = std::vector<std::pair<TopoDS_Shape, juce::StringArray>>;
// Builds the solid in its local frame: axis +Z through the origin, faces from z = 0 to z = faceWidth, tooth 0
// centred on +X. Face tokens are "<owner>/start|end|bore|tip:i|root:i|flank:i:left|right|radial:...|fillet:...".
TopoDS_Shape build(const juce::var& parameters, const juce::String& owner, FaceNames& names, juce::String& error);

// External spur pair on parallel shafts. Profile shifts are honoured through the working pressure angle:
// inv(aw) = inv(a) + 2 tan(a) (x1 + x2) / (z1 + z2), centre distance = m (z1 + z2) / 2 * cos(a) / cos(aw).
struct Mesh
{
    double centreDistance = 0, workingPressureAngle = 0, ratio = 0, contactRatio = 0;
    double tipClearanceA = 0, tipClearanceB = 0;   // A's tip to B's root circle, B's tip to A's root circle
};
// Meshing values of an external pair. problems: conditions under which the teeth cannot mesh correctly
// (different module or pressure angle, involute interference, tip/root contact, contact ratio below 1).
bool mesh(const Spur& a, const Spur& b, Mesh& result, juce::StringArray& problems, juce::StringArray& warnings);

juce::var pairDefaults();
struct PairPlan
{
    juce::var gearA, gearB;            // complete gear definitions (pair links filled in by the document)
    Spur a, b;
    Mesh values;
    juce::String nameA, nameB;
    std::array<double, 3> centreA {}, centreB {};
    double rotationA = 0, rotationB = 0;           // degrees about +Z (feature placement rz)
    juce::String mechanism = "none";               // "none" | "create" (| "keep" for updates)
    juce::StringArray warnings;
    juce::var toJson() const;
};
// Validates a pair request (pairDefaults() fields; unknown fields refused) and computes the compatible pair.
bool planPair(const juce::var& request, PairPlan& plan, juce::String& error);
// Rotation of gear B (degrees) that centres one of its tooth spaces on the line toward gear A when gear A has
// a tooth centred on the line of centres at lineDegrees.
double meshRotationDegrees(int teethB, double lineDegrees);
}
