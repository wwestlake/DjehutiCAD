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
    return root;
}
juce::Array<juce::var> templates()
{
    const auto root = juce::JSON::parse(juce::String::fromUTF8(EngineeringData::craftstockseed_json, EngineeringData::craftstockseed_jsonSize));
    if (const auto* list = root["records"].getArray()) return *list;
    return {};
}
}
