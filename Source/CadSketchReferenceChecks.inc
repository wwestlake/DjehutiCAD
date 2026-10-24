// Sketch reference visibility and performance checks (--cad-sketch-reference-check).
// Production panel, tree items and sketch canvas through synthetic calls; no native pointer automation.
bool runSketchReferenceCheck(juce::var& report)
{
    using namespace djehuti::cad;
    juce::Array<juce::var> checks, measurements; int failures = 0; juce::String error;
    const auto progress = juce::File::getCurrentWorkingDirectory().getChildFile("build/cad-sketch-reference-check.progress.log");
    progress.replaceWithText({});
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile("build/sketch-reference-check"); folder.createDirectory();
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto check = [&](bool ok, const juce::String& name, const juce::String& detail = {})
    {
        auto* v = new juce::DynamicObject(); v->setProperty("name", name); v->setProperty("ok", ok); if (!ok) { v->setProperty("detail", detail); ++failures; } checks.add(v);
        progress.appendText(juce::String((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1) + " s " + (ok ? "PASS " : "FAIL ") + name + (ok ? juce::String() : " :: " + detail) + "\n");
    };
    const auto measure = [&](const juce::String& name, double ms, const juce::var& detail = {})
    {
        auto* v = new juce::DynamicObject(); v->setProperty("name", name); v->setProperty("ms", ms); if (!detail.isVoid()) v->setProperty("detail", detail); measurements.add(v);
        progress.appendText("MEASURE " + name + ": " + juce::String(ms, 1) + " ms " + juce::JSON::toString(detail, true) + "\n");
    };
    const auto now = [] { return juce::Time::getMillisecondCounterHiRes(); };
    const auto pairDocument = [&](const char* mechanism, juce::String& gearA, juce::String& gearB)
    {
        auto service = std::make_shared<CadCommandService>();
        auto* p = new juce::DynamicObject(); p->setProperty("module", 2.0); p->setProperty("faceWidth", 10.0); p->setProperty("mechanism", mechanism);
        auto* a = new juce::DynamicObject(); a->setProperty("teeth", 20); p->setProperty("gearA", a);
        auto* b = new juce::DynamicObject(); b->setProperty("teeth", 40); p->setProperty("gearB", b);
        auto* request = new juce::DynamicObject(); request->setProperty("action", "create"); request->setProperty("parameters", p);
        const auto r = service->gearPairCommand(request);
        gearA = r["gearA"].toString(); gearB = r["gearB"].toString();
        return service;
    };
    const auto cache = [](CadWorkspacePanel& panel) { return panel.sketchCanvas->referenceCacheState(); };
    const auto paint = [](CadWorkspacePanel& panel) { panel.createComponentSnapshot(panel.getLocalBounds()); };
    const auto references = [](const CadCommandService& s, const juce::String& sketch) { const auto* k = s.document().findSketch(sketch); return k != nullptr ? k->references : juce::StringArray(); };
    std::function<CadWorkspacePanel::FeatureItem*(juce::TreeViewItem*, const juce::String&)> findItem = [&](juce::TreeViewItem* item, const juce::String& id) -> CadWorkspacePanel::FeatureItem*
    {
        if (item == nullptr) return nullptr;
        if (auto* f = dynamic_cast<CadWorkspacePanel::FeatureItem*>(item); f != nullptr && f->featureId() == id) return f;
        for (int i = 0; i < item->getNumSubItems(); ++i) if (auto* found = findItem(item->getSubItem(i), id)) return found;
        return nullptr;
    };

    // 1-3. Sketch entry on the reported workflow (20:40 module 2 pair, sketch on XY): nothing shown, nothing prepared.
    for (const auto* mechanism : { "none", "create" })
    for (const double offset : { 0.0, 5.0 })
    {
        juce::String gearA, gearB, sketch;
        auto service = pairDocument(mechanism, gearA, gearB);
        service->createSketch("XY", offset, sketch, error);
        CadWorkspacePanel panel(service); panel.setSize(1500, 950); panel.refreshFromDocument(true);
        const auto label = juce::String("mechanism ") + mechanism + ", XY offset " + juce::String(offset, 0);
        auto t = now(); panel.enterSketch(sketch); paint(panel); const auto entry = now() - t; const auto state = cache(panel);
        measure("Sketch entry and first paint (" + label + ")", entry, state);
        check(references(*service, sketch).isEmpty() && (int) state["solids"] == 0 && (int) state["solidPreparations"] == 0, "New sketch shows and prepares no model geometry (" + label + ")", juce::JSON::toString(state, true));
        check(entry < 3000.0, "Sketch entry with hidden gears takes under 3 s in Debug (" + label + ")", juce::String(entry, 1) + " ms");
        juce::String line; t = now(); service->addSketchLine(sketch, 0, 0, 10, 5, line, error); panel.refreshFromDocument(true); paint(panel);
        measure("Sketch line edit and repaint, references hidden (" + label + ")", now() - t, cache(panel));
        panel.finishSketch();
    }

    // 4-8. Eye controls, independent selective display, context menu, 3D independence, no regeneration.
    {
        juce::String gearA, gearB, sketch, other;
        auto service = pairDocument("none", gearA, gearB);
        service->createSketch("XY", 0, sketch, error); service->createSketch("YZ", 0, other, error);
        juce::String hidden3d; check(service->setVisible(gearB, false, hidden3d), "Gear B hidden in the 3D workspace before sketching");
        const auto shapeA = service->document().find(gearA)->shape, shapeB = service->document().find(gearB)->shape;
        CadWorkspacePanel panel(service); panel.setSize(1500, 950); panel.refreshFromDocument(true);
        auto* itemA = findItem(panel.tree.getRootItem(), gearA);
        check(itemA != nullptr && !itemA->hasEye(), "Outside a sketch, solid tree items keep their normal display (no sketch-reference eye)");
        panel.enterSketch(sketch); paint(panel);
        itemA = findItem(panel.tree.getRootItem(), gearA); auto* itemB = findItem(panel.tree.getRootItem(), gearB);
        auto* itemOther = findItem(panel.tree.getRootItem(), other); auto* itemActive = findItem(panel.tree.getRootItem(), sketch);
        check(itemA && itemB && itemOther && itemA->hasEye() && itemB->hasEye() && itemOther->hasEye(), "While sketching, Gear A, Gear B and another sketch have individual eye controls");
        check(itemActive != nullptr && !panel.sketchReferenceApplicable(sketch), "The open sketch is not offered as its own reference");
        check(!panel.sketchReferenceShown(gearA) && !panel.sketchReferenceShown(gearB) && !panel.sketchReferenceShown(other), "Every eye starts closed in a new sketch");

        auto t = now(); panel.toggleVisibility(gearA); paint(panel); auto state = cache(panel);
        measure("Show Gear A as a reference (first preparation)", now() - t, state);
        check(references(*service, sketch) == juce::StringArray { gearA } && state["solidSources"].toString() == gearA && service->document().find(gearA)->visible, "Showing Gear A displays only Gear A and leaves its 3D visibility unchanged", juce::JSON::toString(state, true));
        t = now(); panel.toggleVisibility(gearB); paint(panel); state = cache(panel);
        measure("Show Gear B as a reference (first preparation)", now() - t, state);
        check(state["solidSources"].toString() == gearA + "," + gearB && !service->document().find(gearB)->visible, "Gear B can be shown in the sketch while it stays hidden in 3D", juce::JSON::toString(state, true));
        panel.toggleVisibility(gearA); paint(panel); state = cache(panel);
        check(state["solidSources"].toString() == gearB, "Hiding Gear A leaves Gear B shown", juce::JSON::toString(state, true));
        const int prepared = (int) state["solidPreparations"];
        t = now();
        for (int i = 0; i < 10; ++i) { panel.toggleVisibility(gearA); paint(panel); panel.toggleVisibility(gearB); paint(panel); }
        state = cache(panel);
        measure("20 show/hide toggles with repaint (cached)", now() - t, state);
        check(state["solidSources"].toString() == gearB && (int) state["solidPreparations"] == prepared, "20 repeated show/hide toggles reuse prepared geometry and do not crash", juce::JSON::toString(state, true));
        check(service->document().find(gearA)->shape.IsSame(shapeA) && service->document().find(gearB)->shape.IsSame(shapeB), "Reference toggles never regenerate the gear solids");
        check(service->document().find(gearA)->visible && !service->document().find(gearB)->visible, "3D visibility is untouched by sketch reference changes");

        check(service->setSketchReferences(sketch, { gearA, gearB }, error), "Both gears shown before the eye and menu checks", error);
        panel.refreshFromDocument(true); paint(panel);
        // The tree eye click is deferred so the tree can be rebuilt safely after the click.
        if (itemB != nullptr)
        {
            itemB = findItem(panel.tree.getRootItem(), gearB);
            const auto width = itemB->getItemPosition(true).getWidth();
            const juce::MouseEvent click(juce::Desktop::getInstance().getMainMouseSource(), { (float) width - 10.0f, 10.0f }, juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0, &panel.tree, &panel.tree, juce::Time::getCurrentTime(), { (float) width - 10.0f, 10.0f }, juce::Time::getCurrentTime(), 1, false);
            panel.automationInProgress = true; itemB->itemClicked(click); panel.automationInProgress = false;
            check(!panel.sketchReferenceShown(gearB) && panel.sketchReferenceShown(gearA), "Clicking Gear B's tree eye hides only Gear B");
            itemB = findItem(panel.tree.getRootItem(), gearB);
            panel.automationInProgress = true; itemB->itemClicked(click); panel.automationInProgress = false;
            check(panel.sketchReferenceShown(gearB), "Clicking the eye again shows Gear B");
        }
        auto menu = panel.sketchReferenceMenu(gearB);
        juce::PopupMenu::MenuItemIterator items(menu); juce::StringArray texts; while (items.next()) texts.add(items.getItem().text + (items.getItem().isEnabled ? "" : " (disabled)"));
        check(texts.contains("Hide in this sketch") && texts.contains("Reference geometry settings..."), "Sketch-mode context menu offers Hide for a shown gear and keeps reference settings", texts.joinIntoString(" | "));
        panel.runSketchReferenceMenu(sketch, gearB, 1); paint(panel);
        check(!panel.sketchReferenceShown(gearB) && panel.sketchReferenceShown(gearA), "Context-menu Hide hides Gear B only");
        panel.runSketchReferenceMenu(sketch, gearB, 1); panel.runSketchReferenceMenu("sketch-stale", gearB, 1); panel.runSketchReferenceMenu(sketch, "gear-999", 1);
        check(panel.sketchReferenceShown(gearB) && references(*service, sketch).size() == 2, "Context-menu Show works; stale sketches and unknown items are ignored safely");
        texts.clear(); auto missing = panel.sketchReferenceMenu(sketch); juce::PopupMenu::MenuItemIterator none(missing); while (none.next()) texts.add(none.getItem().text + (none.getItem().isEnabled ? "" : " (disabled)"));
        check(texts.contains("Not displayable as a sketch reference (disabled)"), "Non-displayable items get a disabled entry instead of a broken command");
        juce::String configureError; auto visibility = juce::var(new juce::DynamicObject()); visibility.getDynamicObject()->setProperty(gearA, false);
        auto prefs = juce::var(new juce::DynamicObject()); prefs.getDynamicObject()->setProperty("visibility", visibility);
        check(panel.configureReferences(prefs, configureError) && !panel.sketchReferenceShown(gearA) && panel.sketchReferenceShown(gearB), "Reference settings toggles write the same per-sketch list", configureError);
        visibility.getDynamicObject()->setProperty(sketch, true);
        check(!panel.configureReferences(prefs, configureError), "Reference settings refuse the open sketch itself", configureError);
        check(service->undo(error) && panel.sketchReferenceShown(gearA), "Reference changes are one undoable document command each");
        check(service->redo(error) && !panel.sketchReferenceShown(gearA), "Redo reapplies the reference change");
        panel.toggleVisibility(other); paint(panel);
        check(panel.sketchShown(other) && service->document().find(other)->visible && cache(panel)["curves"].isInt(), "Another sketch is shown as a reference independently of its 3D visibility");

        // 12. Sketch editing stays functional and snaps to the shown gear without re-preparing it.
        juce::String line; const int before = (int) cache(panel)["solidPreparations"];
        t = now(); check(service->addSketchLine(sketch, -30, 0, -60, 0, line, error), "Sketch editing works with references shown", error); panel.refreshFromDocument(true); paint(panel);
        measure("Sketch line edit and repaint, Gear B shown (cached)", now() - t, cache(panel));
        check((int) cache(panel)["solidPreparations"] == before, "Sketch edits reuse the prepared gear reference");
        gear::Spur spurB; gear::spur(service->document().find(gearB)->modeling, spurB, error);
        const auto* fb = service->document().find(gearB); const double angle = (fb->rz) * juce::MathConstants<double>::pi / 180.0 + spurB.halfAngle(spurB.tipRadius);
        const auto snap = panel.sketchCanvas->probeSnap(fb->x + spurB.tipRadius * std::cos(angle), fb->y + spurB.tipRadius * std::sin(angle));
        check((bool) snap["found"] && (bool) snap["reference"] && snap["source"].toString() == gearB, "Snapping reaches Gear B's tooth tip vertex", juce::JSON::toString(snap, true));
        check(service->document().findSketch(sketch)->entities.size() == 1, "The sketch keeps its own geometry");

        // 10. Invalidation when the referenced gear changes; safe removal.
        auto update = juce::var(new juce::DynamicObject()); update.getDynamicObject()->setProperty("action", "update"); update.getDynamicObject()->setProperty("id", gearB);
        auto teeth = juce::var(new juce::DynamicObject()); teeth.getDynamicObject()->setProperty("teeth", 36); update.getDynamicObject()->setProperty("parameters", teeth);
        const auto curvesBefore = (int) cache(panel)["curves"];
        check((bool) service->gearCommand(update)["ok"], "Gear B can be edited while it is a sketch reference");
        panel.refreshFromDocument(true); paint(panel); state = cache(panel);
        check((int) state["solidPreparations"] == before + 1 && (int) state["curves"] != curvesBefore, "Editing the referenced gear re-prepares only its reference", juce::JSON::toString(state, true));

        // 9. Persistence of the sketch's reference choices.
        const auto saved = references(*service, sketch);
        for (const auto* ext : { "djo", "djc" })
        {
            const auto file = folder.getChildFile(juce::String("references.") + ext);
            CadCommandService loaded; const bool ok = service->save(file, error) && loaded.load(file, error);
            check(ok && references(loaded, sketch) == saved && references(loaded, other).isEmpty(), juce::String("Save and reopen (.") + ext + ") restores the sketch's reference choices", error + " " + references(loaded, sketch).joinIntoString(","));
        }
        {
            auto loaded = std::make_shared<CadCommandService>(); loaded->load(folder.getChildFile("references.djo"), error);
            CadWorkspacePanel reopened(loaded); reopened.setSize(1500, 950); reopened.refreshFromDocument(true); reopened.enterSketch(sketch); paint(reopened);
            const auto shown = cache(reopened);
            check(shown["solidSources"].toString() == gearB && reopened.sketchShown(other), "Reopening the sketch restores its selected background references", juce::JSON::toString(shown, true));
            reopened.finishSketch();
            juce::String fresh; loaded->createSketch("XZ", 0, fresh, error); reopened.refreshFromDocument(true); reopened.enterSketch(fresh); paint(reopened);
            check((int) cache(reopened)["solids"] == 0 && references(*loaded, fresh).isEmpty(), "A new sketch in the reopened document still starts with nothing shown");
            reopened.finishSketch();
        }
        check(service->remove(gearA, error), "A gear shown as a sketch reference can still be deleted normally", error);
        panel.refreshFromDocument(true); paint(panel);
        check(!panel.sketchReferenceApplicable(gearA) && !panel.sketchReferenceShown(gearA), "A deleted gear disappears from the references without errors");
        panel.finishSketch();
    }

    // 11. Idle: the real motion runtime and panel timer, with an assembly pair shown, do no repeated preparation.
    {
        juce::String gearA, gearB, sketch;
        auto service = pairDocument("create", gearA, gearB);
        service->createSketch("XY", 0, sketch, error);
        CadWorkspacePanel panel(service); panel.setSize(1500, 950);
        panel.motionRuntime = std::make_shared<CadMotionRuntime>(service); panel.motionRuntime->synchronize();
        panel.refreshFromDocument(true); panel.enterSketch(sketch);
        juce::String instanceA; for (const auto& i : service->document().mechanicsData().instances) if (i.featureId == gearA) instanceA = i.id;
        check(panel.sketchReferenceApplicable(instanceA) && !panel.sketchReferenceApplicable(gearA), "Assembly gears are offered through their instances (one control per displayed body)");
        panel.toggleVisibility(instanceA); panel.motionRuntime->synchronize(); panel.timerCallback(); paint(panel);
        const auto first = cache(panel);
        const auto revision = service->document().revision();
        const auto t = now();
        for (int i = 0; i < 60; ++i) { panel.motionRuntime->synchronize(); panel.timerCallback(); paint(panel); }
        const auto idle = cache(panel);
        measure("60 idle timer ticks and repaints with Gear A instance shown", now() - t, idle);
        check(service->document().revision() == revision && (int) idle["rebuilds"] == (int) first["rebuilds"] && (int) idle["solidPreparations"] == (int) first["solidPreparations"],
              "Idle ticks neither regenerate geometry nor rebuild or re-prepare references", juce::JSON::toString(first, true) + " -> " + juce::JSON::toString(idle, true));
        panel.finishSketch();
    }

    // Section cost when a sketch plane cuts through both gears and both are shown.
    {
        juce::String gearA, gearB, sketch;
        auto service = pairDocument("none", gearA, gearB);
        service->createSketch("XY", 5, sketch, error);
        CadWorkspacePanel panel(service); panel.setSize(1500, 950); panel.refreshFromDocument(true); panel.enterSketch(sketch);
        const auto t = now(); panel.toggleVisibility(gearA); panel.toggleVisibility(gearB); paint(panel);
        const auto state = cache(panel);
        measure("Show both gears on a plane cutting through them (section included)", now() - t, state);
        check(state["solidSources"].toString() == gearA + "," + gearB, "Both gears can be shown on a plane through their faces");
        // 20 and 40 teeth x 8 profile edges (fillets, radial flanks, involutes, tip, root) plus each bore.
        check((int) state["sectionCurves"] == 20 * 8 + 1 + 40 * 8 + 1, "The plane cut through both gears yields every profile edge exactly once", juce::JSON::toString(state, true));
        check((double) state["timing"]["sectionMs"] < 2000.0, "Cutting both gears takes under 2 s in Debug", juce::JSON::toString(state["timing"], true));
        panel.finishSketch();
        // Faces that are not straight sweeps still use the exact OCCT section: a box tilted 45 degrees about X.
        juce::String box, cutSketch; service->createBox(10, 10, 10, box, error); service->setPlacement(box, 100, 0, 0, 45, 0, 0, error); service->createSketch("XY", 3, cutSketch, error);
        panel.refreshFromDocument(true); panel.enterSketch(cutSketch); panel.toggleVisibility(box); paint(panel);
        const auto tilted = cache(panel);
        check((int) tilted["sectionCurves"] == 4, "A tilted box is still cut exactly (four section edges) through the OCCT fallback", juce::JSON::toString(tilted, true));
        panel.finishSketch();
    }

    auto* r = new juce::DynamicObject(); r->setProperty("checks", checks); r->setProperty("measurements", measurements); r->setProperty("total", checks.size()); r->setProperty("passed", checks.size() - failures); r->setProperty("failed", failures); r->setProperty("ok", failures == 0);
    report = r; return failures == 0;
}
