// Working cursor, construction references and reference-based transforms (--cad-construction-reference-check).
// Production panel, viewport mouse handlers and document commands; synthetic events, no native pointer automation.
#include "DjehutiAgent.h"
#include "FrustEngine.h"
#include "NodeCompiler.h"
extern "C" const char* djehuti_cad_reference(const char* argsJson);
extern "C" const char* djehuti_cad_transform(const char* argsJson);

bool runConstructionReferenceCheck(juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks; int failures = 0; juce::String error;
    const auto progress = juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-construction-reference-check.progress.log"); progress.replaceWithText({});
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile("build/construction-reference-check"); folder.createDirectory();
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto check = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto* v = new juce::DynamicObject(); v->setProperty("name", name); v->setProperty("ok", ok); if (!ok) { v->setProperty("detail", detail); ++failures; } checks.add(v);
        progress.appendText(juce::String((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1) + " s " + (ok ? "PASS " : "FAIL ") + name + (ok ? juce::String() : " :: " + detail) + "\n");
    };
    const auto closeTo = [](double a, double b, double t) { return std::isfinite(a) && std::abs(a - b) <= t; };
    const auto json = [](const juce::String& text) { return juce::JSON::parse(text); };
    const auto request = [&](const char* action, const juce::String& id, const juce::String& parameters)
    { auto r = juce::var(new juce::DynamicObject()); r.getDynamicObject()->setProperty("action", action); if (id.isNotEmpty()) r.getDynamicObject()->setProperty("id", id); if (parameters.isNotEmpty()) r.getDynamicObject()->setProperty("parameters", juce::JSON::parse(parameters)); return r; };
    const auto world = [&](const CadCommandService& s, const juce::String& id) { return s.document().resolveConstruction(id); };
    const auto originOf = [](const CadResolvedRef& r) { return gp_Pnt(r.world.x, r.world.y, r.world.z); };
    const auto cursorOp = [&](CadWorkspacePanel& p, const juce::String& text) { auto a = juce::JSON::parse(text); a.getDynamicObject()->setProperty("action", "cursor"); return p.automation(a); };

    auto service = std::make_shared<CadCommandService>();
    CadWorkspacePanel panel(service); panel.setSize(1500, 950);
    auto selection = std::make_shared<CadSelection>(); panel.setSelection(selection);
    auto& view = panel.modelView;
    const auto mouse = [&](juce::Point<float> p) { return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, &view, &view, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false); };
    const auto click = [&](juce::Point<float> p) { view.mouseDown(mouse(p)); view.mouseUp(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(), 1, 0, 0, 0, 0, &view, &view, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false)); };

    // 1-2. Working cursor: exact coordinates, snapping to existing geometry (through the viewport's own click handler).
    juce::String box; service->createBox(40, 20, 10, box, error); service->setPlacement(box, 100, 50, 0, 0, 0, 0, error);
    panel.refreshFromDocument(true); view.fitAll();
    auto state = cursorOp(panel, R"({"operation":"set","x":12.5,"y":-3,"z":7.25})");
    check((bool) state["ok"] && closeTo(state["state"]["cursor"]["x"], 12.5, 1e-12) && closeTo(state["state"]["cursor"]["z"], 7.25, 1e-12), "The working cursor takes exact coordinates", juce::JSON::toString(state["state"]["cursor"]));
    check(!(bool) cursorOp(panel, R"({"operation":"set","x":"abc"})")["ok"], "Non-numeric cursor coordinates are refused");
    state = cursorOp(panel, R"({"operation":"reset"})");
    check(closeTo(state["state"]["cursor"]["x"], 0, 0) && closeTo(state["state"]["cursor"]["y"], 0, 0) && closeTo(state["state"]["cursor"]["z"], 0, 0), "Reset returns the cursor to the modeling origin");
    const auto points = service->document().snapPoints(box);
    int vertices = 0, midpoints = 0, faceCentres = 0; for (const auto& p : points) { vertices += p.type == "vertex"; midpoints += p.type == "midpoint"; faceCentres += p.type == "faceCentre"; }
    check(vertices == 8 && midpoints == 12 && faceCentres == 6, "A box offers 8 vertices, 12 edge midpoints and 6 face centres", juce::String(vertices) + "/" + juce::String(midpoints) + "/" + juce::String(faceCentres));
    view.setStandardView("isometric"); view.fitAll(); panel.updateConstructionDisplay(true);
    gp_Pnt corner; for (const auto& p : points) if (p.type == "vertex" && closeTo(p.point[0], 140, 1e-9) && closeTo(p.point[1], 70, 1e-9) && closeTo(p.point[2], 10, 1e-9)) corner = gp_Pnt(p.point[0], p.point[1], p.point[2]);
    juce::Point<float> corner2d; check(view.worldToScreen({ (float) corner.X(), (float) corner.Y(), (float) corner.Z() }, corner2d), "The target vertex is on screen");
    panel.executeUiCommand("cursorPlace"); check(view.isCursorPlacing(), "Place by clicking arms the viewport");
    click(corner2d + juce::Point<float>(3, 2));
    check(!view.isCursorPlacing() && closeTo(panel.cursorWorld[0], 140, 1e-6) && closeTo(panel.cursorWorld[1], 70, 1e-6) && closeTo(panel.cursorWorld[2], 10, 1e-6), "A click near a vertex snaps the cursor to it exactly", juce::String(panel.cursorWorld[0]) + "," + juce::String(panel.cursorWorld[1]) + "," + juce::String(panel.cursorWorld[2]));
    state = cursorOp(panel, "{\"operation\":\"snap\",\"body\":\"" + box + "\",\"type\":\"faceCentre\",\"index\":0}");
    check((bool) state["ok"] && state["state"]["cursor"]["note"].toString().contains("centre"), "The agent can snap the cursor to a listed face centre", state["state"]["cursor"]["note"].toString());
    panel.setCursor({ 0, 0, 25 }, "test"); panel.executeUiCommand("cursorPlace");
    click({ 40, 40 });   // empty space: no surface or snap point, so the cursor sits on the XY plane at its current Z
    check(closeTo(panel.cursorWorld[2], 25, 1e-9), "Away from geometry the cursor stays on the explicit XY plane at its Z", juce::String(panel.cursorWorld[2]));
    const auto revisionBeforeCursor = service->document().revision(); const bool modifiedBeforeCursor = service->document().modified(); panel.setCursor({ 1, 2, 3 }, "typed");
    check(service->document().revision() == revisionBeforeCursor && service->document().modified() == modifiedBeforeCursor, "Moving the cursor never changes the document");

    // 3. Persistent reference from the cursor, through the real task panel.
    panel.setCursor({ 140, 70, 10 }, "vertex"); panel.executeUiCommand("cursor");
    panel.automation(json(R"({"action":"set","field":"cursorRefKind","value":"Coordinate system"})"));
    state = panel.automation(json(R"({"action":"click","button":"cursorRefCreate"})"));
    const auto& refs = service->document().constructionData();
    check(refs.size() == 1 && refs[0].kind == "cs" && refs[0].association == "fixed", "Create reference at cursor makes a fixed coordinate system", juce::JSON::toString(state).substring(0, 200));
    const auto cursorRefId = refs.empty() ? juce::String() : refs[0].id;
    check(closeTo(world(*service, cursorRefId).world.x, 140, 1e-9) && closeTo(world(*service, cursorRefId).world.z, 10, 1e-9), "The reference sits exactly at the cursor");

    // 4. Save and reopen: fixed reference survives in .djo and .djc.
    for (const auto* ext : { "djo", "djc" })
    {
        CadCommandService loaded; const auto file = folder.getChildFile(juce::String("refs.") + ext);
        const bool ok = service->save(file, error) && loaded.load(file, error);
        const auto r = loaded.document().resolveConstruction(cursorRefId);
        check(ok && r.ok && closeTo(r.world.x, 140, 1e-9) && closeTo(r.world.y, 70, 1e-9) && closeTo(r.world.z, 10, 1e-9) && loaded.document().findConstruction(cursorRefId)->kind == "cs", juce::String("Save and reopen (.") + ext + ") keeps the reference with its id, kind and coordinates", error);
    }

    // 5-8. Mechanical example: a MOFLON slip ring from the library. Axis from its cylindrical housing, rotate a
    // bracket about that axis, move the slip ring and watch the references follow, then copy it.
    juce::String stator;
    {
        service->setComponentLibraryFile(folder.getChildFile("library-" + juce::Uuid().toDashedString() + ".json"));
        auto insert = juce::JSON::parse(R"({"action":"insert","component":"component-slipring-moflon-mmc182","placement":{"x":30,"y":0,"z":0,"rx":0,"ry":0,"rz":0}})");
        const auto r = service->componentCommand(insert);
        for (const auto& i : service->document().mechanicsData().instances) if (i.grounded) stator = i.id;
        check((bool) r["ok"] && stator.isNotEmpty(), "Insert the MOFLON MMC182 slip ring (stator and rotor) from the library", juce::JSON::toString(r).substring(0, 200));
    }
    panel.refreshFromDocument(true);
    const auto* statorInstance = service->document().mechanicsData().findInstance(stator);
    std::vector<CadFaceInfo> faces; service->listFaces(statorInstance->featureId, faces, error);
    juce::String housingFace; for (const auto& f : faces) if (!f.planar && f.token.isNotEmpty()) housingFace = f.token;
    check(housingFace.isNotEmpty(), "The slip-ring housing has a named cylindrical face", housingFace);
    auto reply = service->referenceCommand(request("create", {}, "{\"kind\":\"axis\",\"name\":\"Slip ring axis\",\"association\":{\"type\":\"cylinderAxis\",\"source\":\"" + stator + "\",\"face\":\"" + housingFace + "\"}}"));
    const auto axisId = reply["id"].toString();
    auto axis = world(*service, axisId);
    check((bool) reply["ok"] && axis.ok && closeTo(axis.world.x, 30, 1e-6) && closeTo(axis.world.y, 0, 1e-6), "An axis created from the cylindrical face lies on the housing axis (x 30, y 0)", reply["error"].toString() + " " + juce::JSON::toString(reply["reference"]["world"]));
    const auto dir = gp_Dir(0, 0, 1).Transformed(axis.world.toTrsf());
    check(closeTo(std::abs(dir.Z()), 1.0, 1e-9), "The axis direction is the housing's Z axis");
    reply = service->referenceCommand(request("create", {}, "{\"kind\":\"cs\",\"name\":\"Slip ring mount\",\"association\":{\"type\":\"frame\",\"source\":\"" + stator + "\"}}"));
    const auto mountId = reply["id"].toString();
    check((bool) reply["ok"] && closeTo(world(*service, mountId).world.x, 30, 1e-9), "A coordinate system follows the slip ring occurrence (starts on its frame)", reply["error"].toString());
    juce::String bracket; service->createBox(20, 10, 5, bracket, error); service->setPlacement(bracket, 50, 0, 0, 0, 0, 0, error);
    reply = service->transformCommand(json("{\"operation\":\"rotate\",\"target\":\"" + bracket + "\",\"axis\":\"" + axisId + "\",\"angleDegrees\":90}"));
    const auto* b = service->document().find(bracket);
    check((bool) reply["ok"] && closeTo(b->x, 30, 1e-6) && closeTo(b->y, 20, 1e-6) && closeTo(b->rz, 90, 1e-6), "Rotating a bracket 90 degrees about the slip-ring axis swings it from (50,0) to (30,20)", reply["error"].toString() + " " + juce::String(b->x) + "," + juce::String(b->y) + " rz " + juce::String(b->rz));
    service->undo(error);
    check(closeTo(service->document().find(bracket)->x, 50, 1e-9) && closeTo(service->document().find(bracket)->rz, 0, 1e-9), "Undo restores the bracket's placement");
    service->redo(error);
    check(closeTo(service->document().find(bracket)->y, 20, 1e-6), "Redo reapplies the rotation");

    const double axisZBefore = axis.world.z;
    reply = service->transformCommand(json("{\"operation\":\"move\",\"target\":\"" + stator + "\",\"displacement\":[0,0,15]}"));
    axis = world(*service, axisId); const auto mount = world(*service, mountId);
    check((bool) reply["ok"] && closeTo(axis.world.z, axisZBefore + 15, 1e-6) && closeTo(mount.world.z, 15, 1e-9) && closeTo(axis.world.x, 30, 1e-6), "Moving the slip ring 15 mm up: the associated axis and coordinate system follow", reply["error"].toString() + " mount z " + juce::String(mount.world.z) + " axis z " + juce::String(axis.world.z));
    check(closeTo(world(*service, cursorRefId).world.z, 10, 1e-12), "A fixed reference stays where it was");
    {
        const auto beforeCopy = service->document().mechanicsData().instances.size(); const auto original = service->document().mechanicsData().findInstance(stator)->placement;
        reply = service->transformCommand(json("{\"operation\":\"move\",\"target\":\"" + stator + "\",\"displacement\":[0,40,0],\"copy\":true}"));
        const auto& m = service->document().mechanicsData(); const auto* copy = m.findInstance(reply["copyId"].toString());
        check((bool) reply["ok"] && m.instances.size() == beforeCopy + 1 && copy != nullptr && copy->featureId == m.findInstance(stator)->featureId && closeTo(copy->placement.y, 40, 1e-9), "Create copy makes another occurrence of the same definition 40 mm away", reply["error"].toString());
        check(closeTo(m.findInstance(stator)->placement.y, original.y, 0) && closeTo(m.findInstance(stator)->placement.z, original.z, 0), "The original occurrence is untouched by a copy");
        check(service->document().componentData()["definitions"].size() == 2, "A copy shares the definition (no new component definition appears)", juce::String(service->document().componentData()["definitions"].size()));
        service->undo(error); check(service->document().mechanicsData().instances.size() == beforeCopy, "One undo removes the copy");
        service->redo(error);
    }
    reply = service->transformCommand(json("{\"operation\":\"rotate\",\"target\":\"" + bracket + "\",\"axis\":\"" + axisId + "\",\"angleDegrees\":90,\"copy\":true}"));
    b = service->document().find(bracket); const auto* bc = service->document().find(reply["copyId"].toString());
    check((bool) reply["ok"] && bc != nullptr && closeTo(b->y, 20, 1e-6) && closeTo(bc->x, 10, 1e-6) && closeTo(bc->y, 0, 1e-6), "A rotated body copy leaves the original in place and appears 90 degrees further round", reply["error"].toString());

    // Mixed operations: move to reference, align cs/axis/plane/point, mirror, scale; and refusals.
    reply = service->referenceCommand(request("create", {}, R"({"kind":"point","name":"Target point","origin":[200,100,50]})"));
    const auto pointId = reply["id"].toString();
    juce::String partBox; service->createBox(10, 10, 10, partBox, error);
    reply = service->referenceCommand(request("create", {}, "{\"kind\":\"cs\",\"name\":\"Part frame\",\"origin\":[5,5,10],\"rotationDegrees\":[0,0,0],\"association\":{\"type\":\"frame\",\"source\":\"" + partBox + "\"}}"));
    const auto partCs = reply["id"].toString();
    reply = service->referenceCommand(request("create", {}, R"({"kind":"cs","name":"Target frame","origin":[100,50,25],"rotationDegrees":[0,0,90]})"));
    const auto targetCs = reply["id"].toString();
    reply = service->transformCommand(json("{\"operation\":\"align\",\"target\":\"" + partBox + "\",\"source\":\"" + partCs + "\",\"to\":\"" + targetCs + "\"}"));
    {
        const auto a = world(*service, partCs), t = world(*service, targetCs);
        const auto aq = a.world.toTrsf().GetRotation(), tq = t.world.toTrsf().GetRotation();
        check((bool) reply["ok"] && closeTo(a.world.x, 100, 1e-6) && closeTo(a.world.y, 50, 1e-6) && closeTo(a.world.z, 25, 1e-6) && std::abs(aq.X() * tq.X() + aq.Y() * tq.Y() + aq.Z() * tq.Z() + aq.W() * tq.W()) > 1.0 - 1e-9, "Align makes the part's coordinate system coincide exactly with the target (origin and orientation)", reply["error"].toString());
    }
    reply = service->transformCommand(json("{\"operation\":\"move\",\"target\":\"" + partBox + "\",\"toReference\":\"" + pointId + "\",\"fromReference\":\"" + partCs + "\"}"));
    check((bool) reply["ok"] && closeTo(world(*service, partCs).world.x, 200, 1e-6) && closeTo(world(*service, partCs).world.y, 100, 1e-6) && closeTo(world(*service, partCs).world.z, 50, 1e-6), "Move to a reference lands the part's reference on the target point", reply["error"].toString());
    reply = service->transformCommand(json("{\"operation\":\"move\",\"target\":\"" + partBox + "\",\"displacement\":[10,0,0],\"frame\":\"" + targetCs + "\"}"));
    check((bool) reply["ok"] && closeTo(world(*service, partCs).world.x, 200, 1e-6) && closeTo(world(*service, partCs).world.y, 110, 1e-6), "A displacement in a rotated reference frame follows that frame's axes (its X is world Y)", reply["error"].toString() + " " + juce::String(world(*service, partCs).world.x) + "," + juce::String(world(*service, partCs).world.y));
    {   // axis alignment: concentric
        juce::String peg; service->createCylinder(3, 30, peg, error); service->setPlacement(peg, 10, 0, 0, 0, 0, 0, error);
        auto r1 = service->referenceCommand(request("create", {}, "{\"kind\":\"axis\",\"name\":\"Peg axis\",\"origin\":[10,0,0],\"direction\":[0,0,1],\"association\":{\"type\":\"frame\",\"source\":\"" + peg + "\"}}"));
        auto r2 = service->referenceCommand(request("create", {}, R"({"kind":"axis","name":"Hole axis","origin":[300,40,0],"direction":[1,0,0]})"));
        reply = service->transformCommand(json("{\"operation\":\"align\",\"target\":\"" + peg + "\",\"source\":\"" + r1["id"].toString() + "\",\"to\":\"" + r2["id"].toString() + "\"}"));
        const auto a = world(*service, r1["id"].toString()); const auto d = gp_Dir(0, 0, 1).Transformed(a.world.toTrsf());
        check((bool) reply["ok"] && closeTo(std::abs(d.X()), 1.0, 1e-9) && closeTo(a.world.y, 40, 1e-6) && closeTo(a.world.z, 0, 1e-6), "Aligning axes makes them concentric (parallel and on the same line) without sliding along it", reply["error"].toString() + " y " + juce::String(a.world.y));
    }
    {   // plane alignment: coplanar
        juce::String plate; service->createBox(20, 20, 4, plate, error);
        auto r1 = service->referenceCommand(request("create", {}, "{\"kind\":\"plane\",\"name\":\"Plate top\",\"origin\":[10,10,4],\"direction\":[0,0,1],\"association\":{\"type\":\"frame\",\"source\":\"" + plate + "\"}}"));
        auto r2 = service->referenceCommand(request("create", {}, R"({"kind":"plane","name":"Datum","origin":[0,0,60],"direction":[0,0,1]})"));
        reply = service->transformCommand(json("{\"operation\":\"align\",\"target\":\"" + plate + "\",\"source\":\"" + r1["id"].toString() + "\",\"to\":\"" + r2["id"].toString() + "\"}"));
        check((bool) reply["ok"] && closeTo(world(*service, r1["id"].toString()).world.z, 60, 1e-9) && closeTo(world(*service, r1["id"].toString()).world.x, 10, 1e-9), "Aligning planes makes them coplanar, moving only along the normal", reply["error"].toString());
    }
    {   // mirror and scale
        juce::String blank; service->createBox(10, 10, 10, blank, error); service->setPlacement(blank, 20, 0, 0, 0, 0, 0, error);
        auto planeRef = service->referenceCommand(request("create", {}, R"({"kind":"plane","name":"Mirror plane","origin":[0,0,0],"direction":[1,0,0]})"));
        const auto featuresBefore = service->document().features().size();
        reply = service->transformCommand(json("{\"operation\":\"mirror\",\"target\":\"" + blank + "\",\"plane\":\"" + planeRef["id"].toString() + "\",\"copy\":true}"));
        const auto* mirrored = service->document().find(reply["id"].toString());
        GProp_GProps volumeProps; if (mirrored != nullptr) BRepGProp::VolumeProperties(mirrored->shape, volumeProps);
        check((bool) reply["ok"] && service->document().features().size() == featuresBefore + 1 && service->document().find(blank) != nullptr && closeTo(volumeProps.Mass(), 2000.0, 1e-3), "Mirror with Create copy keeps the source geometry and adds its mirror image (two boxes)", reply["error"].toString() + " vol " + juce::String(volumeProps.Mass()));
        auto movedPlane = service->referenceCommand(request("create", {}, R"({"kind":"plane","name":"Second mirror plane","origin":[0,0,0],"direction":[0,1,0]})"));
        juce::String other; service->createBox(10, 10, 10, other, error); service->setPlacement(other, 0, 20, 0, 0, 0, 0, error);
        reply = service->transformCommand(json("{\"operation\":\"mirror\",\"target\":\"" + other + "\",\"plane\":\"" + movedPlane["id"].toString() + "\"}"));
        const auto* single = service->document().find(reply["id"].toString()); GProp_GProps singleProps; if (single != nullptr) BRepGProp::VolumeProperties(single->shape, singleProps);
        check((bool) reply["ok"] && closeTo(singleProps.Mass(), 1000.0, 1e-3), "Mirror without Create copy yields only the mirrored body (one box)", reply["error"].toString());
        juce::String small; service->createBox(10, 10, 10, small, error); service->setPlacement(small, 10, 0, 0, 0, 0, 0, error);
        auto centre = service->referenceCommand(request("create", {}, R"({"kind":"point","name":"Scale centre","origin":[0,0,0]})"));
        reply = service->transformCommand(json("{\"operation\":\"scale\",\"target\":\"" + small + "\",\"center\":\"" + centre["id"].toString() + "\",\"factor\":2}"));
        const auto* s = service->document().find(small);
        check((bool) reply["ok"] && closeTo(s->width, 20, 1e-9) && closeTo(s->x, 20, 1e-9), "Scale doubles a box's size and its distance from the centre point", reply["error"].toString());
        const auto count = service->document().features().size();
        reply = service->transformCommand(json("{\"operation\":\"scale\",\"target\":\"" + small + "\",\"center\":\"" + centre["id"].toString() + "\",\"factor\":0.5,\"copy\":true}"));
        check((bool) reply["ok"] && service->document().features().size() == count + 1 && closeTo(service->document().find(small)->width, 20, 1e-9), "Scale with Create copy leaves the original size alone", reply["error"].toString());
    }
    check(!(bool) service->transformCommand(json("{\"operation\":\"scale\",\"target\":\"" + stator + "\",\"center\":\"" + pointId + "\",\"factor\":2}"))["ok"], "Scaling an occurrence is refused (it would change the shared definition)");
    check(!(bool) service->transformCommand(json("{\"operation\":\"mirror\",\"target\":\"" + stator + "\",\"plane\":\"" + mountId + "\"}"))["ok"], "Mirroring an occurrence is refused (it would become an independent part)");
    check(!(bool) service->transformCommand(json("{\"operation\":\"rotate\",\"target\":\"" + bracket + "\",\"axis\":\"" + pointId + "\",\"angleDegrees\":10}"))["ok"], "Rotating about a point reference is refused (a point has no direction)");
    check(!(bool) service->transformCommand(json("{\"operation\":\"move\",\"target\":\"" + bracket + "\",\"displacement\":[1,2],\"bogus\":1}"))["ok"], "Malformed transforms are refused without changing anything");
    {
        const auto& m = service->document().mechanicsData(); juce::String rotorId; for (const auto& i : m.instances) if (i.id != stator && !i.grounded && m.instanceHasJoints(i.id)) rotorId = i.id;
        const auto r = service->transformCommand(json("{\"operation\":\"move\",\"target\":\"" + rotorId + "\",\"displacement\":[1,0,0]}"));
        check(rotorId.isNotEmpty() && !(bool) r["ok"] && r["error"].toString().contains("joints"), "A jointed rotor is not moved directly; the error says its joints position it", r["error"].toString());
    }

    // Reference-to-reference associations and cycles.
    {
        auto a = service->referenceCommand(request("create", {}, R"({"kind":"point","name":"Base point","origin":[10,0,0]})"));
        auto c = service->referenceCommand(request("create", {}, "{\"kind\":\"point\",\"name\":\"Offset point\",\"origin\":[15,0,0],\"association\":{\"type\":\"reference\",\"source\":\"" + a["id"].toString() + "\"}}"));
        check((bool) c["ok"] && closeTo(c["reference"]["relative"]["origin"][0], 5, 1e-9), "An offset reference stores its offset from the source (5 mm)", c["error"].toString());
        service->referenceCommand(request("update", a["id"].toString(), R"({"origin":[40,0,0]})"));
        check(closeTo(world(*service, c["id"].toString()).world.x, 45, 1e-9), "Editing the source moves the offset reference with it (45 mm)");
        auto cycle = service->referenceCommand(request("update", a["id"].toString(), "{\"association\":{\"type\":\"reference\",\"source\":\"" + c["id"].toString() + "\"}}"));
        check(!(bool) cycle["ok"] && cycle["error"].toString().containsIgnoreCase("circular"), "A circular dependency is refused", cycle["error"].toString());
        auto self = service->referenceCommand(request("update", a["id"].toString(), "{\"association\":{\"type\":\"reference\",\"source\":\"" + a["id"].toString() + "\"}}"));
        check(!(bool) self["ok"], "A reference cannot depend on itself");
        service->referenceCommand(request("delete", a["id"].toString(), {}));
        const auto broken = world(*service, c["id"].toString());
        check(!broken.ok && broken.problem.contains("no longer exists"), "Deleting a source reference leaves the dependent broken with a clear reason", broken.problem);
        check(service->referenceCommand(request("get", c["id"].toString(), {}))["reference"]["status"].toString() == "broken", "The broken status is reported through the command");
        service->undo(error); check(world(*service, c["id"].toString()).ok, "Undo of the deletion repairs the association");
    }

    // 9. Undo/redo of reference creation.
    {
        const auto count = service->document().constructionData().size();
        auto r = service->referenceCommand(request("create", {}, R"({"kind":"point","origin":[1,2,3]})"));
        check(service->document().constructionData().size() == count + 1, "Reference created");
        service->undo(error); check(service->document().constructionData().size() == count, "Undo removes the reference");
        service->redo(error); check(service->document().constructionData().size() == count + 1 && service->document().findConstruction(r["id"].toString()) != nullptr, "Redo restores it with the same id");
    }

    // 10. Delete the referenced geometry: associations report broken, nothing is silently re-attached.
    {
        juce::String doomed; service->createCylinder(5, 20, doomed, error);
        auto r1 = service->referenceCommand(request("create", {}, "{\"kind\":\"cs\",\"name\":\"Doomed frame\",\"association\":{\"type\":\"frame\",\"source\":\"" + doomed + "\"}}"));
        std::vector<CadFaceInfo> f2; service->listFaces(doomed, f2, error); juce::String side; for (const auto& f : f2) if (!f.planar && f.token.isNotEmpty()) side = f.token;
        auto r2 = service->referenceCommand(request("create", {}, "{\"kind\":\"axis\",\"name\":\"Doomed axis\",\"association\":{\"type\":\"cylinderAxis\",\"source\":\"" + doomed + "\",\"face\":\"" + side + "\"}}"));
        check((bool) r1["ok"] && (bool) r2["ok"], "References created on a cylinder body", r1["error"].toString() + r2["error"].toString());
        const bool removedBody = service->remove(doomed, error);
        const auto a1 = world(*service, r1["id"].toString()), a2 = world(*service, r2["id"].toString());
        check(removedBody && !a1.ok && !a2.ok && a1.problem.contains("no longer exists") && a2.problem.contains("no longer exists"), "Deleting the body breaks both associations with an explanation", a1.problem + " | " + a2.problem);
        check(service->referenceCommand(request("list", {}, {}))["references"].size() == (int) service->document().constructionData().size(), "Broken references stay listed so they can be repaired or deleted");
        auto repair = service->referenceCommand(request("update", r1["id"].toString(), R"({"association":{"type":"fixed"},"origin":[0,0,0]})"));
        check((bool) repair["ok"] && world(*service, r1["id"].toString()).ok, "A broken reference can be repaired by making it fixed", repair["error"].toString());
        service->undo(error); service->undo(error); service->undo(error);
    }

    // 11. Sketch mode: cursor on the sketch plane, snapping to a reference, references from the cursor.
    {
        auto sketchService = std::make_shared<CadCommandService>(); juce::String sketch, line;
        sketchService->createSketch("XZ", 7, sketch, error); sketchService->addSketchLine(sketch, 0, 0, 20, 10, line, error);
        CadWorkspacePanel sketchPanel(sketchService); sketchPanel.setSize(1500, 950); sketchPanel.refreshFromDocument(true);
        sketchService->referenceCommand(request("create", {}, R"({"kind":"point","name":"Sketch datum","origin":[12,7,6]})"));
        sketchPanel.enterSketch(sketch);
        CadSketchFrame frame; sketchService->document().sketchFrame(sketch, frame);
        sketchPanel.executeUiCommand("cursorPlace");
        check(sketchPanel.sketchCanvas->tool() == "cursor", "In a sketch, Place by clicking arms the sketch cursor tool");
        // Find the reference's sketch coordinates and probe the canvas snap that a click there would use.
        const gp_Vec offset(frame.origin, gp_Pnt(12, 7, 6)); const double u = offset.Dot(gp_Vec(frame.u)), v = offset.Dot(gp_Vec(frame.v));
        const auto snap = sketchPanel.sketchCanvas->probeSnap(u + 0.05, v - 0.04);
        check((bool) snap["found"] && snap["source"].toString().startsWith("ref-"), "The sketch snaps to a reference point lying on the sketch plane", juce::JSON::toString(snap));
        sketchPanel.placeCursorFromSketch((double) snap["x"], (double) snap["y"], snap["type"].toString() + "|" + snap["source"].toString() + "/" + snap["entity"].toString());
        check(closeTo(sketchPanel.cursorWorld[0], 12, 1e-6) && closeTo(sketchPanel.cursorWorld[1], 7, 1e-6) && closeTo(sketchPanel.cursorWorld[2], 6, 1e-6) && sketchPanel.sketchCanvas->tool() == "select", "Placing the cursor in the sketch puts it on the reference, on the sketch plane", juce::String(sketchPanel.cursorWorld[0]) + "," + juce::String(sketchPanel.cursorWorld[1]) + "," + juce::String(sketchPanel.cursorWorld[2]));
        sketchPanel.placeCursorFromSketch(3.0, 4.0, {});
        const gp_Pnt expected = frame.origin.Translated(gp_Vec(frame.u) * 3.0 + gp_Vec(frame.v) * 4.0);
        check(closeTo(sketchPanel.cursorWorld[0], expected.X(), 1e-9) && closeTo(sketchPanel.cursorWorld[1], expected.Y(), 1e-9) && closeTo(sketchPanel.cursorWorld[2], expected.Z(), 1e-9) && sketchPanel.editingSketchId == sketch, "A sketch cursor click maps U/V through the sketch's own coordinate system", juce::JSON::toString(sketchPanel.cursorState()));
        auto st = sketchPanel.automation(json(R"({"action":"cursor","operation":"set","u":2,"v":5})"));
        check(closeTo(st["state"]["cursor"]["sketch"]["u"], 2, 1e-9) && closeTo(st["state"]["cursor"]["sketch"]["v"], 5, 1e-9) && closeTo(st["state"]["cursor"]["sketch"]["w"], 0, 1e-9), "Typed sketch coordinates land on the sketch plane (W = 0)");
        sketchPanel.executeUiCommand("cursor");
        check(sketchPanel.editingSketchId == sketch, "Opening the cursor panel does not leave the sketch");
        sketchPanel.automation(json(R"({"action":"set","field":"cursorRefKind","value":"Axis"})"));
        sketchPanel.automation(json(R"({"action":"click","button":"cursorRefCreate"})"));
        const auto& all = sketchService->document().constructionData(); const auto& last = all.back();
        const auto axisWorld = sketchService->document().resolveConstruction(last.id); const auto direction = gp_Dir(0, 0, 1).Transformed(axisWorld.world.toTrsf());
        check(last.kind == "axis" && closeTo(axisWorld.world.x, sketchPanel.cursorWorld[0], 1e-9) && closeTo(std::abs(direction.Dot(frame.u.Crossed(frame.v))), 1.0, 1e-9), "An axis made from the sketch cursor points along the sketch normal", juce::String(direction.X()) + "," + juce::String(direction.Y()) + "," + juce::String(direction.Z()));
        sketchPanel.setCursor({ 4, 7, 5 }, "test");
        sketchPanel.finishSketch();
        auto saved = folder.getChildFile("sketch-refs.djo"); CadCommandService back;
        check(sketchService->save(saved, error) && back.load(saved, error) && back.document().constructionData().size() == sketchService->document().constructionData().size(), "References made in sketch mode are saved with the document", error);
    }

    // Create and edit through the reference form (the Create menu command).
    {
        panel.executeUiCommand("referenceNew");
        check(panel.task == CadWorkspacePanel::Task::reference && panel.editingReferenceId.isEmpty(), "Create > Construction Reference opens the new-reference form");
        panel.automation(json(R"({"action":"set","field":"refName","value":"Form axis"})")); panel.automation(json(R"({"action":"set","field":"refKind","value":"Axis"})"));
        for (const auto& [field, value] : std::vector<std::pair<const char*, double>> { { "refOx", 10 }, { "refOy", 20 }, { "refOz", 30 }, { "refDx", 0 }, { "refDy", 2 }, { "refDz", 0 } })
        { auto a = juce::var(new juce::DynamicObject()); a.getDynamicObject()->setProperty("action", "set"); a.getDynamicObject()->setProperty("field", field); a.getDynamicObject()->setProperty("value", value); panel.automation(a); }
        panel.automation(json(R"({"action":"click","button":"apply"})"));
        juce::String formAxis; for (const auto& r : service->document().constructionData()) if (r.name == "Form axis") formAxis = r.id;
        const auto made = world(*service, formAxis); const auto madeDirection = gp_Dir(0, 0, 1).Transformed(made.world.toTrsf());
        check(formAxis.isNotEmpty() && made.ok && closeTo(made.world.x, 10, 1e-9) && closeTo(made.world.y, 20, 1e-9) && closeTo(made.world.z, 30, 1e-9) && closeTo(madeDirection.Y(), 1.0, 1e-9), "The form creates an axis from typed origin and direction (direction normalised)", formAxis);
        panel.selectFeature(formAxis);
        check(panel.task == CadWorkspacePanel::Task::reference && panel.editingReferenceId == formAxis, "Selecting a reference opens it for editing");
        panel.automation(json(R"({"action":"set","field":"refName","value":"Renamed axis"})")); panel.automation(json(R"({"action":"set","field":"refOz","value":35})"));
        panel.automation(json(R"({"action":"click","button":"apply"})"));
        check(service->document().findConstruction(formAxis)->name == "Renamed axis" && closeTo(world(*service, formAxis).world.z, 35, 1e-9), "Editing through the form renames and moves the reference");
        service->undo(error); check(closeTo(world(*service, formAxis).world.z, 30, 1e-9), "Undo reverts the edit");
        service->redo(error);
        panel.executeUiCommand("referenceNew");
        panel.automation(json(R"({"action":"set","field":"refName","value":"Form follower"})")); panel.automation(json(R"({"action":"set","field":"refKind","value":"Coordinate system"})"));
        panel.automation(json(R"({"action":"set","field":"refAssoc","value":"Follow a body or occurrence"})"));
        auto pickSource = juce::var(new juce::DynamicObject()); pickSource.getDynamicObject()->setProperty("action", "set"); pickSource.getDynamicObject()->setProperty("field", "refSource"); pickSource.getDynamicObject()->setProperty("value", "Occurrence: " + service->document().mechanicsData().findInstance(stator)->name);
        const auto sourceResult = panel.automation(pickSource);
        panel.automation(json(R"({"action":"click","button":"apply"})"));
        juce::String follower; for (const auto& r : service->document().constructionData()) if (r.name == "Form follower") follower = r.id;
        const auto* followerRef = follower.isNotEmpty() ? service->document().findConstruction(follower) : nullptr;
        check(followerRef != nullptr && followerRef->association == "frame" && followerRef->source == stator, "The form creates a reference that follows an occurrence", juce::JSON::toString(sourceResult["error"]));
        const double followerBefore = world(*service, follower).world.z;
        service->transformCommand(json("{\"operation\":\"move\",\"target\":\"" + stator + "\",\"displacement\":[0,0,3]}"));
        check(closeTo(world(*service, follower).world.z, followerBefore + 3, 1e-9), "...and it follows when the occurrence moves");
        panel.cancelTask();
    }

    // Viewport reference picking and the tree.
    {
        panel.executeUiCommand("cursor"); panel.cursorShown = true; view.setStandardView("isometric"); view.fitAll(); panel.updateConstructionDisplay(true);
        const auto resolved = world(*service, pointId); juce::Point<float> screen;
        check(view.worldToScreen({ (float) resolved.world.x, (float) resolved.world.y, (float) resolved.world.z }, screen), "A reference is visible in the view");
        click(screen);
        check(panel.selectedId == pointId && panel.task == CadWorkspacePanel::Task::reference && panel.editingReferenceId == pointId, "Clicking a reference in the view selects it for editing", panel.selectedId);
        check(panel.sketchCanvas != nullptr && panel.isReference(pointId), "References are first-class ids in the panel");
        panel.toggleVisibility(pointId);
        check(!service->document().findConstruction(pointId)->visible, "The tree eye hides a reference");
        panel.toggleVisibility(pointId); check(service->document().findConstruction(pointId)->visible, "...and shows it again");
        service->undo(error); check(!service->document().findConstruction(pointId)->visible, "Visibility is part of the undoable document state");
        service->redo(error);
        std::function<bool(juce::TreeViewItem*)> hasReferenceItem = [&](juce::TreeViewItem* item) -> bool
        { if (item == nullptr) return false; if (auto* f = dynamic_cast<CadWorkspacePanel::FeatureItem*>(item); f != nullptr && f->featureId() == pointId && f->hasEye()) return true; for (int i = 0; i < item->getNumSubItems(); ++i) if (hasReferenceItem(item->getSubItem(i))) return true; return false; };
        panel.refreshFromDocument(true);
        check(hasReferenceItem(panel.tree.getRootItem()), "The model tree lists the reference under Construction references with an eye control");
        // Transform tool: reference slots are filled by clicking references.
        panel.executeUiCommand("referenceTransform");
        check(panel.task == CadWorkspacePanel::Task::referenceTransform && panel.form != nullptr, "The Transform with References task opens");
        panel.automation(json(R"({"action":"set","field":"trOp","value":"Move to a reference"})"));
        panel.pickReferenceForTransform(pointId);
        auto* slot = dynamic_cast<juce::ComboBox*>(panel.form->control("trRef1"));
        check(slot != nullptr && slot->getSelectedItemIndex() > 0 && panel.referenceChoiceIds[slot->getSelectedItemIndex()] == pointId, "Picking a reference fills the transform's reference slot");
        panel.featureClicked(partBox);
        auto* targetBox = dynamic_cast<juce::ComboBox*>(panel.form->control("trTarget"));
        check(targetBox != nullptr && targetBox->getSelectedItemIndex() > 0 && panel.targetChoiceIds[targetBox->getSelectedItemIndex()] == partBox && panel.task == CadWorkspacePanel::Task::referenceTransform, "Clicking a part in the tree sets the transform target without leaving the tool");
        auto result = panel.automation(json(R"({"action":"click","button":"apply"})"));
        const auto* moved = service->document().find(partBox);
        check((bool) result["ok"] && closeTo(moved->x, 200, 1e-6) && closeTo(moved->y, 100, 1e-6) && closeTo(moved->z, 50, 1e-6), "Apply runs the reference transform through the task panel (the part's origin lands on the target point)", juce::JSON::toString(result["error"]) + " " + juce::String(moved->x) + "," + juce::String(moved->y) + "," + juce::String(moved->z));
        panel.cancelTask();
    }

    // Agent, FRust and node access.
    {
        const auto catalogue = juce::JSON::toString(DjehutiAgentPanel::toolCatalogue());
        check(catalogue.contains("\"cad_reference\"") && catalogue.contains("\"cad_transform\""), "The agent and local API catalogue lists cad_reference and cad_transform");
        auto host = std::make_shared<CadCommandService>(); frust_engine::setCadCommandService(host);
        const auto created = juce::JSON::parse(djehuti_cad_reference(R"({"action":"create","parameters":{"kind":"point","name":"From FRust","origin":[1,2,3]}})"));
        const auto listed = juce::JSON::parse(djehuti_cad_reference(R"({"action":"list"})"));
        check((bool) created["ok"] && listed["references"].size() == 1 && host->document().constructionData().size() == 1, "FRust host functions run the shared reference command");
        juce::String hostBox; host->createBox(10, 10, 10, hostBox, error);
        const auto moved = juce::JSON::parse(djehuti_cad_transform(("{\"operation\":\"move\",\"target\":\"" + hostBox + "\",\"displacement\":[5,0,0]}").toRawUTF8()));
        check((bool) moved["ok"] && closeTo(host->document().find(hostBox)->x, 5, 1e-9), "FRust host functions run the shared transform command");
        frust_engine::setCadCommandService({});
        for (const auto* type : { "cad_reference", "cad_transform" })
        {
            const auto graph = std::string("{\"functionName\":\"construct\",\"params\":[{\"name\":\"args\",\"type\":\"String\"}],\"output\":\"r\",\"nodes\":[{\"id\":\"r\",\"type\":\"") + type + "\",\"inputs\":[{\"param\":\"args\"}]}]}";
            const auto node = node_compiler::CompileGraphToSource(graph);
            check(node.ok && node.source.find(std::string("extern fn djehuti_") + type) != std::string::npos, juce::String(type) + " node compiles to its FRust host command", node.errorMessage);
        }
        const auto& allowed = frust_engine::cadHostFunctions();
        check(std::find(allowed.begin(), allowed.end(), "djehuti_cad_reference") != allowed.end() && std::find(allowed.begin(), allowed.end(), "djehuti_cad_transform") != allowed.end() && std::find(allowed.begin(), allowed.end(), "djehuti_cad_gear") != allowed.end(), "FRust programs may call the reference, transform and gear host functions");
    }
    // Final reopen of the whole mechanical session: associations still resolve.
    {
        const auto file = folder.getChildFile("session.djo"); CadCommandService back;
        const bool ok = service->save(file, error) && back.load(file, error);
        const auto a = back.document().resolveConstruction(axisId), m = back.document().resolveConstruction(mountId);
        check(ok && a.ok && m.ok && closeTo(a.world.z, world(*service, axisId).world.z, 1e-9) && closeTo(m.world.z, world(*service, mountId).world.z, 1e-9) && closeTo(m.world.z, 18, 1e-9), "After reopening, the cylinder-axis and occurrence-frame references resolve to the same places", error);
    }
    {
        const auto image = panel.createComponentSnapshot(panel.getLocalBounds());
        if (auto stream = folder.getChildFile("construction-view.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); juce::PNGImageFormat().writeImageToStream(image, *stream); }
    }
    auto* r = new juce::DynamicObject(); r->setProperty("checks", checks); r->setProperty("total", checks.size()); r->setProperty("passed", checks.size() - failures); r->setProperty("failed", failures); r->setProperty("ok", failures == 0);
    report = r; return failures == 0;
}
