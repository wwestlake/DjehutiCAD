#include "CadCraftStock.h"
#include <EngineeringData.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <gp_Ax2.hxx>
#include <cmath>

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
    return true;
}
TopoDS_Shape shape(const juce::var& g, juce::String& error)
{
    if (!validateGeometry(g, error)) return {};
    const auto profile = g["profile"].toString();
    const double length = g["lengthMm"], width = g["widthMm"], thickness = g["thicknessMm"], radius = (double)g["outerDiameterMm"] / 2;
    if (profile == "rectangular") return BRepPrimAPI_MakeBox(length, width, thickness).Shape();
    if (profile == "rod" || profile == "tube")
    {
        const gp_Ax2 axis(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
        const auto outer = BRepPrimAPI_MakeCylinder(axis, radius, length).Shape();
        if (profile == "rod") return outer;
        BRepAlgoAPI_Cut cut(outer, BRepPrimAPI_MakeCylinder(axis, (double)g["innerDiameterMm"] / 2, length).Shape());
        cut.Build();
        if (!cut.IsDone()) { error = "OCCT could not form the hollow tube."; return {}; }
        return cut.Shape();
    }
    const double r = width / 2;
    const gp_Pnt a(r, 0, 0), b(length-r, 0, 0), c(length-r, width, 0), d(r, width, 0);
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(a, b).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(b, gp_Pnt(length, r, 0), c).Value()).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(c, d).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(d, gp_Pnt(0, r, 0), a).Value()).Edge());
    return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()).Face(), gp_Vec(0, 0, thickness)).Shape();
}
juce::Array<juce::var> templates()
{
    const auto root = juce::JSON::parse(juce::String::fromUTF8(EngineeringData::craftstockseed_json, EngineeringData::craftstockseed_jsonSize));
    if (const auto* list = root["records"].getArray()) return *list;
    return {};
}
}
