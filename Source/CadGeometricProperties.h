#pragma once

#include <JuceHeader.h>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <array>
#include <map>

namespace djehuti::cad
{
class CadDocument;

// Read-only geometric (volume) properties of solid CAD geometry, computed with OCCT BRepGProp.
//
// These are properties of the shape alone, i.e. integrals at unit density. They are NOT physical mass properties:
// multiplying by a density (a later material service) turns volumeMm3 into mass and the inertia integrals into
// mass moments of inertia. Nothing here reports kilograms, newtons or physical inertia.
//
// Units follow the document: millimetres. volume mm^3, centroid mm, inertia integrals mm^5.
//
// Reporting frame: the frame the shape's coordinates are expressed in. For a body that is the world (document)
// frame, right-handed, Z up, with the body's own placement already applied; for an assembly instance it is the
// world frame at the instance's pose (CadDocument::instanceShapeTransform, as export and interference use).
//
// The inertia tensor is taken about the centroid with axes parallel to the reporting frame's axes, in the tensor
// convention (OCCT GProp_GProps::MatrixOfInertia):
//   I_xx = integral (y^2 + z^2) dV,  I_yy = integral (x^2 + z^2) dV,  I_zz = integral (x^2 + y^2) dV,
//   I_xy = I_yx = -integral x y dV,  I_xz = -integral x z dV,  I_yz = -integral y z dV   (x, y, z relative to the centroid)
// Under a rigid motion p' = R p + t the centroid maps as c' = R c + t and the tensor as I' = R I R^T.
// Principal moments are the eigenvalues of I (ascending), principal axes the matching unit eigenvectors.
struct CadGeometricProperties
{
    // "ok", or why nothing was computed: "nullShape", "notSolid", "invalidShape", "nonRigidTransform",
    // "zeroVolume", "unknownFeature", "unknownInstance", "computationFailed".
    juce::String status = "nullShape";
    juce::String message;
    bool ok() const { return status == "ok"; }

    juce::String frame;                 // "shape" (the shape's own coordinates) or "world"
    int solids = 0;                     // closed solids that were integrated
    double volumeMm3 = 0.0;
    gp_Pnt centroidMm;
    std::array<std::array<double, 3>, 3> inertiaMm5 {};       // about the centroid, reporting-frame axes
    std::array<double, 3> principalMomentsMm5 {};             // ascending
    std::array<std::array<double, 3>, 3> principalAxes {};    // principalAxes[i] belongs to principalMomentsMm5[i]

    juce::var toJson() const;
};

namespace geomprops
{
// The shape in its own coordinates. Accepts a solid, or a compound or compsolid made only of solids; anything with
// free shells, faces, edges or vertices outside a solid is refused ("notSolid") rather than partly integrated.
CadGeometricProperties ofShape(const TopoDS_Shape& shape);
// The shape after a rigid transform (rotation plus translation; any scaling or mirroring is refused).
CadGeometricProperties ofShape(const TopoDS_Shape& shape, const gp_Trsf& transform);
// A body's evaluated shape in the world frame (its placement included). Sketches and features without a solid fail.
CadGeometricProperties ofFeature(const CadDocument& document, const juce::String& featureId);
// An assembly instance in the world frame: at its saved reference placement, or at the pose given by instanceWorld
// (instance id -> world frame, e.g. CadMotionRuntime::shownInstanceWorld()).
CadGeometricProperties ofInstance(const CadDocument& document, const juce::String& instanceId,
                                  const std::map<juce::String, gp_Trsf>* instanceWorld = nullptr);
}
}
