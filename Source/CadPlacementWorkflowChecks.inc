// Selection, Move / Rotate manipulator, camera navigation and component placement (--cad-placement-workflow-check).
// Production panel and viewport mouse handlers with synthetic JUCE events on the real handle positions.
bool runPlacementWorkflowCheck(juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks; int failures = 0; juce::String error;
    const auto progress = juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-placement-workflow-check.progress.log"); progress.replaceWithText({});
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile("build/placement-workflow-check"); folder.createDirectory();
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto check = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto* v = new juce::DynamicObject(); v->setProperty("name", name); v->setProperty("ok", ok); if (!ok) { v->setProperty("detail", detail); ++failures; } checks.add(v);
        progress.appendText(juce::String((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1) + " s " + (ok ? "PASS " : "FAIL ") + name + (ok ? juce::String() : " :: " + detail) + "\n");
    };
    const auto closeTo = [](double a, double b, double t) { return std::isfinite(a) && std::abs(a - b) <= t; };
    const auto centre = [](const TopoDS_Shape& s) { Bnd_Box b; BRepBndLib::Add(s, b, true); double x0, y0, z0, x1, y1, z1; b.Get(x0, y0, z0, x1, y1, z1); return gp_Pnt((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2); };
    const auto volumeOf = [](const TopoDS_Shape& s) { GProp_GProps p; BRepGProp::VolumeProperties(s, p); return p.Mass(); };
    auto service = std::make_shared<CadCommandService>();
    CadWorkspacePanel panel(service); panel.setSize(1500, 950);
    auto selection = std::make_shared<CadSelection>(); panel.setSelection(selection);
    auto& view = panel.modelView;
    const auto mouse = [&](juce::Point<float> p, int mods, juce::Point<float> down)
    { return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(mods), 1, 0, 0, 0, 0, &view, &view, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, false); };
    const auto drag = [&](juce::Point<float> from, juce::Point<float> to, int extraMods)
    {
        const int left = juce::ModifierKeys::leftButtonModifier;
        view.mouseDown(mouse(from, left | extraMods, from));
        for (int i = 1; i <= 6; ++i) view.mouseDrag(mouse(from + (to - from) * (i / 6.0f), left | extraMods, from));
        view.mouseUp(mouse(to, extraMods, from));
    };

    // 1. Selection and the manipulator on a body.
    juce::String box; check(service->createBox(10, 20, 30, box, error), "Create a body", error);
    panel.refreshFromDocument(true); view.fitAll(); panel.featureClicked(box);
    check(panel.selectedId == box, "Selecting a body in the tree selects it");
    panel.executeUiCommand("transform"); panel.updateManipulator(true);
    auto state = view.manipulatorState();
    check((bool) state["active"] && state["target"].toString() == box && closeTo((double) state["pivot"][0], 5, 1e-6) && closeTo((double) state["pivot"][1], 10, 1e-6) && closeTo((double) state["pivot"][2], 15, 1e-6),
          "Move / Rotate shows the manipulator at the body's centre", juce::JSON::toString(state, true));
    const auto shapeVolume = volumeOf(service->document().find(box)->shape);

    // 2-4. Drag along X, undo/redo; rotation about Z with 15 degree snapping keeps the pivot.
    auto from = view.manipulatorHandle(0, false, 0.85f), tip = view.manipulatorHandle(0, false, 1.0f);
    juce::Point<float> c; view.toScreen(view.manipulator().pivot, c);
    const auto screenDir = (tip - c) / (tip - c).getDistanceFromOrigin();
    const auto revision = service->document().revision();
    drag(from, from + screenDir * 120.0f, 0);
    auto* f = service->document().find(box);
    check(f->x > 1.0 && closeTo(f->y, 0, 1e-9) && closeTo(f->z, 0, 1e-9) && service->document().revision() != revision, "Dragging the X arrow moves the body along X only", juce::String(f->x, 6));
    const double movedX = f->x;
    check(closeTo(volumeOf(f->shape), shapeVolume, 1e-6) && closeTo(f->width, 10, 0) && closeTo(f->depth, 20, 0), "Moving changes placement, not the modeled geometry");
    check(service->undo(error) && closeTo(service->document().find(box)->x, 0, 1e-12), "Undo returns the body to its previous placement");
    check(service->redo(error) && closeTo(service->document().find(box)->x, movedX, 1e-9), "Redo reapplies the move");
    panel.refreshFromDocument(true); panel.updateManipulator(true);
    const auto before = centre(service->document().find(box)->shape);
    drag(view.manipulatorHandle(2, true, 0.06f), view.manipulatorHandle(2, true, 0.19f), juce::ModifierKeys::ctrlModifier);   // start beside the Y arrow
    f = service->document().find(box);
    const auto after = centre(f->shape);
    check(closeTo(f->rz, 45, 1e-6) && closeTo(f->rx, 0, 1e-6) && closeTo(f->ry, 0, 1e-6), "Dragging the Z ring with Ctrl rotates in 15 degree steps (45 degrees)", juce::String(f->rx) + "," + juce::String(f->ry) + "," + juce::String(f->rz));
    check(before.Distance(after) < 1e-6, "Rotation turns the body about its centre (the shown pivot)", juce::String(before.Distance(after), 9));
    panel.updateManipulator(true);
    const auto revisionBeforeCancel = service->document().revision();
    from = view.manipulatorHandle(1, false, 0.9f);
    view.mouseDown(mouse(from, juce::ModifierKeys::leftButtonModifier, from)); view.mouseDrag(mouse(from + juce::Point<float>(40, -40), juce::ModifierKeys::leftButtonModifier, from));
    check(view.isManipulating() && (bool) view.manipulatorState()["previewing"], "Dragging previews the move in the view before it is committed");
    view.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    view.mouseUp(mouse(from + juce::Point<float>(40, -40), 0, from));
    check(service->document().revision() == revisionBeforeCancel && !(bool) view.manipulatorState()["previewing"], "Esc cancels a drag without changing the document");
    panel.executeUiCommand("transformLocal"); panel.updateManipulator(true);
    const auto local = view.manipulator();
    check(local.local && closeTo(local.axes[0][0], std::cos(juce::MathConstants<double>::pi / 4), 1e-6) && closeTo(local.axes[0][1], std::sin(juce::MathConstants<double>::pi / 4), 1e-6), "Local axes follow the body's orientation");
    panel.executeUiCommand("transformLocal");

    // 5. Numeric placement entry in the inspector (position and rotation).
    panel.selectFeature(box); panel.startTask(CadWorkspacePanel::Task::body);
    auto set = [&](const char* field, double value) { auto* a = new juce::DynamicObject(); a->setProperty("action", "set"); a->setProperty("field", field); a->setProperty("value", value); panel.automation(a); };
    set("x", 25.0); set("rx", 30.0);
    panel.automation(juce::JSON::parse(R"({"action":"click","button":"apply"})"));
    f = service->document().find(box);
    check(closeTo(f->x, 25, 1e-9) && closeTo(f->rx, 30, 1e-9) && closeTo(f->rz, 45, 1e-9), "Numeric position and rotation entry apply to the selected body", juce::String(f->x) + " " + juce::String(f->rx));

    // 6. Camera: orbit about the point under the cursor and about the screen centre; pan; fit; standard views.
    panel.refreshFromDocument(true); view.fitAll();
    juce::Point<float> body; view.toScreen({ (float) centre(f->shape).X(), (float) centre(f->shape).Y(), (float) centre(f->shape).Z() }, body);
    const auto eyeBefore = view.eyePosition();
    view.anchorOrbit(body);
    const auto eyeAfter = view.eyePosition();
    const auto pickedPivot = std::array<float, 3> { view.targetX, view.targetY, view.targetZ };
    check(std::abs(eyeAfter[0] - eyeBefore[0]) + std::abs(eyeAfter[1] - eyeBefore[1]) + std::abs(eyeAfter[2] - eyeBefore[2]) < 1e-3f, "Choosing the orbit pivot does not move the camera");
    check(std::hypot(pickedPivot[0] - centre(f->shape).X(), pickedPivot[1] - centre(f->shape).Y(), pickedPivot[2] - centre(f->shape).Z()) < 40.0, "Orbit pivots about the body under the cursor, not the world origin",
          juce::String(pickedPivot[0]) + "," + juce::String(pickedPivot[1]) + "," + juce::String(pickedPivot[2]));
    const auto orbitFrom = body; drag(orbitFrom, orbitFrom + juce::Point<float>(80, 30), 0);
    juce::Point<float> stays; view.toScreen(pickedPivot, stays);
    check(stays.getDistanceFrom(orbitFrom) < 2.0f, "Orbit drag keeps the picked point fixed on screen", juce::String(stays.getDistanceFrom(orbitFrom), 3) + " px");
    view.panX += 40; view.panY -= 25; view.anchorOrbit({ 3, 3 });
    juce::Point<float> middle; view.toScreen({ view.targetX, view.targetY, view.targetZ }, middle);
    check(middle.getDistanceFrom({ view.getWidth() * 0.5f, view.getHeight() * 0.5f }) < 1.0f, "After panning, orbit over empty space pivots about the screen centre", juce::String(middle.x) + "," + juce::String(middle.y));
    panel.featureClicked(box); check(view.fitSelection(), "Fit to selection frames the selected body");
    check(std::hypot(view.targetX - centre(f->shape).X(), view.targetY - centre(f->shape).Y(), view.targetZ - centre(f->shape).Z()) < 1.0, "Fit to selection orbits about the selection");
    const auto geometryRevision = service->document().revision();
    for (const auto* name : { "front", "back", "left", "right", "top", "bottom", "isometric" }) check(view.setStandardView(name), juce::String("Standard view: ") + name);
    check(view.setStandardView("top") && view.pitch > 1.56f && view.setStandardView("front") && closeTo(view.yaw, 0, 1e-6) && closeTo(view.pitch, 0, 1e-6), "Top and front views face the model squarely");
    juce::String cameraError; check(!view.controlCamera(juce::JSON::parse(R"({"operation":"standardView","view":"sideways"})"), cameraError), "Unknown standard views are refused", cameraError);
    check(service->document().revision() == geometryRevision, "Camera changes never change the document");

    // 7-9. Components: insert, two independent occurrences of one definition, move, save and reopen.
    const auto record = juce::JSON::parse(R"({"id":"check_motor_can","name":"Check motor can","category":"Motion/Motors","origin":"custom","revision":1,"properties":[],"geometry":{"kind":"cylinder","diameterMm":24,"lengthMm":30}})");
    juce::String first, second;
    check(service->document().insertComponent(record, {}, first, error) && service->document().insertComponent(record, {}, second, error), "Insert two occurrences of one component", error);
    const auto* i1 = service->document().mechanicsData().findInstance(first); const auto* i2 = service->document().mechanicsData().findInstance(second);
    check(i1 && i2 && i1->featureId == i2->featureId && i1->id != i2->id, "Both occurrences share one definition and keep their own identity");
    view.setStandardView("isometric"); view.fitAll();   // in Front view the Y arrow points at the viewer
    panel.focusInsertedComponent(second);
    check(panel.selectedId == second && panel.transformTool && view.manipulator().active && view.manipulator().drawId == second, "An inserted component is selected with Move / Rotate ready");
    panel.motionRuntime = std::make_shared<CadMotionRuntime>(service); panel.motionRuntime->synchronize(); panel.updateManipulator(true);
    from = view.manipulatorHandle(1, false, 0.85f); tip = view.manipulatorHandle(1, false, 1.0f); view.toScreen(view.manipulator().pivot, c);
    drag(from, from + (tip - c) / (tip - c).getDistanceFromOrigin() * 100.0f, 0);
    i1 = service->document().mechanicsData().findInstance(first); i2 = service->document().mechanicsData().findInstance(second);
    check(i2->placement.y > 1.0 && closeTo(i1->placement.y, 0, 1e-12) && closeTo(i1->placement.x, 0, 1e-12), "Moving one occurrence leaves the other where it was", juce::String(i2->placement.y));
    panel.updateManipulator(true);
    drag(view.manipulatorHandle(0, true, 0.06f), view.manipulatorHandle(0, true, 0.32f), juce::ModifierKeys::ctrlModifier);
    i2 = service->document().mechanicsData().findInstance(second);
    double rx = 0, ry = 0, rz = 0; mech::eulerDegreesFromFrame(i2->placement, rx, ry, rz);
    check(closeTo(rx, 90, 1e-4), "Rotating an occurrence changes its placement only (90 degrees about X)", juce::String(rx) + "," + juce::String(ry) + "," + juce::String(rz));
    const auto placementA = juce::JSON::toString(mech::frameToJson(i1->placement, 1.0)), placementB = juce::JSON::toString(mech::frameToJson(i2->placement, 1.0));
    for (const auto* ext : { "djo", "djc" })
    {
        CadCommandService loaded; const auto file = folder.getChildFile(juce::String("placement.") + ext);
        const bool ok = service->save(file, error) && loaded.load(file, error);
        const auto* a = loaded.document().mechanicsData().findInstance(first); const auto* b = loaded.document().mechanicsData().findInstance(second);
        check(ok && a && b && a->featureId == b->featureId && loaded.document().componentFor(a->featureId) != nullptr
              && juce::JSON::toString(mech::frameToJson(a->placement, 1.0)) == placementA && juce::JSON::toString(mech::frameToJson(b->placement, 1.0)) == placementB,
              juce::String("Save and reopen (.") + ext + ") keeps both occurrences, their definition and placements", error);
    }
    // A jointed, ungrounded occurrence is placed by its joint and says so instead of moving.
    CadJointRequest q; q.type = "revolute"; q.childInstance = first; juce::String joint; check(service->createJoint(q, joint, error), "Add a revolute joint to one occurrence", error);
    juce::String reason; check(!panel.manipulable(first, reason) && reason.contains("joints"), "A jointed occurrence explains how to move it", reason);
    check(service->setInstanceGrounded(first, true, error) && panel.manipulable(first, reason), "Grounding it makes it movable again");

    // 10. Multi-part rotary component: stationary housing and rotating member on one shared axis.
    {
        auto rig = std::make_shared<CadCommandService>();
        const auto twoPart = juce::JSON::parse(R"({"id":"check_rotary_pair","name":"Check rotary pair","category":"Check/Rotary","origin":"custom","revision":1,"properties":[],
            "geometry":{"kind":"assembly"},
            "assembly":{"parts":[{"role":"stator","name":"Stator","geometry":{"kind":"revolved","profileMm":[[3,0],[10,0],[10,15],[3,15]]}},
                                 {"role":"rotor","name":"Rotor","geometry":{"kind":"revolved","profileMm":[[0,-5],[2.5,-5],[2.5,18],[0,18]]}}],
                        "joint":{"type":"revolute","parent":"stator","child":"rotor","origin":[0,0,0],"direction":[0,0,1],"name":"rotation"}}})");
        juce::String stator; const bool inserted = rig->document().insertComponent(twoPart, {}, stator, error);
        const auto& m = rig->document().mechanicsData();
        check(inserted && m.instances.size() == 2 && m.joints.size() == 1 && m.joints[0].type == "revolute" && m.findInstance(stator) != nullptr && m.findInstance(stator)->grounded,
              "A two-part component inserts a grounded stator and a rotor on a revolute joint in one step", error);
        juce::String rotor; for (const auto& i : m.instances) if (i.id != stator) rotor = i.id;
        check(rig->document().componentFor(m.findInstance(stator)->featureId) != nullptr && m.findInstance(stator)->featureId != m.findInstance(rotor)->featureId, "Stator and rotor keep separate definitions");
        CadWorkspacePanel rigPanel(rig); rigPanel.setSize(1500, 950); rigPanel.refreshFromDocument(true);
        juce::String why; check(rigPanel.manipulable(stator, why) && !rigPanel.manipulable(rotor, why), "The stator moves with Move / Rotate; the rotor follows its joint", why);
        rigPanel.featureClicked(stator); rigPanel.executeUiCommand("transform"); rigPanel.updateManipulator(true);
        Viewport3D::Manipulation lift; lift.axis = 0; lift.amount = 40.0;
        check(rigPanel.applyManipulation(lift), "Move the stator 40 mm along X");
        kin::SolveRequest solveRequest; solveRequest.prescribed[m.joints[0].id] = 0.5;
        const auto solved = kin::solve(m, solveRequest);
        const auto rotorWorld = solved.world.count(rotor) ? solved.world.at(rotor) : gp_Trsf();
        check(solved.valid && closeTo(rotorWorld.TranslationPart().X(), 40.0, 1e-6) && closeTo(rotorWorld.GetRotation().GetRotationAngle(), 0.5, 1e-6), "The rotor follows the stator and turns about the shared axis",
              juce::String(rotorWorld.TranslationPart().X()) + " " + juce::String(rotorWorld.GetRotation().GetRotationAngle()));
        for (const auto* ext : { "djo", "djc" })
        {
            CadCommandService loaded; const auto file = folder.getChildFile(juce::String("rotary.") + ext);
            const bool ok = rig->save(file, error) && loaded.load(file, error);
            check(ok && loaded.document().mechanicsData().joints.size() == 1 && loaded.document().mechanicsData().findInstance(stator) && loaded.document().mechanicsData().findInstance(stator)->grounded,
                  juce::String("Save and reopen (.") + ext + ") keeps the two-part component and its joint", error);
        }
        check(rig->undo(error) && rig->undo(error) && rig->document().mechanicsData().instances.empty() && rig->document().features().empty(), "Undo removes the move, then the whole two-part insertion");
    }

    // 11. Existing gear behaviour: moving a paired gear is a placement change the pair check reports; undo restores it.
    {
        auto gears = std::make_shared<CadCommandService>();
        const auto pair = gears->gearPairCommand(juce::JSON::parse(R"({"action":"create","parameters":{"gearA":{"teeth":20},"gearB":{"teeth":40}}})"));
        const auto gearB = pair["gearB"].toString();
        const auto definition = juce::JSON::toString(gears->document().find(gearB)->modeling);
        CadWorkspacePanel gearPanel(gears); gearPanel.setSize(1500, 950); gearPanel.refreshFromDocument(true); gearPanel.modelView.fitAll();
        gearPanel.featureClicked(gearB); gearPanel.executeUiCommand("transform"); gearPanel.updateManipulator(true);
        Viewport3D::Manipulation nudge; nudge.axis = 0; nudge.amount = 1.0;
        check(gearPanel.applyManipulation(nudge) && juce::JSON::toString(gears->document().find(gearB)->modeling) == definition, "Moving a gear changes its placement, not its tooth geometry");
        auto status = gears->gearPairCommand(juce::JSON::parse("{\"action\":\"check\",\"id\":\"" + gearB + "\"}"));
        check(!(bool) status["compatible"], "The pair check reports a moved gear's centre distance");
        gears->undo(error);
        status = gears->gearPairCommand(juce::JSON::parse("{\"action\":\"check\",\"id\":\"" + gearB + "\"}"));
        check((bool) status["compatible"], "Undo restores the meshing placement");
    }
    auto* r = new juce::DynamicObject(); r->setProperty("checks", checks); r->setProperty("total", checks.size()); r->setProperty("passed", checks.size() - failures); r->setProperty("failed", failures); r->setProperty("ok", failures == 0);
    report = r; return failures == 0;
}
