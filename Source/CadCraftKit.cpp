#include "CadCraftKit.h"
#include "CadDocument.h"
#include "CadCraftStock.h"
#include "CadStructuralModel.h"
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace djehuti::cad::kit
{

static void put(juce::var& obj, const juce::Identifier& id, const juce::var& value)
{
    if (auto* d = obj.getDynamicObject())
        d->setProperty(id, value);
}

static juce::var object()
{
    return juce::var(new juce::DynamicObject());
}

juce::var CadCraftKitMetadata::toVar() const
{
    auto obj = object();
    put(obj, "name", name);
    put(obj, "description", description);
    put(obj, "author", author);
    put(obj, "audience", audience);
    put(obj, "revision", revision);
    put(obj, "notes", notes);
    juce::Array<juce::var> steps;
    for (const auto& s : customAssemblySteps)
        steps.add(s);
    put(obj, "customAssemblySteps", steps);
    return obj;
}

CadCraftKitMetadata CadCraftKitMetadata::fromVar(const juce::var& v)
{
    CadCraftKitMetadata meta;
    if (v.hasProperty("name")) meta.name = v["name"].toString();
    if (v.hasProperty("description")) meta.description = v["description"].toString();
    if (v.hasProperty("author")) meta.author = v["author"].toString();
    if (v.hasProperty("audience")) meta.audience = v["audience"].toString();
    if (v.hasProperty("revision")) meta.revision = v["revision"].toString();
    if (v.hasProperty("notes")) meta.notes = v["notes"].toString();
    if (const auto* steps = v.getProperty("customAssemblySteps", {}).getArray())
    {
        meta.customAssemblySteps.clear();
        for (const auto& s : *steps)
            meta.customAssemblySteps.push_back(s.toString());
    }
    return meta;
}

juce::var toJson(const CadCraftKitMetadata& meta)
{
    return meta.toVar();
}

bool fromJson(const juce::var& json, CadCraftKitMetadata& meta, juce::String& error)
{
    if (!json.isObject())
    {
        error = "Project kit metadata must be a JSON object.";
        return false;
    }
    meta = CadCraftKitMetadata::fromVar(json);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// BOM Implementation
// ---------------------------------------------------------------------------------------------------------------
juce::var CadKitBOMItem::toVar() const
{
    auto obj = object();
    put(obj, "itemType", itemType);
    put(obj, "id", id);
    put(obj, "name", name);
    put(obj, "material", material);
    put(obj, "profile", profile);
    put(obj, "quantity", quantity);
    put(obj, "lengthMm", lengthMm);
    put(obj, "widthMm", widthMm);
    put(obj, "thicknessMm", thicknessMm);
    put(obj, "totalLengthMm", totalLengthMm);
    put(obj, "totalVolumeMm3", totalVolumeMm3);
    put(obj, "estimatedMassG", estimatedMassG);
    put(obj, "notes", notes);
    return obj;
}

juce::var CadKitBOM::toVar() const
{
    auto obj = object();
    juce::Array<juce::var> arr;
    for (const auto& item : items) arr.add(item.toVar());
    put(obj, "items", arr);
    put(obj, "totalFabricatedParts", totalFabricatedParts);
    put(obj, "totalPurchasedComponents", totalPurchasedComponents);
    put(obj, "totalAdhesiveJoints", totalAdhesiveJoints);
    put(obj, "totalAdhesiveVolumeMl", totalAdhesiveVolumeMl);
    put(obj, "totalStructureMassG", totalStructureMassG);
    return obj;
}

juce::String CadKitBOM::toMarkdown() const
{
    juce::String s = "### Consolidated Bill of Materials (BOM)\n\n";
    s += "| Type | Part ID / Name | Material | Profile | Dimensions (mm) | Qty | Total Length (mm) | Est. Mass (g) | Notes |\n";
    s += "|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& item : items)
    {
        juce::String dimStr = "-";
        if (item.lengthMm > 0.0)
        {
            if (item.widthMm > 0.0 && item.thicknessMm > 0.0)
                dimStr = juce::String(item.lengthMm, 1) + " x " + juce::String(item.widthMm, 1) + " x " + juce::String(item.thicknessMm, 1);
            else if (item.widthMm > 0.0)
                dimStr = juce::String(item.lengthMm, 1) + " x " + juce::String(item.widthMm, 1);
            else
                dimStr = "L=" + juce::String(item.lengthMm, 1);
        }

        s += "| " + item.itemType + " | " + item.name + " | " + item.material + " | " + item.profile +
             " | " + dimStr + " | " + juce::String(item.quantity) + " | " +
             (item.totalLengthMm > 0 ? juce::String(item.totalLengthMm, 1) : "-") + " | " +
             juce::String(item.estimatedMassG, 2) + " | " + item.notes + " |\n";
    }
    s += "\n";
    s += "**Summary:** " + juce::String(totalFabricatedParts) + " fabricated parts, " +
         juce::String(totalPurchasedComponents) + " purchased parts, " +
         juce::String(totalAdhesiveJoints) + " adhesive joints (" + juce::String(totalAdhesiveVolumeMl, 2) + " mL), " +
         "Total Structural Mass: " + juce::String(totalStructureMassG, 1) + " g.\n\n";
    return s;
}

juce::String CadKitBOM::toCSV() const
{
    juce::String s = "Type,ID,Name,Material,Profile,Quantity,LengthMm,WidthMm,ThicknessMm,TotalLengthMm,TotalVolumeMm3,EstimatedMassG,Notes\n";
    for (const auto& item : items)
    {
        s += "\"" + item.itemType + "\",\"" + item.id + "\",\"" + item.name + "\",\"" + item.material + "\",\"" +
             item.profile + "\"," + juce::String(item.quantity) + "," + juce::String(item.lengthMm, 2) + "," +
             juce::String(item.widthMm, 2) + "," + juce::String(item.thicknessMm, 2) + "," +
             juce::String(item.totalLengthMm, 2) + "," + juce::String(item.totalVolumeMm3, 2) + "," +
             juce::String(item.estimatedMassG, 2) + ",\"" + item.notes + "\"\n";
    }
    return s;
}

CadKitBOM generateBOM(const CadDocument& doc)
{
    CadKitBOM bom;
    const auto& instances = doc.mechanicsData().instances;

    // Map to group identical fabricated stock items: Key = mat + "|" + profile + "|" + length + "|" + width + "|" + thickness
    std::map<juce::String, CadKitBOMItem> groupedFabricated;
    std::map<juce::String, CadKitBOMItem> groupedPurchased;

    for (const auto& inst : instances)
    {
        const auto* comp = doc.componentFor(inst.featureId);
        if (!comp) continue;

        const bool isStock = craft::isStock(*comp);
        const auto g = (*comp)["geometry"];

        juce::String matName = "Generic Material";
        double densityKgM3 = 550.0;
        if (const auto* assign = doc.materialsData().assignmentFor(inst.featureId))
        {
            if (const auto* mat = doc.materialsData().find(assign->materialId))
            {
                matName = mat->name;
                densityKgM3 = mat->densityKgPerM3.value_or(550.0);
            }
        }
        else if ((*comp).hasProperty("material"))
        {
            matName = (*comp)["material"]["name"].toString();
            densityKgM3 = (*comp)["material"].getProperty("densityKgPerM3", 550.0);
        }

        if (isStock)
        {
            const auto profile = g["profile"].toString();
            const double origL = g.getProperty("lengthMm", 0.0);
            const double w = g.getProperty("widthMm", g.getProperty("outerDiameterMm", 0.0));
            const double t = g.getProperty("thicknessMm", 0.0);

            double finishedL = origL;
            const auto* feat = doc.find(inst.featureId);
            if (feat && !feat->shape.IsNull())
            {
                Bnd_Box bbox;
                BRepBndLib::Add(feat->shape, bbox);
                Standard_Real x1, y1, z1, x2, y2, z2;
                bbox.Get(x1, y1, z1, x2, y2, z2);
                finishedL = std::abs(x2 - x1);
            }

            // Estimate volume in mm3
            double partVolMm3 = 0.0;
            if (profile == "rectangular") partVolMm3 = finishedL * w * t;
            else if (profile == "roundedStick") partVolMm3 = ((finishedL - w) * w + juce::MathConstants<double>::pi * w * w * 0.25) * t;
            else if (profile == "rod") partVolMm3 = juce::MathConstants<double>::pi * (w * 0.5) * (w * 0.5) * finishedL;
            else if (profile == "tube")
            {
                const double id = g.getProperty("innerDiameterMm", 0.0);
                partVolMm3 = juce::MathConstants<double>::pi * 0.25 * (w * w - id * id) * finishedL;
            }

            const double massG = (partVolMm3 * 1e-9) * densityKgM3 * 1000.0;

            const juce::String key = matName + "|" + profile + "|" + juce::String(finishedL, 1) + "|" + juce::String(w, 1) + "|" + juce::String(t, 1);
            if (groupedFabricated.find(key) == groupedFabricated.end())
            {
                CadKitBOMItem item;
                item.itemType = "fabricatedStock";
                item.id = inst.id;
                item.name = (*comp)["name"].toString();
                item.material = matName;
                item.profile = profile;
                item.quantity = 1;
                item.lengthMm = finishedL;
                item.widthMm = w;
                item.thicknessMm = t;
                item.totalLengthMm = finishedL;
                item.totalVolumeMm3 = partVolMm3;
                item.estimatedMassG = massG;
                item.notes = (finishedL < origL - 0.5) ? "Cut to length from " + juce::String(origL, 0) + " mm stock" : "Standard length";
                groupedFabricated[key] = item;
            }
            else
            {
                auto& existing = groupedFabricated[key];
                existing.quantity += 1;
                existing.totalLengthMm += finishedL;
                existing.totalVolumeMm3 += partVolMm3;
                existing.estimatedMassG += massG;
            }
            bom.totalFabricatedParts++;
            bom.totalStructureMassG += massG;
        }
        else
        {
            // Purchased or commercial component
            const juce::String compName = (*comp)["name"].toString();
            const juce::String key = compName + "|" + matName;
            if (groupedPurchased.find(key) == groupedPurchased.end())
            {
                CadKitBOMItem item;
                item.itemType = "purchasedPart";
                item.id = (*comp)["id"].toString();
                item.name = compName;
                item.material = matName;
                item.profile = "Standard/Purchased";
                item.quantity = 1;
                item.notes = (*comp).getProperty("manufacturer", "").toString();
                groupedPurchased[key] = item;
            }
            else
            {
                groupedPurchased[key].quantity += 1;
            }
            bom.totalPurchasedComponents++;
        }
    }

    for (const auto& pair : groupedFabricated)
        bom.items.push_back(pair.second);
    for (const auto& pair : groupedPurchased)
        bom.items.push_back(pair.second);

    // Consumable Adhesives from bonded joints
    const auto adhRoot = doc.adhesiveBom();
    if (const auto* adhesives = adhRoot.getProperty("adhesives", {}).getArray())
    {
        for (const auto& adh : *adhesives)
        {
            CadKitBOMItem item;
            item.itemType = "consumableAdhesive";
            item.id = adh["adhesiveId"].toString();
            item.name = adh["adhesiveName"].toString();
            item.material = "Adhesive Consumable";
            item.profile = "Joint Bond";
            item.quantity = (int) adh["jointCount"];
            item.totalVolumeMm3 = (double) adh["totalVolumeMm3"];
            const double volMl = (double) adh["totalVolumeMl"];
            item.estimatedMassG = volMl * 1.1; // ~1.1 g/mL for glue
            item.notes = juce::String((int)adh["validJointCount"]) + " valid structural joints (" + juce::String(volMl, 2) + " mL required)";
            bom.items.push_back(item);

            bom.totalAdhesiveJoints += item.quantity;
            bom.totalAdhesiveVolumeMl += volMl;
            bom.totalStructureMassG += item.estimatedMassG;
        }
    }

    return bom;
}

// ---------------------------------------------------------------------------------------------------------------
// Stock Cutting Optimization & Nesting Implementation
// ---------------------------------------------------------------------------------------------------------------
juce::var CadStockNestingPlan::toVar() const
{
    auto obj = object();
    juce::Array<juce::var> pcs;
    for (const auto& p : pieces)
    {
        auto po = object();
        put(po, "pieceIndex", p.pieceIndex);
        put(po, "material", p.material);
        put(po, "profile", p.profile);
        put(po, "rawStockLengthMm", p.rawStockLengthMm);
        put(po, "widthMm", p.widthMm);
        put(po, "thicknessMm", p.thicknessMm);
        put(po, "usedLengthMm", p.usedLengthMm);
        put(po, "kerfAllowanceMm", p.kerfAllowanceMm);
        put(po, "offcutWasteMm", p.offcutWasteMm);
        put(po, "utilizationPercent", p.utilizationPercent);
        juce::Array<juce::var> cutsArr;
        for (const auto& c : p.cuts)
        {
            auto co = object();
            put(co, "instanceId", c.instanceId);
            put(co, "partName", c.partName);
            put(co, "cutLengthMm", c.cutLengthMm);
            put(co, "positionOnStockMm", c.positionOnStockMm);
            put(co, "cutStartNote", c.cutStartNote);
            put(co, "cutEndNote", c.cutEndNote);
            cutsArr.add(co);
        }
        put(po, "cuts", cutsArr);
        pcs.add(po);
    }
    put(obj, "pieces", pcs);
    put(obj, "totalRawPiecesRequired", totalRawPiecesRequired);
    put(obj, "totalRawLengthMm", totalRawLengthMm);
    put(obj, "totalCutLengthMm", totalCutLengthMm);
    put(obj, "totalWasteLengthMm", totalWasteLengthMm);
    put(obj, "overallUtilizationPercent", overallUtilizationPercent);
    return obj;
}

juce::String CadStockNestingPlan::toMarkdown() const
{
    juce::String s = "### Stock Cutting Optimization & Nesting Plan\n\n";
    s += "> [!NOTE] This optimization plan nests required cuts into standard stock pieces to minimize material waste.\n\n";
    s += "| Stock Piece # | Material | Profile | Raw Length | Finished Cuts Nested | Used (mm) | Offcut Waste (mm) | Yield (%) |\n";
    s += "|---|---|---|---|---|---|---|---|\n";
    for (const auto& p : pieces)
    {
        juce::String cutsSummary;
        for (size_t i = 0; i < p.cuts.size(); ++i)
        {
            if (i > 0) cutsSummary += " + ";
            cutsSummary += p.cuts[i].partName + " (" + juce::String(p.cuts[i].cutLengthMm, 1) + " mm)";
        }
        s += "| Piece #" + juce::String(p.pieceIndex) + " | " + p.material + " | " + p.profile +
             " | " + juce::String(p.rawStockLengthMm, 1) + " mm | " + cutsSummary +
             " | " + juce::String(p.usedLengthMm, 1) + " | " + juce::String(p.offcutWasteMm, 1) +
             " | " + juce::String(p.utilizationPercent, 1) + "% |\n";
    }
    s += "\n";
    s += "**Overall Cutting Efficiency:** " + juce::String(totalRawPiecesRequired) + " raw pieces required, " +
         "Total Finished Length: " + juce::String(totalCutLengthMm, 1) + " mm, " +
         "Total Offcut Scrap: " + juce::String(totalWasteLengthMm, 1) + " mm, " +
         "Overall Material Yield: " + juce::String(overallUtilizationPercent, 1) + "%.\n\n";
    return s;
}

juce::String CadStockNestingPlan::toCSV() const
{
    juce::String s = "PieceIndex,Material,Profile,RawLengthMm,PartInstance,PartName,CutLengthMm,PositionOnStockMm,CutStartNote,CutEndNote\n";
    for (const auto& p : pieces)
    {
        for (const auto& c : p.cuts)
        {
            s += juce::String(p.pieceIndex) + ",\"" + p.material + "\",\"" + p.profile + "\"," +
                 juce::String(p.rawStockLengthMm, 1) + ",\"" + c.instanceId + "\",\"" + c.partName + "\"," +
                 juce::String(c.cutLengthMm, 1) + "," + juce::String(c.positionOnStockMm, 1) + ",\"" +
                 c.cutStartNote + "\",\"" + c.cutEndNote + "\"\n";
        }
    }
    return s;
}

CadStockNestingPlan generateCuttingOptimization(const CadDocument& doc, double sawKerfMm)
{
    CadStockNestingPlan plan;
    const auto& instances = doc.mechanicsData().instances;

    struct NeededCut
    {
        juce::String instanceId;
        juce::String partName;
        juce::String material;
        juce::String profile;
        double origLengthMm = 0.0;
        double finishedLengthMm = 0.0;
        double widthMm = 0.0;
        double thicknessMm = 0.0;
        juce::String startNote, endNote;
    };

    std::vector<NeededCut> allCuts;
    for (const auto& inst : instances)
    {
        const auto* comp = doc.componentFor(inst.featureId);
        if (!comp || !craft::isStock(*comp)) continue;

        const auto g = (*comp)["geometry"];
        const auto profile = g["profile"].toString();
        const double origL = g.getProperty("lengthMm", 0.0);
        const double w = g.getProperty("widthMm", g.getProperty("outerDiameterMm", 0.0));
        const double t = g.getProperty("thicknessMm", 0.0);

        double finishedL = origL;
        const auto* feat = doc.find(inst.featureId);
        if (feat && !feat->shape.IsNull())
        {
            Bnd_Box bbox;
            BRepBndLib::Add(feat->shape, bbox);
            Standard_Real x1, y1, z1, x2, y2, z2;
            bbox.Get(x1, y1, z1, x2, y2, z2);
            finishedL = std::abs(x2 - x1);
        }

        juce::String matName = "Wood";
        if (const auto* assign = doc.materialsData().assignmentFor(inst.featureId))
        {
            if (const auto* mat = doc.materialsData().find(assign->materialId))
                matName = mat->name;
        }

        NeededCut cut;
        cut.instanceId = inst.id;
        cut.partName = (*comp)["name"].toString() + " (" + inst.id + ")";
        cut.material = matName;
        cut.profile = profile;
        cut.origLengthMm = origL > 0.0 ? origL : 120.0;
        cut.finishedLengthMm = finishedL;
        cut.widthMm = w;
        cut.thicknessMm = t;
        cut.startNote = "Square 90°";
        cut.endNote = (finishedL < origL - 0.5) ? "Cut to length" : "Original edge";

        allCuts.push_back(cut);
    }

    // Sort cuts descending by finished length (First-Fit Decreasing algorithm for 1D bin-packing)
    std::sort(allCuts.begin(), allCuts.end(), [](const NeededCut& a, const NeededCut& b){
        return a.finishedLengthMm > b.finishedLengthMm;
    });

    // Group by material & profile & rawStockLength
    for (const auto& cut : allCuts)
    {
        bool placed = false;
        for (auto& piece : plan.pieces)
        {
            if (piece.material == cut.material && piece.profile == cut.profile &&
                std::abs(piece.rawStockLengthMm - cut.origLengthMm) < 1.0)
            {
                const double neededSpace = cut.finishedLengthMm + (piece.cuts.empty() ? 0.0 : sawKerfMm);
                if (piece.usedLengthMm + neededSpace <= piece.rawStockLengthMm + 1e-4)
                {
                    StockPieceCut sc;
                    sc.instanceId = cut.instanceId;
                    sc.partName = cut.partName;
                    sc.cutLengthMm = cut.finishedLengthMm;
                    sc.positionOnStockMm = piece.usedLengthMm + (piece.cuts.empty() ? 0.0 : sawKerfMm);
                    sc.cutStartNote = cut.startNote;
                    sc.cutEndNote = cut.endNote;

                    piece.cuts.push_back(sc);
                    piece.usedLengthMm += neededSpace;
                    if (!piece.cuts.empty() && piece.cuts.size() > 1)
                        piece.kerfAllowanceMm += sawKerfMm;
                    piece.offcutWasteMm = piece.rawStockLengthMm - piece.usedLengthMm;
                    piece.utilizationPercent = (piece.usedLengthMm / piece.rawStockLengthMm) * 100.0;
                    placed = true;
                    break;
                }
            }
        }

        if (!placed)
        {
            // Start a new raw stock piece
            StockPieceNesting piece;
            piece.pieceIndex = (int) plan.pieces.size() + 1;
            piece.material = cut.material;
            piece.profile = cut.profile;
            piece.rawStockLengthMm = cut.origLengthMm;
            piece.widthMm = cut.widthMm;
            piece.thicknessMm = cut.thicknessMm;

            StockPieceCut sc;
            sc.instanceId = cut.instanceId;
            sc.partName = cut.partName;
            sc.cutLengthMm = cut.finishedLengthMm;
            sc.positionOnStockMm = 0.0;
            sc.cutStartNote = cut.startNote;
            sc.cutEndNote = cut.endNote;

            piece.cuts.push_back(sc);
            piece.usedLengthMm = cut.finishedLengthMm;
            piece.offcutWasteMm = piece.rawStockLengthMm - piece.usedLengthMm;
            piece.utilizationPercent = (piece.usedLengthMm / piece.rawStockLengthMm) * 100.0;

            plan.pieces.push_back(piece);
        }
    }

    plan.totalRawPiecesRequired = (int) plan.pieces.size();
    for (const auto& p : plan.pieces)
    {
        plan.totalRawLengthMm += p.rawStockLengthMm;
        plan.totalWasteLengthMm += p.offcutWasteMm;
        for (const auto& c : p.cuts)
            plan.totalCutLengthMm += c.cutLengthMm;
    }
    plan.overallUtilizationPercent = plan.totalRawLengthMm > 0.0 ?
        ((plan.totalCutLengthMm / plan.totalRawLengthMm) * 100.0) : 0.0;

    return plan;
}

// ---------------------------------------------------------------------------------------------------------------
// Assembly Guide Implementation
// ---------------------------------------------------------------------------------------------------------------
juce::var CadAssemblyStep::toVar() const
{
    auto obj = object();
    put(obj, "stepNumber", stepNumber);
    put(obj, "title", title);
    put(obj, "phase", phase);
    juce::Array<juce::var> insts;
    for (const auto& s : targetInstances) insts.add(s);
    put(obj, "targetInstances", insts);
    juce::Array<juce::var> jts;
    for (const auto& j : targetJoints) jts.add(j);
    put(obj, "targetJoints", jts);
    put(obj, "instructions", instructions);
    put(obj, "safetyNotes", safetyNotes);
    return obj;
}

juce::var CadAssemblyGuide::toVar() const
{
    auto obj = object();
    juce::Array<juce::var> arr;
    for (const auto& s : steps) arr.add(s.toVar());
    put(obj, "steps", arr);
    put(obj, "totalSteps", totalSteps);
    return obj;
}

juce::String CadAssemblyGuide::toMarkdown() const
{
    juce::String s = "### Step-by-Step Construction & Assembly Protocol\n\n";
    for (const auto& step : steps)
    {
        s += "#### Step " + juce::String(step.stepNumber) + ": " + step.title + " (" + step.phase + ")\n\n";
        s += step.instructions + "\n\n";
        if (!step.targetInstances.empty())
        {
            s += "- **Components Involved:** `" + juce::StringArray(step.targetInstances.data(), (int)step.targetInstances.size()).joinIntoString("`, `") + "`\n";
        }
        if (!step.targetJoints.empty())
        {
            s += "- **Adhesive Joints:** `" + juce::StringArray(step.targetJoints.data(), (int)step.targetJoints.size()).joinIntoString("`, `") + "`\n";
        }
        if (step.safetyNotes.isNotEmpty())
        {
            s += "> [!IMPORTANT] " + step.safetyNotes + "\n";
        }
        s += "\n";
    }
    return s;
}

CadAssemblyGuide generateAssemblyGuide(const CadDocument& doc, const CadCraftKitMetadata* meta)
{
    CadAssemblyGuide guide;

    // Step 1: Material Preparation & Safety
    CadAssemblyStep s1;
    s1.stepNumber = 1;
    s1.phase = "Preparation";
    s1.title = "Workspace Preparation & Safety Briefing";
    s1.instructions = "Prepare a clean, level work surface covered with wax paper or silicone mat to prevent adhesive bonding to tables. Inspect cutting tools, personal protective equipment (safety glasses), and ensure adequate room ventilation.";
    s1.safetyNotes = "Wear eye protection when trimming craft wood or handling adhesives. Avoid skin contact with cyanoacrylate (super glue).";
    guide.steps.push_back(s1);

    // Step 2: Fabrication & Stock Cutting
    CadAssemblyStep s2;
    s2.stepNumber = 2;
    s2.phase = "Preparation";
    s2.title = "Stock Trimming & Cutting to Dimensions";
    s2.instructions = "Following the cutting list and 1:1 scale fabrication templates, measure and mark all structural members. Cut craft sticks, rods, and tubes to specified lengths using a hobby razor saw or precision miter cutter. Lightly sand cut edges with 220-grit sandpaper.";
    s2.safetyNotes = "Always cut away from hands on a secure cutting mat. Use light multi-pass strokes rather than heavy force to prevent stick splintering.";
    guide.steps.push_back(s2);

    // If custom assembly steps were authored in metadata, incorporate them
    if (meta && !meta->customAssemblySteps.empty())
    {
        int stepNum = 3;
        for (const auto& cust : meta->customAssemblySteps)
        {
            CadAssemblyStep cs;
            cs.stepNumber = stepNum++;
            cs.phase = "Assembly";
            cs.title = "Author-Defined Assembly Task " + juce::String(cs.stepNumber);
            cs.instructions = cust;
            guide.steps.push_back(cs);
        }
    }
    else
    {
        // Infer assembly steps from assembly groups and bonded joints
        const auto& groups = doc.mechanicsData().assemblies;
        int stepCounter = 3;

        if (!groups.empty())
        {
            for (const auto& grp : groups)
            {
                CadAssemblyStep gs;
                gs.stepNumber = stepCounter++;
                gs.phase = "Subassembly";
                gs.title = "Assemble Subassembly: " + (grp.name.isNotEmpty() ? grp.name : grp.id);
                gs.targetInstances = grp.instanceIds;
                gs.instructions = "Align and dry-fit members [" + juce::StringArray(grp.instanceIds.data(), (int)grp.instanceIds.size()).joinIntoString(", ") +
                                  "]. Ensure perpendicularity and verify alignments before applying adhesive.";
                guide.steps.push_back(gs);
            }
        }

        // Joint Bonding Steps
        const auto& joints = doc.mechanicsData().bondedJoints;
        if (!joints.empty())
        {
            CadAssemblyStep bs;
            bs.stepNumber = stepCounter++;
            bs.phase = "Bonding";
            bs.title = "Structural Adhesive Bonding & Clamping";
            bs.instructions = "Apply a thin, uniform film of adhesive to designated contact surfaces. Press components firmly together and clamp with spring clothespins or binder clips. Wipe away excess squeeze-out immediately with a damp paper towel.";
            for (const auto& j : joints)
            {
                if (j.status == "valid")
                {
                    bs.targetJoints.push_back(j.id);
                    bs.targetInstances.push_back(j.instanceA);
                    bs.targetInstances.push_back(j.instanceB);
                }
            }
            bs.safetyNotes = "Allow adhesive to cure fully (minimum 30 minutes handling time for PVA wood glue, 24 hours for full structural load capacity) before testing.";
            guide.steps.push_back(bs);
        }

        // Final Integration & Verification
        CadAssemblyStep fs;
        fs.stepNumber = stepCounter++;
        fs.phase = "Final Assembly";
        fs.title = "Final Assembly Inspection & Geometry Verification";
        fs.instructions = "Check overall assembly squareness, span dimensions, and joint rigidity. Verify that all bonded interfaces are fully seated without gaps or dry spots.";
        guide.steps.push_back(fs);

        // Laboratory Physical Testing Step
        CadAssemblyStep ts;
        ts.stepNumber = stepCounter++;
        ts.phase = "Testing";
        ts.title = "Quality Control & Physical Mechanical Evaluation";
        ts.instructions = "Mount structure in designated testing supports (matching the virtual laboratory boundary conditions). Apply test load incrementally while recording midspan deflection to compare against virtual finite-element predictions.";
        guide.steps.push_back(ts);
    }

    guide.totalSteps = (int) guide.steps.size();
    return guide;
}

// ---------------------------------------------------------------------------------------------------------------
// Fabrication Template SVG Generator
// ---------------------------------------------------------------------------------------------------------------
juce::String generateFabricationTemplateSVG(const CadDocument& doc, const CadStockNestingPlan& plan)
{
    // Standard Letter / A4 width in mm: 210 x 297 mm
    const double svgWidthMm = 210.0;
    const double svgHeightMm = std::max(297.0, 50.0 + plan.pieces.size() * 35.0);

    juce::String svg;
    svg += "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n";
    svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + juce::String(svgWidthMm, 1) + "mm\" height=\"" +
           juce::String(svgHeightMm, 1) + "mm\" viewBox=\"0 0 " + juce::String(svgWidthMm, 1) + " " + juce::String(svgHeightMm, 1) + "\">\n";

    // Stylesheet
    svg += "  <style>\n";
    svg += "    .title { font-family: sans-serif; font-size: 6px; font-weight: bold; fill: #1d222b; }\n";
    svg += "    .subtitle { font-family: sans-serif; font-size: 3.5px; fill: #485562; }\n";
    svg += "    .stock-border { fill: #f8f6f0; stroke: #2a3440; stroke-width: 0.4; }\n";
    svg += "    .cut-line { stroke: #d32f2f; stroke-width: 0.5; stroke-dasharray: 1, 0.5; }\n";
    svg += "    .part-label { font-family: sans-serif; font-size: 2.8px; font-weight: bold; fill: #1a237e; text-anchor: middle; }\n";
    svg += "    .dim-label { font-family: monospace; font-size: 2.2px; fill: #37474f; text-anchor: middle; }\n";
    svg += "    .ruler-tick { stroke: #90a4ae; stroke-width: 0.2; }\n";
    svg += "  </style>\n\n";

    // Header Title Block
    svg += "  <!-- Header Title Block -->\n";
    svg += "  <text x=\"10\" y=\"12\" class=\"title\">DjehutiCAD Craft Designer — 1:1 Scale Fabrication Template</text>\n";
    svg += "  <text x=\"10\" y=\"18\" class=\"subtitle\">Print at 100% scale (no scaling / actual size). Verify calibration bar before cutting.</text>\n\n";

    // 50 mm calibration verification bar
    svg += "  <!-- 50 mm Calibration Bar -->\n";
    svg += "  <line x1=\"10\" y1=\"24\" x2=\"60\" y2=\"24\" stroke=\"#000\" stroke-width=\"0.8\" />\n";
    svg += "  <line x1=\"10\" y1=\"21\" x2=\"10\" y2=\"27\" stroke=\"#000\" stroke-width=\"0.8\" />\n";
    svg += "  <line x1=\"60\" y1=\"21\" x2=\"60\" y2=\"27\" stroke=\"#000\" stroke-width=\"0.8\" />\n";
    svg += "  <text x=\"35\" y=\"22\" class=\"dim-label\">50.0 mm calibration check</text>\n\n";

    double startY = 36.0;
    for (const auto& piece : plan.pieces)
    {
        const double stockWidthMm = std::max(6.0, std::min(15.0, piece.widthMm > 0 ? piece.widthMm : 10.0));
        const double stockLengthMm = piece.rawStockLengthMm > 0 ? piece.rawStockLengthMm : 120.0;
        const double x0 = 15.0;
        const double y0 = startY;

        svg += "  <!-- Stock Piece #" + juce::String(piece.pieceIndex) + " (" + piece.material + ") -->\n";
        svg += "  <text x=\"" + juce::String(x0, 1) + "\" y=\"" + juce::String(y0 - 2.5, 1) + "\" class=\"dim-label\" text-anchor=\"start\">Piece #" +
               juce::String(piece.pieceIndex) + ": " + piece.material + " " + piece.profile + " (" +
               juce::String(stockLengthMm, 0) + " mm raw)</text>\n";

        // Raw stock background rectangle
        svg += "  <rect x=\"" + juce::String(x0, 1) + "\" y=\"" + juce::String(y0, 1) + "\" width=\"" +
               juce::String(stockLengthMm, 1) + "\" height=\"" + juce::String(stockWidthMm, 1) + "\" class=\"stock-border\" />\n";

        // Draw millimeter ticks along top edge of stock
        for (double tick = 0.0; tick <= stockLengthMm; tick += 10.0)
        {
            const double tickH = (fmod(tick, 50.0) == 0.0) ? 2.5 : 1.2;
            svg += "  <line x1=\"" + juce::String(x0 + tick, 1) + "\" y1=\"" + juce::String(y0, 1) + "\" x2=\"" +
                   juce::String(x0 + tick, 1) + "\" y2=\"" + juce::String(y0 + tickH, 1) + "\" class=\"ruler-tick\" />\n";
        }

        // Draw nested cuts
        double currentPos = 0.0;
        for (const auto& cut : piece.cuts)
        {
            const double cutX = x0 + currentPos;
            const double cutW = cut.cutLengthMm;
            const double cutMidX = cutX + cutW * 0.5;

            // Cut divider line at end of piece
            if (currentPos + cutW < stockLengthMm - 0.5)
            {
                svg += "  <line x1=\"" + juce::String(cutX + cutW, 1) + "\" y1=\"" + juce::String(y0, 1) + "\" x2=\"" +
                       juce::String(cutX + cutW, 1) + "\" y2=\"" + juce::String(y0 + stockWidthMm, 1) + "\" class=\"cut-line\" />\n";
            }

            // Part label and length
            svg += "  <text x=\"" + juce::String(cutMidX, 1) + "\" y=\"" + juce::String(y0 + stockWidthMm * 0.5 + 1.0, 1) + "\" class=\"part-label\">" +
                   cut.partName + "</text>\n";
            svg += "  <text x=\"" + juce::String(cutMidX, 1) + "\" y=\"" + juce::String(y0 + stockWidthMm + 3.0, 1) + "\" class=\"dim-label\">" +
                   juce::String(cutW, 1) + " mm</text>\n";

            currentPos += cutW + 1.0; // include kerf
        }

        // Offcut scrap label
        if (piece.offcutWasteMm > 2.0)
        {
            const double offcutX = x0 + currentPos;
            const double offcutW = stockLengthMm - currentPos;
            svg += "  <rect x=\"" + juce::String(offcutX, 1) + "\" y=\"" + juce::String(y0, 1) + "\" width=\"" +
                   juce::String(offcutW, 1) + "\" height=\"" + juce::String(stockWidthMm, 1) + "\" fill=\"#efebe9\" stroke=\"none\" />\n";
            svg += "  <text x=\"" + juce::String(offcutX + offcutW * 0.5, 1) + "\" y=\"" + juce::String(y0 + stockWidthMm * 0.5 + 1.0, 1) + "\" class=\"dim-label\">Scrap</text>\n";
        }

        startY += stockWidthMm + 18.0;
    }

    svg += "</svg>\n";
    return svg;
}

// ---------------------------------------------------------------------------------------------------------------
// Educational Project Documentation Generator
// ---------------------------------------------------------------------------------------------------------------
juce::String generateEducationalProjectReport(const CadDocument& doc,
                                              const CadCraftKitMetadata& kitMeta,
                                              const structural::StructuralResult* analysisResult,
                                              const structural::ExperimentComparison* labComparison)
{
    const auto bom = generateBOM(doc);
    const auto plan = generateCuttingOptimization(doc, 1.0);
    const auto guide = generateAssemblyGuide(doc, &kitMeta);

    juce::String r = "# " + kitMeta.name + "\n\n";
    r += "**Document Revision:** " + kitMeta.revision + " | **Author:** " + kitMeta.author + " | **Audience:** " + kitMeta.audience + "\n\n";
    r += "> " + kitMeta.description + "\n\n";

    // 1. Objectives
    r += "## 1. Project Overview & Educational Objectives\n";
    r += "This project kit guides students and makers through the complete engineering design loop:\n";
    r += "1. **Parametric Modeling:** Authoring craft structures with slender structural members in DjehutiCAD.\n";
    r += "2. **Virtual Finite-Element Testing:** Simulating static equilibrium, member forces, and elastic deflection under applied load.\n";
    r += "3. **Precise Fabrication:** Measuring and cutting stock to size using 1:1 templates with stock nesting optimization.\n";
    r += "4. **Adhesive Assembly:** Bonding components with engineered adhesives and verifying joint integrity.\n";
    r += "5. **Empirical Validation:** Conducting physical load testing to compare actual mechanical deflections against virtual predictions.\n\n";

    // 2. Bill of Materials
    r += "## 2. Bill of Materials & Stock Inventory\n";
    r += bom.toMarkdown();

    // 3. Cutting & Nesting Plan
    r += "## 3. Stock Cutting & Fabrication Guide\n";
    r += plan.toMarkdown();

    // 4. Assembly Guide
    r += "## 4. Construction & Assembly Protocol\n";
    r += guide.toMarkdown();

    // 5. Theoretical Assumptions
    r += "## 5. Structural Mechanics & Theoretical Assumptions\n";
    r += "- **Formulation:** 3D Euler-Bernoulli linear-elastic beam and frame theory (6 degrees of freedom per node: $u_x, u_y, u_z, \\theta_x, \\theta_y, \\theta_z$).\n";
    r += "- **Kinematics:** Small-displacement assumption; axial, torsion, and biaxial flexural response.\n";
    r += "- **Joint Connectivity:** Valid Phase 3 adhesive joints provide rigid continuous load transfer across bonding interfaces.\n";
    r += "- **Material Model:** Linear-elastic longitudinal properties aligned with stock grain.\n\n";

    // 6. Virtual FEA Predictions
    r += "## 6. Virtual Structural Analysis Predictions\n";
    if (analysisResult != nullptr && analysisResult->solved)
    {
        r += "- **Peak Predicted Deflection:** " + juce::String(analysisResult->maxDeflectionM * 1000.0, 3) + " mm\n";
        r += "- **Peak Normal Stress:** " + juce::String(analysisResult->maxNormalStressPa / 1e6, 2) + " MPa\n";
        r += "- **Maximum Stress Utilization:** " + juce::String(analysisResult->maxUtilizationRatio * 100.0, 1) + "%\n";
        r += "- **Total Equilibrium Reaction Force:** Rx=" + juce::String(analysisResult->totalReactionForceN[0], 2) + " N, " +
             "Ry=" + juce::String(analysisResult->totalReactionForceN[1], 2) + " N, " +
             "Rz=" + juce::String(analysisResult->totalReactionForceN[2], 2) + " N\n\n";

        r += "### Predicted Member Forces & Utilization\n";
        r += "| Member / Instance | Axial Force (N) | Max Bending Moment (N*m) | Peak Normal Stress (MPa) | Utilization (%) |\n";
        r += "|---|---|---|---|---|\n";
        for (const auto& m : analysisResult->memberResults)
        {
            r += "| `" + m.instanceId + "` | " + juce::String(m.axialForceN, 2) + " | " +
                 juce::String(m.maxBendingMomentNm, 3) + " | " +
                 juce::String(m.maxNormalStressPa / 1e6, 2) + " | " +
                 juce::String(m.utilizationRatio * 100.0, 1) + "% |\n";
        }
        r += "\n";
    }
    else
    {
        r += "*Virtual simulation results are not yet recorded for this project model. Run the Phase 4/5 solver in CreationDock to populate predictions.*\n\n";
    }

    if (labComparison != nullptr)
    {
        r += "### Virtual Laboratory Design Comparison\n";
        r += "- **Baseline Test (A):** `" + labComparison->nameA + "` -> " + juce::String(labComparison->deflectionA_Mm, 3) + " mm deflection\n";
        r += "- **Comparison Test (B):** `" + labComparison->nameB + "` -> " + juce::String(labComparison->deflectionB_Mm, 3) + " mm deflection\n";
        r += "- **Delta Deflection:** " + juce::String(labComparison->deltaDeflectionMm, 3) + " mm (" +
             (labComparison->percentDeflectionChange > 0 ? "+" : "") + juce::String(labComparison->percentDeflectionChange, 1) + "%)\n";
        r += "- **Comparison Finding:** " + labComparison->summary + "\n\n";
    }

    // 7. Physical Testing Protocol
    r += "## 7. Hands-On Physical Testing Protocol\n";
    r += "Follow this classroom testing procedure to experimentally evaluate the physical construction:\n\n";
    r += "1. **Support Setup:** Place the cured craft assembly onto support blocks matching the modeled spans and boundary conditions.\n";
    r += "2. **Measurement Baseline:** Position a dial indicator or zero a digital caliper against the bottom of the midspan loading point. Record initial zero reading ($y_0$).\n";
    r += "3. **Incremental Loading:** Hang calibrated test weights (e.g., 200 g, 500 g, 1000 g increments) from the designated loading saddle.\n";
    r += "4. **Deflection Measurement:** Record the steady-state deflection ($y_i$) under each weight. Ensure the weight is settled and not swinging.\n";
    r += "5. **Recovery Check:** Remove all weights and verify whether the structure returns to its original baseline position (verifying elastic vs. permanent plastic deformation).\n\n";

    // 8. Virtual vs. Physical Measurement Comparison Worksheet
    r += "## 8. Virtual Simulation vs. Physical Measurement Comparison Worksheet\n";
    r += "Record student empirical laboratory measurements and calculate discrepancy against the DjehutiCAD virtual prediction:\n\n";
    r += "| Load Increment (N) | Virtual FEA Prediction (mm) | Physical Measured Deflection (mm) | Error (mm) | Percent Discrepancy (%) |\n";
    r += "|---|---|---|---|---|\n";
    r += "| 2.0 N | _(from virtual FEA)_ | ________________ | ____________ | ____________ |\n";
    r += "| 5.0 N | _(from virtual FEA)_ | ________________ | ____________ | ____________ |\n";
    r += "| 10.0 N | _(from virtual FEA)_ | ________________ | ____________ | ____________ |\n\n";

    r += "### Discussion & Real-World Discrepancy Analysis\n";
    r += "In professional engineering practice, physical test results rarely match virtual linear-elastic models perfectly. Consider the following factors:\n";
    r += "- **Wood Grain & Orthotropy:** Craft sticks are natural wood with anisotropic fibers. Bending stiffness varies when grain is misaligned with the member axis.\n";
    r += "- **Adhesive Compliance & Film Thickness:** Real PVA glue joints have slight compliance and uneven thickness, unlike the idealized rigid nodes of theoretical FEA.\n";
    r += "- **Support Settlement:** Support blocks or test tables may slightly deflect or settle under load, adding apparent displacement.\n";
    r += "- **Moisture & Environmental Conditions:** Wood stiffness ($E$) changes with ambient relative humidity.\n\n";

    if (kitMeta.notes.isNotEmpty())
    {
        r += "## 9. Additional Notes & Instructions\n";
        r += kitMeta.notes + "\n\n";
    }

    return r;
}

// ---------------------------------------------------------------------------------------------------------------
// Complete Project Kit Exporter
// ---------------------------------------------------------------------------------------------------------------
bool exportCompleteProjectKit(const CadDocument& doc,
                              const CadCraftKitMetadata& kitMeta,
                              const juce::File& outputFolder,
                              juce::String& error,
                              const structural::StructuralResult* analysisResult,
                              const structural::ExperimentComparison* labComparison)
{
    if (!outputFolder.createDirectory())
    {
        error = "Failed to create project kit directory: " + outputFolder.getFullPathName();
        return false;
    }

    // 1. Generate BOM
    const auto bom = generateBOM(doc);
    const auto bomCsvFile = outputFolder.getChildFile("Bill-of-Materials.csv");
    if (!bomCsvFile.replaceWithText(bom.toCSV()))
    {
        error = "Failed to write Bill-of-Materials.csv";
        return false;
    }

    // 2. Generate Cutting Optimization
    const auto plan = generateCuttingOptimization(doc, 1.0);
    const auto cutCsvFile = outputFolder.getChildFile("Cutting-List.csv");
    if (!cutCsvFile.replaceWithText(plan.toCSV()))
    {
        error = "Failed to write Cutting-List.csv";
        return false;
    }

    // 3. Generate 1:1 Scale SVG Fabrication Template
    const auto svgContent = generateFabricationTemplateSVG(doc, plan);
    const auto svgFile = outputFolder.getChildFile("Fabrication-Templates.svg");
    if (!svgFile.replaceWithText(svgContent))
    {
        error = "Failed to write Fabrication-Templates.svg";
        return false;
    }

    // 4. Generate Comprehensive Educational Project Report (Markdown)
    const auto reportContent = generateEducationalProjectReport(doc, kitMeta, analysisResult, labComparison);
    const auto reportFile = outputFolder.getChildFile("Project-Report.md");
    if (!reportFile.replaceWithText(reportContent))
    {
        error = "Failed to write Project-Report.md";
        return false;
    }

    // 5. Generate Kit Manifest (JSON)
    auto manifest = object();
    put(manifest, "kit", kitMeta.toVar());
    put(manifest, "bom", bom.toVar());
    put(manifest, "nestingPlan", plan.toVar());
    if (analysisResult != nullptr)
        put(manifest, "analysisResult", analysisResult->toVar());
    put(manifest, "generatedDate", juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S"));

    const auto manifestFile = outputFolder.getChildFile("kit-manifest.json");
    if (!manifestFile.replaceWithText(juce::JSON::toString(manifest, true)))
    {
        error = "Failed to write kit-manifest.json";
        return false;
    }

    // 6. Save native CAD document snapshot
    const auto nativeDocFile = outputFolder.getChildFile("Project-Model.djo");
    CadDocument saveCopy = doc;
    if (!saveCopy.save(nativeDocFile, error))
    {
        return false;
    }

    return true;
}

} // namespace djehuti::cad::kit
