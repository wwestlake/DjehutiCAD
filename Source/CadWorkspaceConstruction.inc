// Working cursor, construction references and reference-based transforms (included by CadWorkspacePanel.cpp).
// References live in the document and change only through CadCommandService::referenceCommand / transformCommand,
// so they are transactional, undoable and saved. The cursor is temporary workspace state.

namespace
{
// Engineering numeric input: digits, sign, decimal point and exponent (1e-3, 2.5E+2).
constexpr const char* engineeringCharacters = "0123456789.-+eE";
}

bool CadWorkspacePanel::isReference(const juce::String& id) const { return id.isNotEmpty() && service->document().findConstruction(id) != nullptr; }
bool CadWorkspacePanel::referenceVisible(const juce::String& id) const { const auto* r = service->document().findConstruction(id); return r != nullptr && r->visible; }

void CadWorkspacePanel::updateConstructionDisplay(bool force)
{
    const auto& d = service->document(); const auto pose = motionRuntime != nullptr ? motionRuntime->pose() : nullptr;
    const auto key = juce::String(d.documentInstance()) + "|" + juce::String(d.revision()) + "|" + selectedId + "|" + juce::String(pose ? pose->serial : 0) + (task == Task::cursor ? "|K|" : "|-|")
                   + juce::String(cursorWorld[0], 9) + "," + juce::String(cursorWorld[1], 9) + "," + juce::String(cursorWorld[2], 9) + (cursorShown ? "|c" : "|-") + (editingSketchId.isNotEmpty() ? "s" : "m");
    if (!force && key == constructionKey) return;
    constructionKey = key;
    std::vector<Viewport3D::ConstructionDraw> draws;
    for (const auto& r : d.constructionData())
    {
        if (!r.visible) continue;
        const auto resolved = d.resolveConstruction(r.id);
        if (!resolved.ok) continue;
        Viewport3D::ConstructionDraw draw; draw.id = r.id; draw.kind = r.kind; draw.name = r.name; draw.selected = r.id == selectedId;
        draw.origin = { (float) resolved.world.x, (float) resolved.world.y, (float) resolved.world.z };
        const auto t = resolved.world.toTrsf();
        for (int i = 0; i < 3; ++i) { const auto a = gp_Dir(i == 0 ? 1 : 0, i == 1 ? 1 : 0, i == 2 ? 1 : 0).Transformed(t); draw.axes[(size_t) i] = { (float) a.X(), (float) a.Y(), (float) a.Z() }; }
        draws.push_back(std::move(draw));
    }
    Viewport3D::CursorDraw cursor; cursor.visible = cursorShown; cursor.selected = task == Task::cursor; cursor.position = { (float) cursorWorld[0], (float) cursorWorld[1], (float) cursorWorld[2] };
    modelView.setConstructionDisplay(std::move(draws), cursor);
    if (editingSketchId.isNotEmpty()) sketchCanvas->repaint();
}

const std::vector<djehuti::cad::CadSnapPoint>& CadWorkspacePanel::snapPointsFor(const juce::String& bodyId)
{
    const auto& d = service->document(); const auto pose = motionRuntime != nullptr ? motionRuntime->pose() : nullptr;
    const auto key = juce::String(d.documentInstance()) + "|" + juce::String(d.revision()) + "|" + juce::String(pose ? pose->serial : 0);
    if (key != snapCacheKey) { snapCache.clear(); snapCacheKey = key; }
    auto it = snapCache.find(bodyId);
    if (it == snapCache.end()) it = snapCache.emplace(bodyId, d.snapPoints(bodyId)).first;
    return it->second;
}

// Cursor target under the pointer: a reference, then a well-defined point of the body under it (vertex, edge midpoint,
// arc centre, planar face centre, cylinder axis midpoint), then the surface point under it, and finally the XY plane
// at the cursor's current Z. A screen click never invents a depth.
bool CadWorkspacePanel::cursorPointAt(juce::Point<float> position, std::array<double, 3>& world, juce::String& label)
{
    const auto& d = service->document();
    float best = 14.0f; bool found = false;
    const auto consider = [&](const std::array<double, 3>& p, float bonus, const juce::String& text)
    {
        juce::Point<float> s;
        if (!modelView.worldToScreen({ (float) p[0], (float) p[1], (float) p[2] }, s)) return;
        const float distance = s.getDistanceFrom(position) - bonus;
        if (s.getDistanceFrom(position) < 14.0f && distance < best) { best = distance; world = p; label = text; found = true; }
    };
    for (const auto& r : d.constructionData())
    {
        if (!r.visible) continue;
        const auto resolved = d.resolveConstruction(r.id); if (!resolved.ok) continue;
        consider({ resolved.world.x, resolved.world.y, resolved.world.z }, 4.0f, "reference " + r.name);
    }
    // Snap candidates come from every body within reach of the pointer, not just the one exactly under it, so a
    // vertex on a silhouette corner can still be reached.
    if (cursorSnapping)
    {
        juce::StringArray bodies;
        for (const auto& o : { std::pair<float, float> { 0, 0 }, { 12, 0 }, { -12, 0 }, { 0, 12 }, { 0, -12 }, { 9, 9 }, { -9, 9 }, { 9, -9 }, { -9, -9 } })
            if (const auto b = modelView.pick({ position.x + o.first, position.y + o.second }, djehuti::cad::CadSelection::Mode::body); b.valid()) bodies.addIfNotAlreadyThere(b.drawId);
        for (const auto& id : bodies)
            for (const auto& sp : snapPointsFor(id))
            {
                const float bonus = sp.type == "vertex" ? 3.0f : sp.type == "centre" ? 2.0f : sp.type == "midpoint" ? 1.0f : 0.0f;
                consider(sp.point, bonus, featureName(id) + " " + sp.label);
            }
    }
    if (found) return true;
    const auto hit = modelView.pick(position, djehuti::cad::CadSelection::Mode::face);
    if (hit.valid() && hit.hasPoint)
    {
        std::array<double, 3> p = hit.point;
        if (const auto pose = motionRuntime != nullptr ? motionRuntime->pose() : nullptr)
            for (const auto& i : pose->instances)
                if (i.instanceId == hit.drawId)
                {
                    const auto q = p;
                    for (int k = 0; k < 3; ++k) p[(size_t) k] = i.matrix[(size_t) k] * q[0] + i.matrix[(size_t) (4 + k)] * q[1] + i.matrix[(size_t) (8 + k)] * q[2] + i.matrix[(size_t) (12 + k)];
                }
        world = p; label = "surface of " + featureName(hit.drawId); return true;
    }
    std::array<float, 3> o {}, dir {};
    if (modelView.screenRay(position, o, dir) && std::abs(dir[2]) > 1.0e-6f)
    {
        const double t = (cursorWorld[2] - o[2]) / dir[2];
        if (t > 0) { world = { o[0] + t * dir[0], o[1] + t * dir[1], cursorWorld[2] }; label = "XY plane at Z = " + juce::String(cursorWorld[2], 3) + " mm"; return true; }
    }
    return false;
}

void CadWorkspacePanel::setCursor(const std::array<double, 3>& world, const juce::String& note)
{
    cursorWorld = world; cursorNote = note;
    updateConstructionDisplay(true);
    if (task == Task::cursor) rebuildTaskPanel(false);
}

void CadWorkspacePanel::setCursorPlacing(bool on)
{
    if (editingSketchId.isNotEmpty()) sketchCanvas->setTool(on ? "cursor" : "select");
    else modelView.setCursorPlacing(on);
    updateFloatingTools();
    setMessage(on ? "Place cursor: click a snap point, a surface, or the XY plane at the cursor's Z (Esc cancels). It snaps to vertices, edge midpoints, arc centres, face centres, cylinder axes and references."
                  : "Cursor placement ended.", 0);
}

bool CadWorkspacePanel::bodyCentre(const juce::String& id, std::array<double, 3>& centre) const
{
    const auto& d = service->document();
    const auto* instance = d.mechanicsData().findInstance(id);
    const auto* f = d.find(instance != nullptr ? instance->featureId : id);
    if (f == nullptr || f->shape.IsNull() || f->type == "sketch") return false;
    gp_Trsf world;
    if (instance != nullptr) { gp_Trsf frame = instance->placement.toTrsf(); juce::String ignored; d.referenceWorld(id, frame, ignored); world = d.instanceShapeTransform(f->id, frame); }
    Bnd_Box box; BRepBndLib::Add(f->shape.Moved(TopLoc_Location(world)), box, true);
    if (box.IsVoid()) return false;
    double x0, y0, z0, x1, y1, z1; box.Get(x0, y0, z0, x1, y1, z1);
    centre = { (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2 }; return true;
}

bool CadWorkspacePanel::cursorToSelection(juce::String& note)
{
    const auto& d = service->document();
    if (const auto* r = d.findConstruction(selectedId))
    {
        const auto resolved = d.resolveConstruction(r->id);
        if (!resolved.ok) { note = r->name + " is broken: " + resolved.problem; return false; }
        setCursor({ resolved.world.x, resolved.world.y, resolved.world.z }, "reference " + r->name); note = "Cursor moved to " + r->name + "."; return true;
    }
    const auto pick = selection != nullptr ? selection->selected() : djehuti::cad::CadPick();
    if (pick.valid() && pick.face >= 0)
    {
        for (const auto& sp : snapPointsFor(pick.drawId))
            if (sp.label == "Face " + juce::String(pick.face + 1) + " centre" || sp.label == "Cylinder " + juce::String(pick.face + 1) + " axis midpoint")
            { setCursor(sp.point, featureName(pick.drawId) + " " + sp.label); note = "Cursor moved to the " + sp.label + " of " + featureName(pick.drawId) + "."; return true; }
        note = "That face has no well-defined centre (only planar faces and cylinders do)."; return false;
    }
    std::array<double, 3> c {};
    if (selectedId.isNotEmpty() && bodyCentre(selectedId, c)) { setCursor(c, featureName(selectedId) + " bounds centre"); note = "Cursor moved to the bounds centre of " + featureName(selectedId) + "."; return true; }
    note = "Select a body, occurrence, planar face or reference first."; return false;
}

void CadWorkspacePanel::placeCursorFromSketch(double u, double v, const juce::String& snapNote)
{
    djehuti::cad::CadSketchFrame frame;
    if (!service->document().sketchFrame(editingSketchId, frame)) { showError("The sketch plane is not available."); return; }
    const auto p = frame.origin.Translated(gp_Vec(frame.u) * u + gp_Vec(frame.v) * v);
    sketchCanvas->setTool("select"); updateFloatingTools();
    const auto source = snapNote.fromFirstOccurrenceOf("|", false, false).upToFirstOccurrenceOf("/", false, false);
    setCursor({ p.X(), p.Y(), p.Z() }, source.isNotEmpty() ? (isReference(source) ? "reference " : "sketch ") + featureName(source) : juce::String("sketch plane"));
    setMessage("Cursor placed on the sketch plane at U " + juce::String(u, 3) + ", V " + juce::String(v, 3) + " mm.", 0);
}

juce::var CadWorkspacePanel::cursorState() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty("x", cursorWorld[0]); o->setProperty("y", cursorWorld[1]); o->setProperty("z", cursorWorld[2]);
    o->setProperty("note", cursorNote); o->setProperty("shown", cursorShown); o->setProperty("snapping", cursorSnapping);
    o->setProperty("placing", editingSketchId.isNotEmpty() ? sketchCanvas->tool() == "cursor" : modelView.isCursorPlacing());
    if (editingSketchId.isNotEmpty())
    {
        djehuti::cad::CadSketchFrame f;
        if (service->document().sketchFrame(editingSketchId, f))
        {
            const gp_Vec d(f.origin, gp_Pnt(cursorWorld[0], cursorWorld[1], cursorWorld[2])); const auto n = f.u.Crossed(f.v);
            auto* local = new juce::DynamicObject(); local->setProperty("u", d.Dot(gp_Vec(f.u))); local->setProperty("v", d.Dot(gp_Vec(f.v))); local->setProperty("w", d.Dot(gp_Vec(n))); o->setProperty("sketch", local);
        }
    }
    o->setProperty("units", "mm");
    o->setProperty("selected", task == Task::cursor);
    return o;
}

// The selected marker (the working cursor or a construction reference) with its editable properties; void otherwise.
juce::var CadWorkspacePanel::markerState() const
{
    auto* o = new juce::DynamicObject();
    if (task == Task::cursor) { o->setProperty("type", "cursor"); o->setProperty("id", "@cursor"); o->setProperty("temporary", true); o->setProperty("cursor", cursorState()); return o; }
    if (task == Task::reference && editingReferenceId.isNotEmpty())
        if (const auto* r = service->document().findConstruction(editingReferenceId)) { o->setProperty("type", r->kind); o->setProperty("id", r->id); o->setProperty("temporary", false); o->setProperty("reference", service->document().constructionToJson(*r)); return o; }
    return juce::var();
}

juce::var CadWorkspacePanel::cursorCommand(const juce::var& args, juce::String& error)
{
    const auto op = args.getProperty("operation", "get").toString();
    const auto number = [&](const char* key, double& out) { if (!args.hasProperty(key)) return false; const auto v = args[key]; if (!(v.isInt() || v.isInt64() || v.isDouble()) || !std::isfinite((double) v) || std::abs((double) v) > 1.0e9) { error = juce::String(key) + " must be a finite number within +/-1e9."; return false; } out = (double) v; return true; };
    if (op == "get") {}
    else if (op == "reset") setCursor({ 0, 0, 0 }, "origin");
    else if (op == "set")
    {
        auto w = cursorWorld;
        if (args.hasProperty("u") || args.hasProperty("v"))
        {
            if (editingSketchId.isEmpty()) { error = "u/v are sketch-plane coordinates; open a sketch or use x, y, z."; return cursorState(); }
            djehuti::cad::CadSketchFrame f; if (!service->document().sketchFrame(editingSketchId, f)) { error = "The sketch plane is not available."; return cursorState(); }
            const gp_Vec d(f.origin, gp_Pnt(w[0], w[1], w[2])); double u = d.Dot(gp_Vec(f.u)), v = d.Dot(gp_Vec(f.v));
            number("u", u); number("v", v); if (error.isNotEmpty()) return cursorState();
            const auto p = f.origin.Translated(gp_Vec(f.u) * u + gp_Vec(f.v) * v); w = { p.X(), p.Y(), p.Z() };
        }
        else { number("x", w[0]); number("y", w[1]); number("z", w[2]); if (error.isNotEmpty()) return cursorState(); }
        setCursor(w, "typed coordinates");
    }
    else if (op == "select") { startTask(Task::cursor); }
    else if (op == "toSelection") { juce::String note; if (!cursorToSelection(note)) error = note; }
    else if (op == "toReference")
    {
        const auto id = args["id"].toString(); const auto* r = service->document().findConstruction(id);
        if (r == nullptr) error = "Unknown construction reference " + id + ".";
        else { const auto resolved = service->document().resolveConstruction(id); if (!resolved.ok) error = r->name + " is broken: " + resolved.problem; else setCursor({ resolved.world.x, resolved.world.y, resolved.world.z }, "reference " + r->name); }
    }
    else if (op == "snap")
    {
        // Snap to a listed point of a body: by label (see cad_reference snapPoints), or type + index among that type.
        const auto body = args["body"].toString();
        if (service->document().mechanicsData().findInstance(body) == nullptr && (service->document().find(body) == nullptr || service->document().find(body)->shape.IsNull())) error = "Unknown body or occurrence: " + body;
        else
        {
            const auto& points = snapPointsFor(body); const djehuti::cad::CadSnapPoint* chosen = nullptr; int seen = 0;
            const auto label = args["label"].toString(), type = args["type"].toString(); const int index = args.getProperty("index", 0);
            for (const auto& p : points)
            {
                if (label.isNotEmpty()) { if (p.label == label) { chosen = &p; break; } }
                else if (p.type == type && seen++ == index) { chosen = &p; break; }
            }
            if (chosen == nullptr) error = "No such snap point on " + featureName(body) + "; list them with cad_reference snapPoints.";
            else setCursor(chosen->point, featureName(body) + " " + chosen->label);
        }
    }
    else if (op == "place")
    {
        double x = 0, y = 0;
        if (!number("viewX", x) || !number("viewY", y)) { if (error.isEmpty()) error = "place needs viewX and viewY (pixels in the model view)."; }
        else { std::array<double, 3> w {}; juce::String label; if (cursorPointAt({ (float) x, (float) y }, w, label)) setCursor(w, label); else error = "Nothing to place the cursor on at that point."; }
    }
    else if (op == "options")
    {
        if (args.hasProperty("shown")) { if (!args["shown"].isBool()) error = "shown must be true or false."; else { cursorShown = args["shown"]; updateConstructionDisplay(true); } }
        if (args.hasProperty("snapping")) { if (!args["snapping"].isBool()) error = "snapping must be true or false."; else cursorSnapping = args["snapping"]; }
    }
    else error = "operation must be get, set, reset, select, toSelection, toReference, snap, place or options.";
    return cursorState();
}

juce::StringArray CadWorkspacePanel::referenceOptions(bool includeBlank, const juce::String& blank)
{
    juce::StringArray labels; referenceChoiceIds.clear();
    if (includeBlank) { labels.add(blank); referenceChoiceIds.add(juce::String()); }
    for (const auto& r : service->document().constructionData()) { labels.add(r.name + " (" + (r.kind == "cs" ? juce::String("coordinate system") : r.kind) + ")"); referenceChoiceIds.add(r.id); }
    return labels;
}

juce::StringArray CadWorkspacePanel::targetOptions(const juce::String& blank)
{
    juce::StringArray labels { blank }; targetChoiceIds.clear(); targetChoiceIds.add(juce::String());
    const auto& d = service->document();
    for (const auto& i : d.mechanicsData().instances) if (const auto* f = d.find(i.featureId); f != nullptr && !f->shape.IsNull()) { labels.add(i.name + " (occurrence)"); targetChoiceIds.add(i.id); }
    for (const auto& f : d.features())
    {
        if (f.type == "sketch" || f.shape.IsNull() || f.type == "component") continue;
        bool instanced = false; for (const auto& i : d.mechanicsData().instances) instanced = instanced || i.featureId == f.id;
        if (!instanced) { labels.add(f.name + " (body)"); targetChoiceIds.add(f.id); }
    }
    return labels;
}

void CadWorkspacePanel::addCursorFields()
{
    form->addHeader("Position in the model (mm)");
    form->addNote("cursorNote", "Temporary construction point (" + cursorNote + "). Units are millimetres. Moving it never changes geometry or earlier operations.", 3);
    // Fields show nine decimals (nanometres) and remember the text they showed: a field the user did not touch is
    // never re-submitted, so applying one coordinate cannot round or alter the others.
    const auto coordinate = [this](const char* key, const char* caption, double value, const char* unit, const char* tip)
    { auto& e = form->addNumber(key, caption, value, unit, tip, 9); e.setInputRestrictions(32, engineeringCharacters); formInitial[key] = e.getText(); formExact[key] = value; };
    coordinate("cursorX", "X", cursorWorld[0], "mm", "World X of the working cursor, in millimetres (the document's unit).");
    coordinate("cursorY", "Y", cursorWorld[1], "mm", "World Y of the working cursor, in millimetres.");
    coordinate("cursorZ", "Z", cursorWorld[2], "mm", "World Z of the working cursor, in millimetres.");
    form->addButton("cursorSet", "Set cursor to these coordinates", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] {
        if (safe == nullptr || safe->form == nullptr) return;
        for (const auto* k : { "cursorX", "cursorY", "cursorZ" }) if (!safe->form->numberValid(k)) { safe->showError("Cursor coordinates must be numbers."); return; }
        const auto value = [&](const char* key, double current) { const auto it = safe->formInitial.find(key); return it != safe->formInitial.end() && it->second == safe->form->text(key) ? current : safe->form->number(key); };
        safe->setCursor({ value("cursorX", safe->cursorWorld[0]), value("cursorY", safe->cursorWorld[1]), value("cursorZ", safe->cursorWorld[2]) }, "typed coordinates"); safe->setMessage("Cursor set to the typed coordinates (mm).", 0); }); }, "Move the cursor to exactly these world coordinates. Typed values are used as entered.");
    if (editingSketchId.isNotEmpty())
    {
        const auto state = cursorState()["sketch"];
        form->addHeader("On the sketch plane (sketch coordinates)");
        coordinate("cursorU", "U", (double) state["u"], "mm", "Cursor position along the sketch's U axis, in millimetres.");
        coordinate("cursorV", "V", (double) state["v"], "mm", "Cursor position along the sketch's V axis, in millimetres.");
        form->addNote("cursorW", "Distance from the sketch plane: " + juce::String((double) state["w"], 4) + " mm", 1);
        form->addButton("cursorSetSketch", "Set on the sketch plane", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] {
            if (safe == nullptr || safe->form == nullptr) return;
            if (!safe->form->numberValid("cursorU") || !safe->form->numberValid("cursorV")) { safe->showError("U and V must be numbers."); return; }
            const auto current = safe->cursorState()["sketch"];
            const auto value = [&](const char* key, double now) { const auto it = safe->formInitial.find(key); return it != safe->formInitial.end() && it->second == safe->form->text(key) ? now : safe->form->number(key); };
            safe->placeCursorFromSketch(value("cursorU", (double) current["u"]), value("cursorV", (double) current["v"]), {}); }); }, "Put the cursor on the sketch plane at these sketch coordinates.");
    }
    form->addHeader("Placement");
    auto& shown = form->addToggle("cursorShown", "Show cursor", cursorShown, "Display the working cursor in the view.");
    auto prior = shown.onClick; shown.onClick = [this, prior] { if (prior) prior(); if (form != nullptr) { cursorShown = form->toggle("cursorShown"); updateConstructionDisplay(true); } };
    auto& snapping = form->addToggle("cursorSnap", "Snap to geometry and references", cursorSnapping, "Snap to vertices, edge midpoints, arc centres, planar face centres, cylinder axes and references.");
    auto priorSnap = snapping.onClick; snapping.onClick = [this, priorSnap] { if (priorSnap) priorSnap(); if (form != nullptr) cursorSnapping = form->toggle("cursorSnap"); };
    form->addButton("cursorPlace", "Place by clicking...", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) safe->setCursorPlacing(true); }); }, editingSketchId.isNotEmpty() ? "Click in the sketch; it snaps to sketch geometry and references and lies on the sketch plane." : "Click in the model view.");
    form->addButton("cursorToSelection", "Cursor to selection", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe == nullptr) return; juce::String note; if (safe->cursorToSelection(note)) safe->setMessage(note, 0); else safe->showError(note); }); }, "Move the cursor to the selected reference, face centre or body's bounds centre.");
    form->addButton("cursorReset", "Reset to origin", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] { if (safe != nullptr) { safe->setCursor({ 0, 0, 0 }, "origin"); safe->setMessage("Cursor reset to the modeling origin.", 0); } }); }, "Return the cursor to (0, 0, 0).");
    form->addHeader("Create a reference at the cursor");
    form->addChoice("cursorRefKind", "Kind", { "Point", "Axis", "Coordinate system", "Plane" }, 0, editingSketchId.isNotEmpty() ? "Axes and planes use the sketch normal; a coordinate system uses the sketch axes." : "Axes and planes point along +Z; edit the direction afterwards.");
    form->addText("cursorRefName", "Name", {}, "Optional reference name.");
    form->addButton("cursorRefCreate", "Create reference at cursor", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] {
        if (safe == nullptr || safe->form == nullptr) return;
        const juce::StringArray kinds { "point", "axis", "cs", "plane" }; const auto kind = kinds[juce::jlimit(0, 3, safe->form->choice("cursorRefKind"))];
        auto def = juce::var(new juce::DynamicObject()); auto* p = def.getDynamicObject();
        p->setProperty("kind", kind); if (safe->form->text("cursorRefName").isNotEmpty()) p->setProperty("name", safe->form->text("cursorRefName"));
        p->setProperty("origin", juce::Array<juce::var> { safe->cursorWorld[0], safe->cursorWorld[1], safe->cursorWorld[2] });
        djehuti::cad::CadSketchFrame f; const bool sketching = safe->editingSketchId.isNotEmpty() && safe->service->document().sketchFrame(safe->editingSketchId, f);
        if (kind == "axis" || kind == "plane") { const auto n = sketching ? f.u.Crossed(f.v) : gp_Dir(0, 0, 1); p->setProperty("direction", juce::Array<juce::var> { n.X(), n.Y(), n.Z() }); }
        if (kind == "cs" && sketching) { gp_Trsf t; t.SetTransformation(gp_Ax3(gp_Pnt(), f.u.Crossed(f.v), f.u)); double rx = 0, ry = 0, rz = 0; djehuti::cad::mech::eulerDegreesFromFrame(djehuti::cad::CadFrame::fromTrsf(t.Inverted()), rx, ry, rz); p->setProperty("rotationDegrees", juce::Array<juce::var> { rx, ry, rz }); }
        auto request = juce::var(new juce::DynamicObject()); request.getDynamicObject()->setProperty("action", "create"); request.getDynamicObject()->setProperty("parameters", def);
        const auto reply = safe->service->referenceCommand(request);
        if (!(bool) reply["ok"]) { safe->showError(reply["error"].toString()); return; }
        safe->refreshFromDocument(true); safe->rebuildTaskPanel(false);
        safe->setMessage("Created " + reply["reference"]["name"].toString() + " at the cursor. Select it in the tree to edit or associate it.", 0); }); }, "Create a persistent, document-owned reference at the cursor position.");
}

void CadWorkspacePanel::addReferenceFields()
{
    const auto& d = service->document();
    const auto* existing = d.findConstruction(editingReferenceId);
    juce::String kind = existing != nullptr ? existing->kind : "point", assoc = existing != nullptr ? existing->association : "fixed";
    std::array<double, 3> origin = cursorWorld, direction { 0, 0, 1 }, rotation { 0, 0, 0 };
    juce::String problem, face = existing != nullptr ? existing->face : juce::String();
    if (existing != nullptr)
    {
        const auto json = d.constructionToJson(*existing);
        const auto source = (json["status"].toString() == "ok") ? json["world"] : djehuti::cad::construction::frameToPresentation(existing->frame);
        for (int i = 0; i < 3; ++i) { origin[(size_t) i] = source["origin"][i]; direction[(size_t) i] = source["direction"][i]; rotation[(size_t) i] = source["rotationDegrees"][i]; }
        if (json["status"].toString() != "ok") problem = json["problem"].toString();
    }
    form->addHeader(existing != nullptr ? "Edit reference" : "New reference");
    if (problem.isNotEmpty()) form->addNote("refProblem", "BROKEN: " + problem + " The reference keeps its stored values; fix the association or delete it.", 4);
    form->addText("refName", "Name", existing != nullptr ? existing->name : juce::String(), "Reference name (blank gives a numbered default).");
    form->addChoice("refKind", "Kind", { "Point", "Axis", "Coordinate system", "Plane" }, juce::jmax(0, juce::StringArray { "point", "axis", "cs", "plane" }.indexOf(kind)), "Point: position. Axis: line along a direction. Coordinate system: origin and orientation. Plane: origin and normal.");
    form->addToggle("refVisible", "Visible", existing != nullptr ? existing->visible : true, "Show the reference in the view. Hidden references still work in operations.");
    form->addChoice("refAssoc", "Behaviour", { "Fixed", "Follow a body or occurrence", "Axis of a cylindrical face", "Centre of a face", "Offset from another reference" }, juce::jmax(0, juce::StringArray { "fixed", "frame", "cylinderAxis", "faceCentre", "reference" }.indexOf(assoc)),
                    "Fixed references keep their coordinates. The others follow their source by stable id and report a broken association if it disappears.");
    form->addNote("refAssocNote", {}, 3);
    juce::StringArray sources { "(choose)" }; sourceChoiceIds.clear(); sourceChoiceIds.add(juce::String()); int sourceIndex = 0;
    for (const auto& i : d.mechanicsData().instances) { sources.add("Occurrence: " + i.name); sourceChoiceIds.add(i.id); }
    for (const auto& f : d.features())
    {
        if (f.type == "sketch" || f.shape.IsNull()) continue;
        bool instanced = false; for (const auto& i : d.mechanicsData().instances) instanced = instanced || i.featureId == f.id;
        if (!instanced) { sources.add("Body: " + f.name); sourceChoiceIds.add(f.id); }
    }
    for (const auto& r : d.constructionData()) if (r.id != editingReferenceId) { sources.add("Reference: " + r.name); sourceChoiceIds.add(r.id); }
    if (existing != nullptr) sourceIndex = juce::jmax(0, sourceChoiceIds.indexOf(existing->source));
    form->addChoice("refSource", "Source", sources, sourceIndex, "The occurrence, body or reference this one follows.");
    form->addText("refFace", "Face", face, "Persistent face token (for example gear-1/bore), from cad_list_faces or 'Use selected face'.");
    form->addButton("refUseFace", "Use selected face", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] {
        if (safe == nullptr || safe->form == nullptr) return;
        const auto pick = safe->selection != nullptr ? safe->selection->selected() : djehuti::cad::CadPick();
        if (!pick.valid() || pick.face < 0) { safe->showError("Select a face in Face mode first."); return; }
        std::vector<djehuti::cad::CadFaceInfo> faces; juce::String e; juce::String token;
        if (safe->service->listFaces(pick.featureId, faces, e)) for (const auto& f : faces) if (f.faceIndex == pick.face) token = f.token;
        if (token.isEmpty()) { safe->showError("That face has no persistent name and cannot be referenced."); return; }
        if (auto* box = dynamic_cast<juce::ComboBox*>(safe->form->control("refSource"))) { const int i = safe->sourceChoiceIds.indexOf(pick.drawId); if (i >= 0) box->setSelectedItemIndex(i, juce::sendNotificationSync); }
        if (auto* text = dynamic_cast<juce::TextEditor*>(safe->form->control("refFace"))) text->setText(token, true);
        safe->setMessage("Using face " + token + " of " + safe->featureName(pick.drawId) + ".", 0); }); }, "Fill Source and Face from the face selected in the model view.");
    const auto coordinate = [this](const char* key, const char* caption, double value, const char* unit, const char* tip)
    { auto& e = form->addNumber(key, caption, value, unit, tip, 9); e.setInputRestrictions(32, engineeringCharacters); formInitial[key] = e.getText(); formExact[key] = value; };
    form->addHeader("Position (world, millimetres)");
    coordinate("refOx", "X", origin[0], "mm", "World X of the reference origin, in millimetres."); coordinate("refOy", "Y", origin[1], "mm", "World Y of the reference origin, in millimetres."); coordinate("refOz", "Z", origin[2], "mm", "World Z of the reference origin, in millimetres.");
    form->addHeader("Direction");
    coordinate("refDx", "Direction X", direction[0], {}, "Axis direction / plane normal. Any non-zero vector; it is normalised."); coordinate("refDy", "Direction Y", direction[1], {}, "Axis direction / plane normal."); coordinate("refDz", "Direction Z", direction[2], {}, "Axis direction / plane normal.");
    form->addHeader("Orientation");
    coordinate("refRx", "Rotate X", rotation[0], "deg", "Rotation about world X, applied first (extrinsic X, Y, Z), in degrees."); coordinate("refRy", "Rotate Y", rotation[1], "deg", "Rotation about world Y, in degrees."); coordinate("refRz", "Rotate Z", rotation[2], "deg", "Rotation about world Z, applied last, in degrees.");
    form->addHeader("Actions");
    form->addButton("refCursorHere", "Cursor to this position", [this] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this)] {
        if (safe == nullptr || safe->form == nullptr) return;
        for (const auto* k : { "refOx", "refOy", "refOz" }) if (!safe->form->numberValid(k)) { safe->showError("Position must be numbers."); return; }
        safe->setCursor({ safe->form->number("refOx"), safe->form->number("refOy"), safe->form->number("refOz") }, "reference position"); safe->setMessage("Cursor moved to the reference position.", 0); }); }, "Move the working cursor to the position in these fields.");
    if (existing != nullptr)
        form->addButton("refDelete", "Delete reference", [this, id = editingReferenceId] { postUiAction([safe = juce::Component::SafePointer<CadWorkspacePanel>(this), id] {
            if (safe == nullptr) return;
            auto a = juce::var(new juce::DynamicObject()); a.getDynamicObject()->setProperty("action", "delete"); a.getDynamicObject()->setProperty("id", id);
            const auto reply = safe->service->referenceCommand(a);
            if (!(bool) reply["ok"]) { safe->showError(reply["error"].toString()); return; }
            safe->refreshFromDocument(true); safe->closeConstructionTask();
            safe->setMessage("Reference deleted. Undo restores it." + (reply["warning"].toString().isNotEmpty() ? " " + reply["warning"].toString() : juce::String()), 0); }); }, "Delete this reference. References that were derived from it stay and report a broken association.");
}

juce::var CadWorkspacePanel::readReferenceForm(juce::String& error) const
{
    auto def = juce::var(new juce::DynamicObject()); auto* p = def.getDynamicObject();
    if (form == nullptr) return def;
    const juce::StringArray kinds { "point", "axis", "cs", "plane" }, types { "fixed", "frame", "cylinderAxis", "faceCentre", "reference" };
    const auto kind = kinds[juce::jlimit(0, 3, form->choice("refKind"))], type = types[juce::jlimit(0, 4, form->choice("refAssoc"))];
    p->setProperty("kind", kind); p->setProperty("name", form->text("refName")); p->setProperty("visible", form->toggle("refVisible"));
    auto assoc = juce::var(new juce::DynamicObject()); assoc.getDynamicObject()->setProperty("type", type);
    if (type != "fixed")
    {
        const int index = form->choice("refSource");
        if (index <= 0 || index >= sourceChoiceIds.size()) { error = "Choose the source this reference follows."; return def; }
        assoc.getDynamicObject()->setProperty("source", sourceChoiceIds[index]);
        if (type == "cylinderAxis" || type == "faceCentre") { if (form->text("refFace").isEmpty()) { error = "Enter or pick the face token."; return def; } assoc.getDynamicObject()->setProperty("face", form->text("refFace")); }
    }
    p->setProperty("association", assoc);
    if (type != "cylinderAxis" && type != "faceCentre")
    {
        const bool editing = editingReferenceId.isNotEmpty();
        // When editing, a group whose fields still show what the form built is not re-submitted, so the stored exact
        // values (and an associative reference's offset) are never rewritten from rounded display text.
        const auto unchanged = [&](const char* a, const char* b, const char* c)
        { if (!editing) return false; for (const auto* k : { a, b, c }) { const auto it = formInitial.find(k); if (it == formInitial.end() || it->second != form->text(k)) return false; } return true; };
        const auto numbers = [&](const char* a, const char* b, const char* c, juce::Array<juce::var>& out) { for (const auto* k : { a, b, c }) { if (!form->numberValid(k)) { error = "Position, direction and rotation must be valid numbers (digits, sign, decimal point, exponent)."; return false; } const auto init = formInitial.find(k); const auto exact = formExact.find(k); out.add(editing && init != formInitial.end() && exact != formExact.end() && init->second == form->text(k) ? exact->second : form->number(k)); } return true; };
        juce::Array<juce::var> origin, direction, rotation;
        if (!unchanged("refOx", "refOy", "refOz")) { if (!numbers("refOx", "refOy", "refOz", origin)) return def; p->setProperty("origin", origin); }
        if ((kind == "axis" || kind == "plane") && !unchanged("refDx", "refDy", "refDz"))
        {
            if (!numbers("refDx", "refDy", "refDz", direction)) return def;
            if (std::hypot((double) direction[0], (double) direction[1], (double) direction[2]) < 1.0e-12) { error = "The direction must not be the zero vector."; return def; }
            p->setProperty("direction", direction);
        }
        if (kind == "cs" && !unchanged("refRx", "refRy", "refRz")) { if (!numbers("refRx", "refRy", "refRz", rotation)) return def; p->setProperty("rotationDegrees", rotation); }
    }
    return def;
}

void CadWorkspacePanel::acceptReference()
{
    juce::String e; const auto def = readReferenceForm(e);
    if (e.isNotEmpty()) { showError(e); return; }
    auto request = juce::var(new juce::DynamicObject()); request.getDynamicObject()->setProperty("action", editingReferenceId.isEmpty() ? "create" : "update");
    request.getDynamicObject()->setProperty("id", editingReferenceId); request.getDynamicObject()->setProperty("parameters", def);
    const auto reply = service->referenceCommand(request);
    if (!(bool) reply["ok"]) { showError(reply["error"].toString()); return; }
    const auto id = reply["id"].toString(); refreshFromDocument(true);
    setMessage((editingReferenceId.isEmpty() ? "Created " : "Updated ") + reply["reference"]["name"].toString() + ".", 0);
    if (editingSketchId.isNotEmpty()) closeConstructionTask(); else selectFeature(id);
}

void CadWorkspacePanel::closeConstructionTask()
{
    editingReferenceId.clear();
    if (editingSketchId.isNotEmpty()) { task = Task::sketchEdit; selectedId = editingSketchId; syncTreeSelection(); updateHighlights(); updateToolbar(); rebuildTaskPanel(false); return; }
    task = Task::none; selectedId.clear(); syncTreeSelection(); updateHighlights(); updateToolbar(); rebuildTaskPanel(false);
}

void CadWorkspacePanel::toggleReferenceVisible(const juce::String& id)
{
    const auto* r = service->document().findConstruction(id); if (r == nullptr) return;
    auto def = juce::var(new juce::DynamicObject()); def.getDynamicObject()->setProperty("visible", !r->visible);
    auto a = juce::var(new juce::DynamicObject()); a.getDynamicObject()->setProperty("action", "update"); a.getDynamicObject()->setProperty("id", id); a.getDynamicObject()->setProperty("parameters", def);
    const auto reply = service->referenceCommand(a);
    if (!(bool) reply["ok"]) { showError(reply["error"].toString()); return; }
    refreshFromDocument(true); tree.repaint();
    setMessage(reply["reference"]["name"].toString() + ((bool) reply["reference"]["visible"] ? " is shown." : " is hidden."), 0);
}

void CadWorkspacePanel::showReferenceMenu(const juce::String& id)
{
    const auto* r = service->document().findConstruction(id); if (r == nullptr) return;
    juce::PopupMenu menu;
    menu.addItem(1, "Edit reference"); menu.addItem(2, r->visible ? "Hide" : "Show"); menu.addItem(3, "Move cursor here");
    menu.addItem(4, manipulatorPivotRef == id ? "Stop using as Move / Rotate pivot" : "Use as Move / Rotate pivot"); menu.addSeparator(); menu.addItem(5, "Delete reference");
    menu.showMenuAsync(juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<CadWorkspacePanel>(this), id](int result)
    {
        if (safe == nullptr || result == 0 || !safe->isReference(id)) return;   // the reference may have been deleted while the menu was open
        switch (result)
        {
            case 1: safe->selectFeature(id); break;
            case 2: safe->toggleReferenceVisible(id); break;
            case 3: { juce::String note; safe->selectedId = id; if (safe->cursorToSelection(note)) safe->setMessage(note, 0); else safe->showError(note); break; }
            case 4: safe->manipulatorPivotRef = safe->manipulatorPivotRef == id ? juce::String() : id; safe->updateManipulator(true); safe->setMessage(safe->manipulatorPivotRef.isEmpty() ? "Move / Rotate pivots at the body centre again." : "Move / Rotate now pivots at this reference.", 0); break;
            case 5: { auto a = juce::var(new juce::DynamicObject()); a.getDynamicObject()->setProperty("action", "delete"); a.getDynamicObject()->setProperty("id", id);
                      const auto reply = safe->service->referenceCommand(a); if (!(bool) reply["ok"]) safe->showError(reply["error"].toString()); else { if (safe->manipulatorPivotRef == id) safe->manipulatorPivotRef.clear(); safe->refreshFromDocument(true); if (safe->selectedId == id) safe->closeConstructionTask(); safe->setMessage("Reference deleted. Undo restores it.", 0); } break; }
            default: break;
        }
    });
}

void CadWorkspacePanel::addReferenceTransformFields()
{
    const auto& d = service->document();
    int targetIndex = 0; const auto targets = targetOptions("(choose a body or occurrence)");
    if (const int found = targetChoiceIds.indexOf(selectedId); found > 0) targetIndex = found;
    form->addHeader("Operation");
    form->addChoice("trOp", "Operation", { "Move by displacement", "Move to a reference", "Rotate about a reference axis", "Align to a reference", "Mirror across a reference plane", "Scale about a reference point" }, 0, "The operation the selected reference geometry drives.");
    form->addChoice("trTarget", "Part", targets, targetIndex, "The body or assembly occurrence to transform. An occurrence changes only its placement; its component definition is never altered.");
    form->addToggle("trCopy", "Create copy", false, "Off: transform the selected original. On: leave the original and create a transformed copy (an occurrence is copied as another occurrence of the same definition; mirror keeps the source body).");
    form->addHeader("Displacement");
    form->addNumber("trDx", "dX", 0, "mm", "Displacement along X (or along the frame's X).", 4); form->addNumber("trDy", "dY", 0, "mm", "Displacement along Y.", 4); form->addNumber("trDz", "dZ", 0, "mm", "Displacement along Z.", 4);
    const auto refs1 = referenceOptions(true, "(choose a reference)");
    form->addHeader("References");
    form->addChoice("trRef1", "Reference", refs1, 0, "Click a reference in the view or tree to fill the next empty slot.");
    juce::StringArray refs2 = refs1; refs2.set(0, "(none)");
    form->addChoice("trRef2", "Second reference", refs2, 0, "Used by 'Move to' (optional start point) and 'Align' (the reference to align to).");
    form->addNumber("trAngle", "Angle", 90.0, "deg", "Rotation about the reference axis (right-hand rule).", 4);
    form->addToggle("trFlip", "Flip direction", false, "Align with the target direction reversed.");
    form->addNumber("trFactor", "Scale factor", 2.0, {}, "Uniform scale (0.001 to 1000). Supported for box and cylinder bodies.", 6);
    form->addNote("trHelp", {}, 3);
    juce::ignoreUnused(d);
}

juce::var CadWorkspacePanel::readReferenceTransform(juce::String& error) const
{
    auto args = juce::var(new juce::DynamicObject()); auto* a = args.getDynamicObject();
    if (form == nullptr) return args;
    const juce::StringArray ops { "move", "move", "rotate", "align", "mirror", "scale" };
    const int op = juce::jlimit(0, 5, form->choice("trOp")), targetIndex = form->choice("trTarget");
    if (targetIndex <= 0 || targetIndex >= targetChoiceIds.size()) { error = "Choose the part to transform."; return args; }
    a->setProperty("operation", ops[op]); a->setProperty("target", targetChoiceIds[targetIndex]); a->setProperty("copy", form->toggle("trCopy"));
    const auto ref = [&](const char* key) -> juce::String { const int i = form->choice(key); return i > 0 && i < referenceChoiceIds.size() ? referenceChoiceIds[i] : juce::String(); };
    const auto need = [&](const char* key, const char* name) { const auto id = ref(key); if (id.isEmpty()) error = juce::String("Choose ") + name + "."; return id; };
    const auto number = [&](const char* key, const char* name, double& out) { if (!form->numberValid(key)) { error = juce::String(name) + " must be a number."; return false; } out = form->number(key); return true; };
    if (op == 0)
    {
        double x = 0, y = 0, z = 0; if (!number("trDx", "dX", x) || !number("trDy", "dY", y) || !number("trDz", "dZ", z)) return args;
        a->setProperty("displacement", juce::Array<juce::var> { x, y, z }); if (ref("trRef1").isNotEmpty()) a->setProperty("frame", ref("trRef1"));
    }
    else if (op == 1) { a->setProperty("toReference", need("trRef1", "the reference to move to")); if (ref("trRef2").isNotEmpty()) a->setProperty("fromReference", ref("trRef2")); }
    else if (op == 2) { double angle = 0; a->setProperty("axis", need("trRef1", "the rotation axis")); if (!number("trAngle", "Angle", angle)) return args; a->setProperty("angleDegrees", angle); }
    else if (op == 3) { a->setProperty("source", need("trRef1", "the source reference on the part")); a->setProperty("to", need("trRef2", "the target reference")); a->setProperty("flip", form->toggle("trFlip")); }
    else if (op == 4) a->setProperty("plane", need("trRef1", "the mirror plane"));
    else { double k = 1; a->setProperty("center", need("trRef1", "the scale centre")); if (!number("trFactor", "Scale factor", k)) return args; a->setProperty("factor", k); }
    return args;
}

void CadWorkspacePanel::acceptReferenceTransform()
{
    juce::String e; const auto args = readReferenceTransform(e);
    if (e.isNotEmpty()) { showError(e); return; }
    const auto reply = service->transformCommand(args);
    if (!(bool) reply["ok"]) { showError(reply["error"].toString()); return; }
    refreshFromDocument(true);
    const auto id = reply["id"].toString(); const auto op = args["operation"].toString();
    if (id.isNotEmpty()) selectFeature(id);
    setMessage(juce::String(op == "move" ? "Moved " : op == "rotate" ? "Rotated " : op == "align" ? "Aligned " : op == "mirror" ? "Mirrored " : "Scaled ") + featureName(args["target"].toString()) + ((bool) args["copy"] ? ": the original is unchanged and a copy was created. " : ". ") + "Undo reverts it.", 0);
}

void CadWorkspacePanel::pickReferenceForTransform(const juce::String& id)
{
    if (form == nullptr) return;
    const int index = referenceChoiceIds.indexOf(id); if (index < 0) return;
    auto* first = dynamic_cast<juce::ComboBox*>(form->control("trRef1")); auto* second = dynamic_cast<juce::ComboBox*>(form->control("trRef2"));
    const int op = form->choice("trOp"); const bool usesSecond = op == 1 || op == 3;
    juce::ComboBox* slot = first;
    if (first != nullptr && first->getSelectedItemIndex() > 0 && usesSecond && second != nullptr) slot = second;
    if (slot != nullptr) slot->setSelectedItemIndex(index, juce::sendNotificationSync);
    setMessage("Using reference " + featureName(id) + ".", 0);
}
