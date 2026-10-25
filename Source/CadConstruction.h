#pragma once

#include <JuceHeader.h>
#include "CadMechanics.h"

#include <array>
#include <vector>

// Document-owned construction references: points, axes, coordinate systems and planes used for placement,
// alignment, rotation, mirroring and scaling. A reference is a CadFrame (millimetres, unit quaternion):
//   point  - the frame origin
//   axis   - the line through the origin along the frame's +Z
//   cs     - the full frame
//   plane  - the plane through the origin with normal +Z
// A reference is either fixed (its frame is the world frame) or associative (it is derived from a source on every
// query, using stable ids; nothing is cached, so a moved or edited source is always followed and a deleted one is
// reported as broken):
//   frame         frame stored relative to the source occurrence/body frame (follows moves and rotations)
//   cylinderAxis  axis of the cylindrical face `face` of the source
//   faceCentre    centre of the face `face` of the source (planar faces also give the face normal)
//   reference     frame stored relative to another reference (offsets; cycles are refused)
namespace djehuti::cad
{
struct CadConstructionRef
{
    juce::String id, name, kind = "point";   // point | axis | cs | plane
    bool visible = true;
    CadFrame frame;                          // fixed: world frame; frame/reference: relative to the source; derived types: unused
    juce::String association = "fixed";      // fixed | frame | cylinderAxis | faceCentre | reference
    juce::String source, face;               // stable feature, occurrence or reference id; persistent face token
};

struct CadResolvedRef
{
    bool ok = false;
    juce::String problem;                    // why the association is broken
    CadFrame world;
};

// A well-defined point on a body that a cursor or reference can snap to (world millimetres).
struct CadSnapPoint
{
    std::array<double, 3> point {};
    juce::String type;                       // vertex | midpoint | centre | faceCentre | axisPoint
    juce::String label, face;                // face token for faceCentre/axisPoint
};

namespace construction
{
inline bool isKind(const juce::String& k) { return k == "point" || k == "axis" || k == "cs" || k == "plane"; }
inline bool isAssociation(const juce::String& a) { return a == "fixed" || a == "frame" || a == "cylinderAxis" || a == "faceCentre" || a == "reference"; }
juce::var toStorage(const CadConstructionRef&);
bool fromStorage(const juce::var&, CadConstructionRef&, juce::String& error);
juce::var listToJson(const std::vector<CadConstructionRef>&);
bool listFromJson(const juce::var&, std::vector<CadConstructionRef>&, juce::String& error);
// World frame for presentation: origin, unit direction (+Z) and rotation in degrees (extrinsic XYZ).
juce::var frameToPresentation(const CadFrame&);
}
}
