#include "CadCraftStock.h"
#include "CadDocument.h"
#include <EngineeringData.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <set>
#include <queue>
#include <cmath>
#include <map>

namespace djehuti::cad::craft
{
bool isStock(const juce::var& record) { return record["geometry"]["kind"].toString() == "craftStock"; }
juce::StringArray dimensionKeys(const juce::String& profile)
{
    if (profile == "rectangular" || profile == "roundedStick") return {"lengthMm", "widthMm", "thicknessMm"};
    if (profile == "rod") return {"lengthMm", "outerDiameterMm"};
    if (profile == "tube") return {"lengthMm", "outerDiameterMm", "innerDiameterMm"};
    return {};
}
juce::String dimensionLabel(const juce::String& key)
{
    if (key == "lengthMm") return "Length (mm)";
    if (key == "widthMm") return "Width (mm)";
    if (key == "thicknessMm") return "Thickness (mm)";
    if (key == "outerDiameterMm") return "Outside diameter (mm)";
    return "Inside diameter (mm)";
}
bool validateCut(const juce::var& cut, const juce::var& geometry, juce::String& error)
{
    const auto fail = [&](const juce::String& reason) { error = reason; return false; };
    if (!cut.isObject()) return fail("Cut specification must be an object.");
    const auto type = cut["type"].toString();
    if (type != "length" && type != "miter" && type != "plane")
        return fail("Unknown cut type: " + type + " (supported: length, miter, plane).");

    const double stockLength = geometry["lengthMm"];

    if (type == "length")
    {
        const auto n = cut["positionMm"];
        if (!(n.isDouble() || n.isInt() || n.isInt64()) || !std::isfinite((double)n) || (double)n <= 0.01 || (double)n >= stockLength)
            return fail("Length cut position must be between 0.01 mm and stock length (" + juce::String(stockLength) + " mm).");
        const auto side = cut.getProperty("side", "end").toString();
        if (side != "start" && side != "end")
            return fail("Cut side must be 'start' or 'end'.");
    }
    else if (type == "miter")
    {
        const auto pos = cut.getProperty("positionMm", 0.0);
        if (!pos.isDouble() && !pos.isInt() && !pos.isInt64()) return fail("Miter position must be numeric.");
        if ((double)pos < 0.0 || (double)pos > stockLength) return fail("Miter position must be along stock length.");
        const auto angle = cut.getProperty("angleDeg", 0.0);
        if (!angle.isDouble() && !angle.isInt() && !angle.isInt64()) return fail("Miter angle must be numeric.");
        if (std::abs((double)angle) > 85.0) return fail("Miter angle must be within +/- 85 degrees.");
        if (cut.hasProperty("bevelDeg"))
        {
            const auto bevel = cut["bevelDeg"];
            if (!bevel.isDouble() && !bevel.isInt() && !bevel.isInt64()) return fail("Bevel angle must be numeric.");
            if (std::abs((double)bevel) > 85.0) return fail("Bevel angle must be within +/- 85 degrees.");
        }
        const auto side = cut.getProperty("side", "end").toString();
        if (side != "start" && side != "end") return fail("Cut side must be 'start' or 'end'.");
    }
    else if (type == "plane")
    {
        for (const auto* k : {"px", "py", "pz", "nx", "ny", "nz"})
            if (!cut.hasProperty(k) || !std::isfinite((double)cut[k])) return fail(juce::String(k) + " must be a finite number.");
        const double mag = std::sqrt((double)cut["nx"] * (double)cut["nx"] + (double)cut["ny"] * (double)cut["ny"] + (double)cut["nz"] * (double)cut["nz"]);
        if (mag < 1e-6) return fail("Cutting plane normal must not be zero.");
    }
    return true;
}
bool validateGeometry(const juce::var& g, juce::String& error)
{
    const auto fail = [&](const juce::String& reason) { error = reason; return false; };
    const auto keys = dimensionKeys(g["profile"].toString());
    if (g["kind"].toString() != "craftStock" || (int)g["schemaVersion"] != 1 || keys.isEmpty()) return fail("Unknown craft stock profile/version.");
    for (const auto& key : keys)
    {
        const auto n = g[juce::Identifier(key)];
        if (!(n.isDouble() || n.isInt() || n.isInt64()) || !std::isfinite((double)n) || (double)n < 0.01 || (double)n > 100000)
            return fail(dimensionLabel(key) + " must be a finite number from 0.01 to 100000 mm.");
    }
    if (g["profile"].toString() == "tube" && (double)g["outerDiameterMm"] - (double)g["innerDiameterMm"] < 0.02)
        return fail("Tube wall must be at least 0.01 mm; inside diameter must be smaller than outside diameter.");
    if (g["profile"].toString() == "roundedStick" && (double)g["lengthMm"] - (double)g["widthMm"] < 0.01)
        return fail("Rounded stick length must exceed its width by at least 0.01 mm. End radius is half the width.");
    if (g.hasProperty("cuts"))
    {
        if (!g["cuts"].isArray()) return fail("Craft stock cuts must be an array.");
        for (const auto& cut : *g["cuts"].getArray())
            if (!validateCut(cut, g, error)) return false;
    }
    return true;
}
TopoDS_Shape shape(const juce::var& g, juce::String& error)
{
    if (!validateGeometry(g, error)) return {};
    const auto profile = g["profile"].toString();
    const double length = g["lengthMm"], width = g["widthMm"], thickness = g["thicknessMm"], radius = (double)g["outerDiameterMm"] / 2;
    TopoDS_Shape base;
    if (profile == "rectangular") base = BRepPrimAPI_MakeBox(length, width, thickness).Shape();
    else if (profile == "rod" || profile == "tube")
    {
        const gp_Ax2 axis(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
        const auto outer = BRepPrimAPI_MakeCylinder(axis, radius, length).Shape();
        if (profile == "rod") base = outer;
        else
        {
            BRepAlgoAPI_Cut cut(outer, BRepPrimAPI_MakeCylinder(axis, (double)g["innerDiameterMm"] / 2, length).Shape());
            cut.Build();
            if (!cut.IsDone()) { error = "OCCT could not form the hollow tube."; return {}; }
            base = cut.Shape();
        }
    }
    else
    {
        const double r = width / 2;
        const gp_Pnt a(r, 0, 0), b(length-r, 0, 0), c(length-r, width, 0), d(r, width, 0);
        BRepBuilderAPI_MakeWire wire;
        wire.Add(BRepBuilderAPI_MakeEdge(a, b).Edge());
        wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(b, gp_Pnt(length, r, 0), c).Value()).Edge());
        wire.Add(BRepBuilderAPI_MakeEdge(c, d).Edge());
        wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(d, gp_Pnt(0, r, 0), a).Value()).Edge());
        base = BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()).Face(), gp_Vec(0, 0, thickness)).Shape();
    }
    if (base.IsNull()) return {};

    TopoDS_Shape current = base;
    if (g.hasProperty("cuts") && g["cuts"].isArray())
    {
        for (const auto& cut : *g["cuts"].getArray())
        {
            const auto type = cut["type"].toString();
            gp_Pnt P;
            gp_Dir n;
            if (type == "length")
            {
                const double pos = cut["positionMm"];
                const auto side = cut.getProperty("side", "end").toString();
                P = gp_Pnt(pos, 0, 0);
                n = side == "start" ? gp_Dir(-1, 0, 0) : gp_Dir(1, 0, 0);
            }
            else if (type == "miter")
            {
                const double pos = cut.getProperty("positionMm", 0.0);
                const double angleRad = (double)cut.getProperty("angleDeg", 0.0) * juce::MathConstants<double>::pi / 180.0;
                const double bevelRad = (double)cut.getProperty("bevelDeg", 0.0) * juce::MathConstants<double>::pi / 180.0;
                const auto side = cut.getProperty("side", "end").toString();
                P = gp_Pnt(pos, 0, 0);
                const double dirX = side == "start" ? -1.0 : 1.0;
                const double nx = dirX * std::cos(angleRad) * std::cos(bevelRad);
                const double ny = dirX * std::sin(angleRad) * std::cos(bevelRad);
                const double nz = -dirX * std::sin(bevelRad);
                gp_Vec vec(nx, ny, nz);
                if (vec.Magnitude() < 1e-6) { error = "Invalid cut direction."; return {}; }
                n = gp_Dir(vec);
            }
            else
            {
                P = gp_Pnt(cut["px"], cut["py"], cut["pz"]);
                gp_Vec vec(cut["nx"], cut["ny"], cut["nz"]);
                if (vec.Magnitude() < 1e-6) { error = "Plane normal must not be zero."; return {}; }
                n = gp_Dir(vec);
            }

            Bnd_Box b;
            BRepBndLib::Add(current, b);
            Standard_Real x1, y1, z1, x2, y2, z2;
            b.Get(x1, y1, z1, x2, y2, z2);
            const double diag = std::sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1) + (z2-z1)*(z2-z1));
            const double size = std::max(diag * 2.0, 500.0) + 100.0;
            const gp_Vec up = std::abs(n.Z()) < 0.9 ? gp_Vec(0, 0, 1) : gp_Vec(0, 1, 0);
            const gp_Vec uVec = (gp_Vec(n.XYZ()) ^ up).Normalized();
            const gp_Vec vVec = (gp_Vec(n.XYZ()) ^ uVec).Normalized();
            const gp_Pnt corner = P.Translated(uVec * (-size)).Translated(vVec * (-size));
            const gp_Ax2 boxAxis(corner, n, gp_Dir(uVec));
            const TopoDS_Shape cutBox = BRepPrimAPI_MakeBox(boxAxis, 2.0 * size, 2.0 * size, size).Shape();
            BRepAlgoAPI_Cut cutOp(current, cutBox);
            cutOp.Build();
            if (!cutOp.IsDone() || cutOp.HasErrors())
            {
                error = "OCCT Boolean cut operation failed.";
                return {};
            }
            const TopoDS_Shape res = cutOp.Shape();
            TopExp_Explorer solidEx(res, TopAbs_SOLID);
            if (!solidEx.More())
            {
                error = "Cut completely removes the piece.";
                return {};
            }
            TopoDS_Shape primarySolid;
            double maxVol = 0.0;
            for (; solidEx.More(); solidEx.Next())
            {
                GProp_GProps sp;
                BRepGProp::VolumeProperties(solidEx.Current(), sp);
                if (sp.Mass() > maxVol) { maxVol = sp.Mass(); primarySolid = solidEx.Current(); }
            }
            if (maxVol < 0.001)
            {
                error = "Cut leaves degenerate or zero-volume solid.";
                return {};
            }
            current = primarySolid;
        }
    }
    return current;
}
juce::var cutList(const CadDocument& document)
{
    auto root = components::object();
    juce::Array<juce::var> items;
    std::map<juce::String, int> summaryCounts;
    std::map<juce::String, juce::var> summaryTemplates;

    for (const auto& inst : document.mechanicsData().instances)
    {
        const auto* comp = document.componentFor(inst.featureId);
        if (!comp || !craft::isStock(*comp)) continue;

        auto item = components::object();
        components::put(item, "instanceId", inst.id);
        components::put(item, "instanceName", inst.name);
        components::put(item, "featureId", inst.featureId);
        components::put(item, "componentName", (*comp)["name"]);

        const auto* assignment = document.materialsData().assignmentFor(inst.featureId);
        juce::String matName = "Default";
        if (assignment)
        {
            if (const auto* mat = document.materialsData().find(assignment->materialId))
                matName = mat->name;
        }
        else if ((*comp).hasProperty("material"))
        {
            matName = (*comp)["material"]["name"].toString();
        }
        components::put(item, "material", matName);

        const auto g = (*comp)["geometry"];
        const auto profile = g["profile"].toString();
        components::put(item, "profile", profile);

        const double origLength = g["lengthMm"];
        components::put(item, "originalLengthMm", origLength);

        double finishedLength = origLength;
        const auto* feat = document.find(inst.featureId);
        if (feat && !feat->shape.IsNull())
        {
            Bnd_Box bbox;
            BRepBndLib::Add(feat->shape, bbox);
            Standard_Real x1, y1, z1, x2, y2, z2;
            bbox.Get(x1, y1, z1, x2, y2, z2);
            finishedLength = x2 - x1;
        }
        components::put(item, "finishedLengthMm", finishedLength);

        const double offcut = std::max(0.0, origLength - finishedLength);
        components::put(item, "offcutMm", offcut);

        juce::Array<juce::var> cutListEntries;
        if (const auto* cuts = g.getProperty("cuts", {}).getArray())
        {
            for (const auto& c : *cuts) cutListEntries.add(components::clone(c));
        }
        components::put(item, "cuts", cutListEntries);

        const juce::String groupKey = matName + "|" + profile + "|" + juce::String(origLength, 2) + "|" + juce::String(finishedLength, 2) + "|" + juce::String(cutListEntries.size());
        summaryCounts[groupKey]++;
        if (summaryTemplates.find(groupKey) == summaryTemplates.end())
        {
            summaryTemplates[groupKey] = components::clone(item);
        }

        items.add(item);
    }

    juce::Array<juce::var> summary;
    for (const auto& pair : summaryCounts)
    {
        auto entry = components::clone(summaryTemplates[pair.first]);
        components::put(entry, "quantity", pair.second);
        summary.add(entry);
    }

    components::put(root, "items", items);
    components::put(root, "summary", summary);
    components::put(root, "totalPieces", items.size());
    components::put(root, "adhesives", adhesiveBom(document));
    return root;
}
bool evaluateBondedJoint(const CadDocument& doc, CadBondedJoint& joint)
{
    const auto* instA = doc.mechanicsData().findInstance(joint.instanceA);
    const auto* instB = doc.mechanicsData().findInstance(joint.instanceB);
    if (!instA || !instB)
    {
        joint.status = "invalid_reference";
        joint.failureReason = !instA ? ("Referenced instance not found: " + joint.instanceA)
                                     : ("Referenced instance not found: " + joint.instanceB);
        joint.contactAreaMm2 = 0.0;
        joint.contactLengthMm = 0.0;
        joint.estimatedVolumeMm3 = 0.0;
        return true;
    }

    const CadAdhesive* adh = doc.mechanicsData().findAdhesive(joint.adhesiveId);
    CadAdhesive defaultAdh;
    if (!adh)
    {
        for (const auto& d : mech::defaultAdhesives())
        {
            if (d.id == joint.adhesiveId) { defaultAdh = d; adh = &defaultAdh; break; }
        }
    }
    if (!adh)
    {
        joint.status = "invalid_reference";
        joint.failureReason = "Adhesive definition not found: " + joint.adhesiveId;
        joint.contactAreaMm2 = 0.0;
        joint.contactLengthMm = 0.0;
        joint.estimatedVolumeMm3 = 0.0;
        return true;
    }

    auto getSubstrate = [&](const juce::String& featId) -> juce::String
    {
        if (const auto* comp = doc.componentFor(featId))
        {
            const auto cat = (*comp)["category"].toString();
            if (cat.containsIgnoreCase("wood")) return "Wood";
            if (cat.containsIgnoreCase("plastic")) return "Plastic";
            if (cat.containsIgnoreCase("cardboard")) return "Cardboard";
            if (cat.containsIgnoreCase("foam")) return "Foam board";
        }
        if (const auto* assign = doc.materialsData().assignmentFor(featId))
        {
            if (const auto* mat = doc.materialsData().find(assign->materialId))
            {
                if (mat->name.containsIgnoreCase("wood") || mat->family.containsIgnoreCase("wood")) return "Wood";
                if (mat->name.containsIgnoreCase("plastic") || mat->family.containsIgnoreCase("plastic") || mat->name.containsIgnoreCase("pla") || mat->name.containsIgnoreCase("acrylic")) return "Plastic";
                if (mat->name.containsIgnoreCase("cardboard")) return "Cardboard";
                if (mat->name.containsIgnoreCase("foam")) return "Foam board";
            }
        }
        return "Wood";
    };

    const juce::String subA = getSubstrate(instA->featureId);
    const juce::String subB = getSubstrate(instB->featureId);

    bool compatible = true;
    juce::String incompatMsg;
    if (!adh->compatibleSubstrates.contains(subA, true))
    {
        compatible = false;
        incompatMsg = adh->name + " is incompatible with " + subA + " substrate (piece " + joint.instanceA + ")";
    }
    else if (!adh->compatibleSubstrates.contains(subB, true))
    {
        compatible = false;
        incompatMsg = adh->name + " is incompatible with " + subB + " substrate (piece " + joint.instanceB + ")";
    }

    if (!compatible)
    {
        joint.status = "incompatible_substrate";
        joint.failureReason = incompatMsg;
        joint.contactAreaMm2 = 0.0;
        joint.contactLengthMm = 0.0;
        joint.estimatedVolumeMm3 = 0.0;
        return true;
    }

    const auto* featA = doc.find(instA->featureId);
    const auto* featB = doc.find(instB->featureId);
    if (!featA || featA->shape.IsNull() || !featB || featB->shape.IsNull())
    {
        joint.status = "invalid_reference";
        joint.failureReason = "Instance shapes are not evaluated.";
        return true;
    }

    const gp_Trsf trsfA = instA->placement.toTrsf();
    const gp_Trsf trsfB = instB->placement.toTrsf();
    const TopoDS_Shape shapeA_world = featA->shape.Moved(trsfA);
    const TopoDS_Shape shapeB_world = featB->shape.Moved(trsfB);

    BRepExtrema_DistShapeShape distCalc(shapeA_world, shapeB_world);
    distCalc.Perform();
    if (!distCalc.IsDone() || distCalc.NbSolution() == 0)
    {
        joint.status = "invalid_reference";
        joint.failureReason = "Distance evaluation failed between shapes.";
        return true;
    }

    const double minDist = distCalc.Value();
    joint.separationDistanceMm = minDist;
    const double tolerance = std::max(0.6, joint.bondThicknessMm * 1.5 + 0.1);

    if (minDist > tolerance)
    {
        joint.status = "separated";
        joint.failureReason = "Pieces separated by " + juce::String(minDist, 2) + " mm (tolerance: " + juce::String(tolerance, 2) + " mm)";
        joint.contactAreaMm2 = 0.0;
        joint.contactLengthMm = 0.0;
        joint.estimatedVolumeMm3 = 0.0;
        return true;
    }

    Bnd_Box bA, bB;
    BRepBndLib::Add(shapeA_world, bA);
    BRepBndLib::Add(shapeB_world, bB);
    Standard_Real ax1, ay1, az1, ax2, ay2, az2;
    Standard_Real bx1, by1, bz1, bx2, by2, bz2;
    bA.Get(ax1, ay1, az1, ax2, ay2, az2);
    bB.Get(bx1, by1, bz1, bx2, by2, bz2);

    const double ix1 = std::max(ax1, bx1), ix2 = std::min(ax2, bx2);
    const double iy1 = std::max(ay1, by1), iy2 = std::min(ay2, by2);
    const double iz1 = std::max(az1, bz1), iz2 = std::min(az2, bz2);
    const double odx = std::max(0.0, ix2 - ix1);
    const double ody = std::max(0.0, iy2 - iy1);
    const double odz = std::max(0.0, iz2 - iz1);

    double boxContactArea = 0.0;
    if (odx <= ody && odx <= odz) boxContactArea = ody * odz;
    else if (ody <= odx && ody <= odz) boxContactArea = odx * odz;
    else boxContactArea = odx * ody;

    if (joint.type == "surface")
    {
        double calculatedArea = 0.0;
        for (TopExp_Explorer exA(shapeA_world, TopAbs_FACE); exA.More(); exA.Next())
        {
            const TopoDS_Face& fA = TopoDS::Face(exA.Current());
            BRepAdaptor_Surface adaptA(fA);
            if (adaptA.GetType() != GeomAbs_Plane) continue;
            const gp_Pln plnA = adaptA.Plane();
            gp_Dir dirA = plnA.Axis().Direction();
            if (fA.Orientation() == TopAbs_REVERSED) dirA.Reverse();

            for (TopExp_Explorer exB(shapeB_world, TopAbs_FACE); exB.More(); exB.Next())
            {
                const TopoDS_Face& fB = TopoDS::Face(exB.Current());
                BRepAdaptor_Surface adaptB(fB);
                if (adaptB.GetType() != GeomAbs_Plane) continue;
                const gp_Pln plnB = adaptB.Plane();
                gp_Dir dirB = plnB.Axis().Direction();
                if (fB.Orientation() == TopAbs_REVERSED) dirB.Reverse();

                if (dirA.Dot(dirB) < -0.8)
                {
                    const double planeDist = plnB.Distance(plnA.Location());
                    if (planeDist <= tolerance)
                    {
                        gp_Trsf nudge;
                        nudge.SetTranslation(gp_Vec(dirA.X() * (planeDist * 0.99), dirA.Y() * (planeDist * 0.99), dirA.Z() * (planeDist * 0.99)));
                        const TopoDS_Face nudgedFace = TopoDS::Face(fA.Moved(nudge));
                        const gp_Vec extVec(dirA.X() * 0.5, dirA.Y() * 0.5, dirA.Z() * 0.5);
                        BRepPrimAPI_MakePrism prism(nudgedFace, extVec);
                        if (prism.IsDone())
                        {
                            BRepAlgoAPI_Common common(prism.Shape(), shapeB_world);
                            common.Build();
                            if (common.IsDone() && !common.Shape().IsNull())
                            {
                                GProp_GProps vp;
                                BRepGProp::VolumeProperties(common.Shape(), vp);
                                const double fa = vp.Mass() / 0.5;
                                if (fa > 0.05) calculatedArea += fa;
                            }
                        }
                    }
                }
            }
        }

        if (calculatedArea < 0.05) calculatedArea = boxContactArea;
        if (calculatedArea < 0.05) calculatedArea = 100.0;

        joint.status = "valid";
        joint.failureReason = "";
        joint.contactAreaMm2 = calculatedArea;
        joint.contactLengthMm = 0.0;
        joint.estimatedVolumeMm3 = joint.contactAreaMm2 * joint.bondThicknessMm;
    }
    else if (joint.type == "edge")
    {
        double edgeLen = 0.0;
        for (TopExp_Explorer exE(shapeA_world, TopAbs_EDGE); exE.More(); exE.Next())
        {
            const TopoDS_Edge& e = TopoDS::Edge(exE.Current());
            BRepExtrema_DistShapeShape eDist(e, shapeB_world);
            eDist.Perform();
            if (eDist.IsDone() && eDist.Value() <= tolerance)
            {
                GProp_GProps lp;
                BRepGProp::LinearProperties(e, lp);
                edgeLen += lp.Mass();
            }
        }
        if (edgeLen < 0.1) edgeLen = std::max({odx, ody, odz});
        if (edgeLen < 0.1) edgeLen = 10.0;

        const double w = std::max(1.0, joint.bondThicknessMm * 2.0);
        joint.status = "valid";
        joint.failureReason = "";
        joint.contactLengthMm = edgeLen;
        joint.contactAreaMm2 = edgeLen * w;
        joint.estimatedVolumeMm3 = joint.contactAreaMm2 * joint.bondThicknessMm;
    }
    else // "fillet"
    {
        double seamLen = 0.0;
        for (TopExp_Explorer exE(shapeA_world, TopAbs_EDGE); exE.More(); exE.Next())
        {
            const TopoDS_Edge& e = TopoDS::Edge(exE.Current());
            BRepExtrema_DistShapeShape eDist(e, shapeB_world);
            eDist.Perform();
            if (eDist.IsDone() && eDist.Value() <= tolerance)
            {
                GProp_GProps lp;
                BRepGProp::LinearProperties(e, lp);
                seamLen += lp.Mass();
            }
        }
        if (seamLen < 0.1) seamLen = std::max({odx, ody, odz});
        if (seamLen < 0.1) seamLen = 10.0;

        const double s = std::max(0.5, joint.beadLegMm);
        joint.status = "valid";
        joint.failureReason = "";
        joint.contactLengthMm = seamLen;
        joint.contactAreaMm2 = 2.0 * s * seamLen;
        joint.estimatedVolumeMm3 = 0.5 * s * s * seamLen;
    }

    return true;
}

std::vector<juce::String> getConnectedBondedGroup(const CadDocument& doc, const juce::String& instanceId)
{
    std::vector<juce::String> group;
    if (doc.mechanicsData().findInstance(instanceId) == nullptr) return group;

    std::map<juce::String, std::vector<juce::String>> adj;
    for (const auto& joint : doc.mechanicsData().bondedJoints)
    {
        if (joint.status == "valid")
        {
            adj[joint.instanceA].push_back(joint.instanceB);
            adj[joint.instanceB].push_back(joint.instanceA);
        }
    }

    std::set<juce::String> visited;
    std::vector<juce::String> queue;
    queue.push_back(instanceId);
    visited.insert(instanceId);

    size_t head = 0;
    while (head < queue.size())
    {
        const auto curr = queue[head++];
        group.push_back(curr);
        for (const auto& neighbor : adj[curr])
        {
            if (visited.find(neighbor) == visited.end())
            {
                visited.insert(neighbor);
                queue.push_back(neighbor);
            }
        }
    }
    return group;
}

juce::var adhesiveBom(const CadDocument& doc)
{
    auto root = components::object();
    int totalJoints = 0, validJoints = 0, separatedJoints = 0, incompatibleJoints = 0;
    double totalVolumeMm3 = 0.0, totalAreaMm2 = 0.0;

    std::map<juce::String, int> adhJointCounts;
    std::map<juce::String, int> adhValidCounts;
    std::map<juce::String, double> adhArea;
    std::map<juce::String, double> adhVolume;
    std::map<juce::String, juce::String> adhNames;

    juce::Array<juce::var> jointList;

    for (const auto& j : doc.mechanicsData().bondedJoints)
    {
        ++totalJoints;
        if (j.status == "valid")
        {
            ++validJoints;
            totalVolumeMm3 += j.estimatedVolumeMm3;
            totalAreaMm2 += j.contactAreaMm2;
            adhValidCounts[j.adhesiveId]++;
            adhArea[j.adhesiveId] += j.contactAreaMm2;
            adhVolume[j.adhesiveId] += j.estimatedVolumeMm3;
        }
        else if (j.status == "separated")
        {
            ++separatedJoints;
        }
        else if (j.status == "incompatible_substrate")
        {
            ++incompatibleJoints;
        }

        adhJointCounts[j.adhesiveId]++;
        if (adhNames.find(j.adhesiveId) == adhNames.end())
        {
            const auto* a = doc.mechanicsData().findAdhesive(j.adhesiveId);
            adhNames[j.adhesiveId] = a ? a->name : j.adhesiveId;
        }

        auto item = components::object();
        components::put(item, "id", j.id);
        components::put(item, "name", j.name);
        components::put(item, "instanceA", j.instanceA);
        components::put(item, "instanceB", j.instanceB);
        components::put(item, "type", j.type);
        components::put(item, "adhesiveId", j.adhesiveId);
        components::put(item, "adhesiveName", adhNames[j.adhesiveId]);
        components::put(item, "status", j.status);
        components::put(item, "failureReason", j.failureReason);
        components::put(item, "separationDistanceMm", j.separationDistanceMm);
        components::put(item, "bondThicknessMm", j.bondThicknessMm);
        components::put(item, "contactAreaMm2", j.contactAreaMm2);
        components::put(item, "contactLengthMm", j.contactLengthMm);
        components::put(item, "estimatedVolumeMm3", j.estimatedVolumeMm3);
        jointList.add(item);
    }

    juce::Array<juce::var> adhSummary;
    for (const auto& pair : adhJointCounts)
    {
        auto item = components::object();
        components::put(item, "adhesiveId", pair.first);
        components::put(item, "adhesiveName", adhNames[pair.first]);
        components::put(item, "jointCount", pair.second);
        components::put(item, "validJointCount", adhValidCounts[pair.first]);
        components::put(item, "totalAreaMm2", adhArea[pair.first]);
        components::put(item, "totalVolumeMm3", adhVolume[pair.first]);
        components::put(item, "totalVolumeMl", adhVolume[pair.first] / 1000.0);
        adhSummary.add(item);
    }

    components::put(root, "totalJoints", totalJoints);
    components::put(root, "validJoints", validJoints);
    components::put(root, "separatedJoints", separatedJoints);
    components::put(root, "incompatibleJoints", incompatibleJoints);
    components::put(root, "totalBondAreaMm2", totalAreaMm2);
    components::put(root, "totalAdhesiveVolumeMm3", totalVolumeMm3);
    components::put(root, "totalAdhesiveVolumeMl", totalVolumeMm3 / 1000.0);
    components::put(root, "adhesives", adhSummary);
    components::put(root, "joints", jointList);

    return root;
}

TopoDS_Shape createFilletBeadSolid(const CadDocument& doc, const CadBondedJoint& joint, juce::String& error)
{
    const auto* instA = doc.mechanicsData().findInstance(joint.instanceA);
    const auto* instB = doc.mechanicsData().findInstance(joint.instanceB);
    if (!instA || !instB) { error = "Invalid instance reference"; return TopoDS_Shape(); }
    const auto* featA = doc.find(instA->featureId);
    const auto* featB = doc.find(instB->featureId);
    if (!featA || !featB) { error = "Geometry not evaluated"; return TopoDS_Shape(); }

    const double s = std::max(0.5, joint.beadLegMm);
    const double L = std::max(1.0, joint.contactLengthMm);

    BRepBuilderAPI_MakePolygon poly;
    poly.Add(gp_Pnt(0, 0, 0));
    poly.Add(gp_Pnt(0, s, 0));
    poly.Add(gp_Pnt(0, 0, s));
    poly.Close();
    if (!poly.IsDone()) { error = "Failed to construct bead polygon"; return TopoDS_Shape(); }
    BRepBuilderAPI_MakeFace faceMaker(poly.Wire());
    if (!faceMaker.IsDone()) { error = "Failed to construct bead face"; return TopoDS_Shape(); }
    BRepPrimAPI_MakePrism prism(faceMaker.Face(), gp_Vec(L, 0, 0));
    if (!prism.IsDone()) { error = "Failed to extrude bead solid"; return TopoDS_Shape(); }
    return prism.Shape();
}

juce::Array<juce::var> templates()
{
    const auto root = juce::JSON::parse(juce::String::fromUTF8(EngineeringData::craftstockseed_json, EngineeringData::craftstockseed_jsonSize));
    if (const auto* list = root["records"].getArray()) return *list;
    return {};
}

std::vector<CadMaterial> illustrativeCraftMaterials()
{
    std::vector<CadMaterial> list;

    CadMaterial wood;
    wood.id = "mat-craft-wood-birch";
    wood.name = "Craft Wood Stick (Birch/Basswood)";
    wood.model = "isotropic";
    wood.densityKgPerM3 = 550.0;
    wood.youngsModulusMPa = 9500.0;
    wood.poissonsRatio = 0.35;
    wood.shearModulusMPa = 3500.0;
    wood.tensileStrengthMPa = 80.0;
    wood.compressiveStrengthMPa = 40.0;
    wood.shearStrengthMPa = 8.0;
    wood.family = "Wood";
    wood.condition = "Illustrative longitudinal properties along grain";
    wood.source = "Wood Handbook / DjehutiCAD Phase 4";
    wood.note = "Assumes grain aligned with member length. Transverse properties are lower. Approximate linear elastic model.";
    list.push_back(wood);

    CadMaterial plastic;
    plastic.id = "mat-craft-plastic-pla";
    plastic.name = "Craft Plastic (PLA)";
    plastic.model = "isotropic";
    plastic.densityKgPerM3 = 1240.0;
    plastic.youngsModulusMPa = 3500.0;
    plastic.poissonsRatio = 0.36;
    plastic.shearModulusMPa = 1280.0;
    plastic.tensileStrengthMPa = 50.0;
    plastic.compressiveStrengthMPa = 60.0;
    plastic.shearStrengthMPa = 25.0;
    plastic.family = "Plastic";
    plastic.condition = "Room temperature 23C";
    plastic.source = "Polymer Handbook / DjehutiCAD Phase 4";
    plastic.note = "Approximate linear elastic model for PLA/acrylic craft stock.";
    list.push_back(plastic);

    CadMaterial cardboard;
    cardboard.id = "mat-craft-cardboard-corrugated";
    cardboard.name = "Craft Cardboard (Corrugated)";
    cardboard.model = "isotropic";
    cardboard.densityKgPerM3 = 650.0;
    cardboard.youngsModulusMPa = 1800.0;
    cardboard.poissonsRatio = 0.30;
    cardboard.shearModulusMPa = 690.0;
    cardboard.tensileStrengthMPa = 15.0;
    cardboard.compressiveStrengthMPa = 10.0;
    cardboard.shearStrengthMPa = 4.0;
    cardboard.family = "Cardboard";
    cardboard.condition = "Effective beam stiffness";
    cardboard.source = "Packaging Engineering Handbook / DjehutiCAD Phase 4";
    cardboard.note = "Homogenized beam section approximation.";
    list.push_back(cardboard);

    CadMaterial foam;
    foam.id = "mat-craft-foam-board-panel";
    foam.name = "Craft Foam Board (Paper-Faced EPS)";
    foam.model = "isotropic";
    foam.densityKgPerM3 = 100.0;
    foam.youngsModulusMPa = 120.0;
    foam.poissonsRatio = 0.30;
    foam.shearModulusMPa = 45.0;
    foam.tensileStrengthMPa = 2.0;
    foam.compressiveStrengthMPa = 1.2;
    foam.shearStrengthMPa = 0.6;
    foam.family = "Foam board";
    foam.condition = "Sandwich panel effective beam stiffness";
    foam.source = "Composite Packaging Reference / DjehutiCAD Phase 4";
    foam.note = "Equivalent homogenized elastic properties for sandwich panel.";
    list.push_back(foam);

    return list;
}
}
