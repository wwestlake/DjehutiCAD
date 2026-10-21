#include "DjehutiAgent.h"
#include "ProtectedCredentialStore.h"
#include <ai_provider/OpenAiProvider.h>

namespace
{
juce::String setting(juce::ApplicationProperties& p, const char* name, const char* fallback)
{
    return p.getUserSettings()->getValue(name, fallback);
}
std::string credentialName(const juce::String& provider, const juce::String& profile)
{
    const auto raw = provider + "/" + profile;
    juce::String safe;
    for (int i = 0; i < raw.length(); ++i)
    {
        const auto c = raw[i];
        safe << (juce::String("\\/:*?\"<>| ").containsChar(c) ? '_' : c);
    }
    return "agent/" + safe.toStdString();
}
std::vector<ai_provider::ToolDefinition> tools()
{
    const auto emptyObject = R"({"type":"object","properties":{},"additionalProperties":false})";
    const auto dimensions = R"({"type":"object","properties":{"width":{"type":"number"},"depth":{"type":"number"},"height":{"type":"number"}},"required":["width","depth","height"],"additionalProperties":false})";
    const auto featureDimensions = R"({"type":"object","properties":{"id":{"type":"string"},"width":{"type":"number"},"depth":{"type":"number"},"height":{"type":"number"},"radius":{"type":"number"}},"required":["id"],"additionalProperties":false})";
    const auto cylinder = R"({"type":"object","properties":{"radius":{"type":"number"},"height":{"type":"number"}},"required":["radius","height"],"additionalProperties":false})";
    const auto placement = R"({"type":"object","properties":{"id":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"},"rx":{"type":"number"},"ry":{"type":"number"},"rz":{"type":"number"}},"required":["id","x","y","z"],"additionalProperties":false})";
    const auto booleanArgs = R"({"type":"object","properties":{"operation":{"type":"string","enum":["union","subtract","intersect"]},"first":{"type":"string"},"second":{"type":"string"}},"required":["operation","first","second"],"additionalProperties":false})";
    const auto sketch = R"({"type":"object","properties":{"plane":{"type":"string","enum":["XY","XZ","YZ"]},"offset":{"type":"number"}},"required":["plane"],"additionalProperties":false})";
    const auto sketchLine = R"({"type":"object","properties":{"sketch":{"type":"string"},"x1":{"type":"number"},"y1":{"type":"number"},"x2":{"type":"number"},"y2":{"type":"number"}},"required":["sketch","x1","y1","x2","y2"],"additionalProperties":false})";
    const auto sketchRectangle = R"({"type":"object","properties":{"sketch":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"},"width":{"type":"number"},"height":{"type":"number"}},"required":["sketch","x","y","width","height"],"additionalProperties":false})";
    const auto sketchCircle = R"({"type":"object","properties":{"sketch":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"},"radius":{"type":"number"}},"required":["sketch","x","y","radius"],"additionalProperties":false})";
    const auto sketchDimension = R"({"type":"object","properties":{"sketch":{"type":"string"},"entity":{"type":"string"},"value":{"type":"number"}},"required":["sketch","entity","value"],"additionalProperties":false})";
    const auto sketchPosition = R"({"type":"object","properties":{"sketch":{"type":"string"},"entity":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"}},"required":["sketch","entity","x","y"],"additionalProperties":false})";
    const auto sketchConstraint = R"({"type":"object","properties":{"sketch":{"type":"string"},"type":{"type":"string"},"first":{"type":"string"},"second":{"type":"string"},"value":{"type":"number"}},"required":["sketch","type","first"],"additionalProperties":false})";
    const auto extrude = R"({"type":"object","properties":{"sketch":{"type":"string"},"depth":{"type":"number"},"reverse":{"type":"boolean"}},"required":["sketch","depth"],"additionalProperties":false})";
    const auto instance = R"({"type":"object","properties":{"feature":{"type":"string"},"name":{"type":"string"}},"required":["feature"],"additionalProperties":false})";
    const auto joint = R"({"type":"object","properties":{"joint":{"type":"string"},"name":{"type":"string"},"type":{"type":"string","enum":["fixed","revolute","prismatic"]},"parent":{"type":"string"},"child":{"type":"string"},"origin":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"axis":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"reference":{"type":"number"},"lower":{"type":"number"},"upper":{"type":"number"}},"required":["type","child","origin","axis"],"additionalProperties":false})";
    const auto mapping = R"({"type":"object","properties":{"joint":{"type":"string"},"signal":{"type":"string"},"quantity":{"type":"string","enum":["angle","angular_velocity","position","linear_velocity"]},"unit":{"type":"string"},"scale":{"type":"number"},"offset":{"type":"number"},"initialCoordinate":{"type":"number"}},"required":["joint","signal","quantity","unit"],"additionalProperties":false})";
    const auto preview = R"({"type":"object","properties":{"joint":{"type":"string"},"value":{"type":"number"},"reset":{"type":"boolean"}},"additionalProperties":false})";
    const auto lifecycle = R"({"type":"object","properties":{"command":{"type":"string","enum":["play","start","pause","resume","stop","reset","step","retry"]},"stepMs":{"type":"number"}},"required":["command"],"additionalProperties":false})";
    const auto grounded = R"({"type":"object","properties":{"instance":{"type":"string"},"grounded":{"type":"boolean"}},"required":["instance","grounded"],"additionalProperties":false})";
    const auto instancePlacement = R"({"type":"object","properties":{"instance":{"type":"string"},"origin":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"rotationDeg":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["instance","origin","rotationDeg"],"additionalProperties":false})";
    const auto jointRole = R"({"type":"object","properties":{"joint":{"type":"string"},"role":{"type":"string","enum":["driver","dependent","locked"]},"lockValue":{"type":"number"},"enforceLimits":{"type":"boolean"}},"required":["joint","role"],"additionalProperties":false})";
    const auto coupling = R"({"type":"object","properties":{"name":{"type":"string"},"type":{"type":"string","enum":["gear","linear","rackPinion","screw"]},"jointA":{"type":"string"},"jointB":{"type":"string"},"ratio":{"type":"number"},"pitchRadius":{"type":"number"},"lead":{"type":"number"},"offset":{"type":"number"}},"required":["type","jointA","jointB"],"additionalProperties":false})";
    const auto removeCoupling = R"({"type":"object","properties":{"coupling":{"type":"string"}},"required":["coupling"],"additionalProperties":false})";
    const auto listen = R"({"type":"object","properties":{"listen":{"type":"boolean"}},"required":["listen"],"additionalProperties":false})";
    const auto faceList = R"({"type":"object","properties":{"feature":{"type":"string"}},"required":["feature"],"additionalProperties":false})";
    const auto exportArgs = R"({"type":"object","properties":{"format":{"type":"string","enum":["3mf","stl","step","glb"]},"path":{"type":"string"},"parts":{"type":"array","items":{"type":"string"}},"quality":{"type":"string","enum":["low","medium","high","standard","fine","custom"]},"chordal_mm":{"type":"number"},"angular_deg":{"type":"number"},"overwrite":{"type":"boolean"}},"required":["format","path"],"additionalProperties":false})";
    const auto faceSketch = R"({"type":"object","properties":{"feature":{"type":"string"},"face":{"type":"string"}},"required":["feature","face"],"additionalProperties":false})";
    // IK: one target description shared by create, update and solve (solve uses id plus desired/mode/enabled as unsaved overrides).
    const std::string ikTarget = R"({"type":"object","properties":{"id":{"type":"string"},"name":{"type":"string"},"instance":{"type":"string"},"referenceInstance":{"type":"string"},"mode":{"type":"string","enum":["pose","position","orientation"]},"enabled":{"type":"boolean"},"toolOrigin":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"toolRotationDeg":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"desiredOrigin":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"desiredRotationDeg":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"captureCurrent":{"type":"boolean"}},"additionalProperties":false})";
    const std::string ikTaskFields = R"("name":{"type":"string"},"adjustableJoints":{"type":"array","items":{"type":"string"}},"positionToleranceMm":{"type":"number"},"angleToleranceDeg":{"type":"number"},"targets":{"type":"array","items":)" + ikTarget + "}";
    static const std::string ikCreate = R"({"type":"object","properties":{)" + ikTaskFields + R"(},"required":["adjustableJoints","targets"],"additionalProperties":false})";
    static const std::string ikUpdate = R"({"type":"object","properties":{"task":{"type":"string"},)" + ikTaskFields + R"(},"required":["task"],"additionalProperties":false})";
    static const std::string ikSolve = R"({"type":"object","properties":{"task":{"type":"string"},)" + ikTaskFields + R"(},"additionalProperties":false})";
    const auto ikTaskOnly = R"({"type":"object","properties":{"task":{"type":"string"}},"required":["task"],"additionalProperties":false})";
    const auto ikOptionalTask = R"({"type":"object","properties":{"task":{"type":"string"}},"additionalProperties":false})";
    const auto cut = R"({"type":"object","properties":{"sketch":{"type":"string"},"target":{"type":"string"},"depth":{"type":"number"},"throughAll":{"type":"boolean"},"reverse":{"type":"boolean"}},"required":["sketch","target"],"additionalProperties":false})";
    // Physical materials: flat fields carry their units in the key name; "clear" makes listed properties unspecified.
    const auto materialFields = std::string(R"("name":{"type":"string"},"family":{"type":"string"},"specification":{"type":"string"},"condition":{"type":"string"},"source":{"type":"string"},"model":{"type":"string","enum":["isotropic"]},"densityKgPerM3":{"type":"number"},"youngsModulusMPa":{"type":"number"},"poissonsRatio":{"type":"number"},"yieldStrengthMPa":{"type":"number"},"note":{"type":"string"})");
    static const std::string materialCreate = R"({"type":"object","properties":{)" + materialFields + R"(},"required":["name","densityKgPerM3","youngsModulusMPa","poissonsRatio"],"additionalProperties":false})";
    static const std::string materialUpdate = R"({"type":"object","properties":{"material":{"type":"string"},)" + materialFields + R"(,"clear":{"type":"array","items":{"type":"string","enum":["yieldStrength","density","youngsModulus","poissonsRatio"]}}},"required":["material"],"additionalProperties":false})";
    const auto materialList = R"({"type":"object","properties":{"material":{"type":"string"},"feature":{"type":"string"}},"additionalProperties":false})";
    const auto materialDelete = R"({"type":"object","properties":{"material":{"type":"string"},"clearAssignments":{"type":"boolean"}},"required":["material"],"additionalProperties":false})";
    const auto materialAssign = R"({"type":"object","properties":{"feature":{"type":"string"},"material":{"type":"string"}},"required":["feature","material"],"additionalProperties":false})";
    const auto materialClear = R"({"type":"object","properties":{"feature":{"type":"string"}},"required":["feature"],"additionalProperties":false})";
    // Analysis cases: nested objects are described in the tool text; quantities are {"value": ..., "unit": "..."}.
    const auto analysisFields = std::string(R"("name":{"type":"string"},"description":{"type":"string"},"parts":{"type":"array","items":{"type":"object"}},"loads":{"type":"array","items":{"type":"object"}},"supports":{"type":"array","items":{"type":"object"}},"gravity":{"type":["object","null"]},"settings":{"type":"object"})");
    static const std::string analysisCreate = R"({"type":"object","properties":{)" + analysisFields + R"(},"required":["name"],"additionalProperties":false})";
    static const std::string analysisUpdate = R"({"type":"object","properties":{"case":{"type":"string"},)" + analysisFields + R"(},"required":["case"],"additionalProperties":false})";
    const auto analysisCaseOnly = R"({"type":"object","properties":{"case":{"type":"string"}},"required":["case"],"additionalProperties":false})";
    const auto analysisList = R"({"type":"object","properties":{"case":{"type":"string"}},"additionalProperties":false})";
    const auto sketchBezier = R"({"type":"object","properties":{"sketch":{"type":"string"},"x1":{"type":"number"},"y1":{"type":"number"},"cx1":{"type":"number"},"cy1":{"type":"number"},"cx2":{"type":"number"},"cy2":{"type":"number"},"x2":{"type":"number"},"y2":{"type":"number"},"startConnection":{"type":"string"},"endConnection":{"type":"string"}},"required":["sketch","x1","y1","cx1","cy1","cx2","cy2","x2","y2"],"additionalProperties":false})";
    const auto sketchMovePoint = R"({"type":"object","properties":{"sketch":{"type":"string"},"point":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"}},"required":["sketch","point","x","y"],"additionalProperties":false})";
    const auto sketchInsertVertex = R"({"type":"object","properties":{"sketch":{"type":"string"},"line":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"}},"required":["sketch","line","x","y"],"additionalProperties":false})";
    const auto sketchSmooth = R"({"type":"object","properties":{"sketch":{"type":"string"},"curve":{"type":"string"},"end":{"type":"string","enum":["p1","p2"]}},"required":["sketch","curve","end"],"additionalProperties":false})";
    const auto fillet = R"({"type":"object","properties":{"target":{"type":"string"},"edges":{"type":"array","items":{"type":"integer"}},"radius":{"type":"number"}},"required":["target","edges","radius"],"additionalProperties":false})";
    const auto stockTool = R"({"type":"object","properties":{"action":{"type":"string","enum":["list","create","update","duplicate","delete","setDefault"]},"stock":{"type":"string"},"definition":{"type":"object","description":"Stock fields: name, material (document material id), thicknessMm (substrate), finishedThicknessMm, sheetWidthMm, sheetLengthMm, gauge, gaugeStandard, finish, coating, note, source, insideRadiusMm, kFactor, formingMethod (kFactor), qualification (unqualified|reference|shopQualified), formingSource. Optional numeric fields may be null to clear. All lengths mm."}},"additionalProperties":false})";
    const auto catalogTool = R"({"type":"object","properties":{"action":{"type":"string","enum":["list","reload","createMaterial","updateMaterial","duplicateMaterial","deleteMaterial","createStock","updateStock","duplicateStock","deleteStock","useMaterial","useStock","importFile","exportFile"]},"material":{"type":"string"},"stock":{"type":"string"},"feature":{"type":"string"},"setDefault":{"type":"boolean"},"path":{"type":"string"},"overwrite":{"type":"boolean"},"definition":{"type":"object","description":"Flat material fields as cad_material_create, or stock fields as cad_stock. Material references are catalog IDs. clear:[property keys] clears optional material properties. Optional stock numeric fields may be null."}},"additionalProperties":false})";
    return {
        { "open_cad_workspace", "Open the CAD workspace.", emptyObject },
        { "open_frust_editor", "Open the FRust editor.", emptyObject },
        { "open_node_editor", "Open the node editor.", emptyObject },
        { "open_3d_views", "Open the 3D views workspace and add a view.", emptyObject },
        { "cad_create_box", "Create a dimensioned box in millimeters and return its managed feature id.", dimensions },
        { "cad_create_cylinder", "Create a cylinder in millimeters and return its managed feature id.", cylinder },
        { "cad_create_sketch", "Create an editable XY, XZ, or YZ planar sketch in millimeters.", sketch },
        { "cad_sketch_line", "Add a line entity to a managed sketch.", sketchLine },
        { "cad_sketch_rectangle", "Add a connected dimensioned rectangle to a managed sketch.", sketchRectangle },
        { "cad_sketch_circle", "Add a dimensioned circle to a managed sketch.", sketchCircle },
        { "cad_sketch_dimension", "Change a sketch line length or circle radius in millimeters.", sketchDimension },
        { "cad_sketch_position", "Move a sketch entity start point or circle center in millimeters.", sketchPosition },
        { "cad_sketch_constraint", "Add a supported sketch constraint: coincident, horizontal, vertical, fixed, length, distance, radius, or diameter.", sketchConstraint },
        { "cad_extrude", "Extrude a closed sketch profile into a real solid.", extrude },
        { "cad_cut", "Cut a closed sketch profile from an explicit solid target; support finite depth or through-all.", cut },
        { "cad_fillet", "Apply a constant-radius fillet in millimeters to selected edge indices of a target solid body.", fillet },
        { "cad_set_dimensions", "Change a managed CAD feature's dimensions in millimeters.", featureDimensions },
        { "cad_set_placement", "Set a feature's millimeter position and optional Euler rotations in degrees.", placement },
        { "cad_boolean", "Create a union, subtract, or intersect feature from two distinct solid body ids. For subtract, first is the target body to keep and second is the cutting tool. Both sources are hidden and stay in the feature history; undo restores them.", booleanArgs },
        { "cad_undo", "Undo the most recent CAD document command.", emptyObject },
        { "cad_create_instance", "Create a movable assembly instance of a solid body feature id. Instances keep their own stable id; the first instance appears where the body is modelled.", instance },
        { "cad_create_joint", "Create a joint between assembly instances. parent is an instance id or 'world'; origin is the joint origin in world millimetres and axis the world direction of the joint +Z motion axis at the reference pose. Revolute coordinates are radians, prismatic metres; lower/upper set engineering limits.", joint },
        { "cad_update_joint", "Update an existing joint (field 'joint') with the same fields as cad_create_joint; the child keeps its current reference position.", joint },
        { "cad_set_joint_mapping", "Map a DEWB signal to a joint in the connection profile: quantity angle/angular_velocity (revolute) or position/linear_velocity (prismatic), unit e.g. rad, deg, rev, rad/s, rpm, m, mm, m/s; scale is the gear ratio, offset applies to state signals, initialCoordinate to velocity signals.", mapping },
        { "cad_motion_preview", "Preview a driver joint coordinate (radians or metres): the mechanism solver computes the dependent joints; the document is not changed. reset=true returns all drivers to their reference. Not allowed while DEWB drives the joints.", preview },
        { "cad_set_instance_grounded", "Ground (fix to the world at its placement) or unground an assembly instance. Mechanisms are solved relative to the world and grounded instances; nothing is grounded implicitly.", grounded },
        { "cad_set_instance_placement", "Set an instance's reference placement: origin in world millimetres and orientation as extrinsic rotations about world X, Y, Z in degrees. Allowed for grounded instances and instances without joints.", instancePlacement },
        { "cad_set_joint_role", "Set a joint's coordinate role in the mechanism: driver (prescribed input), dependent (solved from the constraints) or locked (held at lockValue, rad or m). enforceLimits makes its limits hard solver bounds. Mapped DEWB joints must stay drivers.", jointRole },
        { "cad_create_coupling", "Couple two joints: (qB - refB) = k (qA - refA) + offset. gear: two revolute joints, k = signed ratio, offset = phase in rad; linear: two prismatic joints, k = ratio, offset in m; rackPinion: A revolute, B prismatic, pitchRadius in m; screw: A revolute, B prismatic, lead in m per revolution. Usually make joint B dependent.", coupling },
        { "cad_remove_coupling", "Remove a joint coupling by id.", removeCoupling },
        { "cad_motion_lifecycle", "Ask the connected DEWB simulator to change its run state: start, pause, resume, stop (end the run, keep the final state), reset (new timeline at the initial state), step (advance stepMs milliseconds of simulation time and stay paused), play (start or resume by the confirmed state), or retry (resend the unconfirmed request with the same id). DEWB must advertise lifecycle control; check cad_motion_status for the confirmed state and result.", lifecycle },
        { "cad_motion_listen", "Start (listen=true) or stop the DCAD motion endpoint that DEWB connects to.", listen },
        { "cad_motion_status", "Return the motion link and mechanism status JSON: link state, pipe path, DEWB simulation, accepted profile revision, timeline, received and last posed sequence, signals, problems, the mechanical interface with instance and joint ids, and 'mechanism' (solve status, residuals, free coordinates, problems and every solved joint coordinate).", emptyObject },
        { "cad_ik_create_task", "Create an inverse-kinematics task (saved, undoable). adjustableJoints: driver joint ids the solve may move (others keep their values; dependent joints follow the mechanism). targets: instance carrying the endpoint, toolOrigin/toolRotationDeg = endpoint frame on that instance (mm, extrinsic XYZ degrees), referenceInstance ('world' or an instance id), mode pose|position|orientation, desiredOrigin/desiredRotationDeg in the reference frame, or captureCurrent=true to copy the shown pose. Returns the task id and stored definition.", ikCreate.c_str() },
        { "cad_ik_update_task", "Update a saved IK task ('task' id); fields given replace the stored ones, targets with a known id keep fields you omit. Undoable.", ikUpdate.c_str() },
        { "cad_ik_remove_task", "Delete a saved IK task. Undoable.", ikTaskOnly },
        { "cad_ik_inspect", "Without 'task': list the saved IK tasks. With 'task': its definition and its last solve, plus the target errors of the pose currently shown.", ikOptionalTask },
        { "cad_ik_solve", "Solve an IK task now and return the structured result: published (true only when every mechanism constraint, bound and target tolerance holds), status (solved, underconstrained, singular, targetNotReached, limitRestricted, invalidInput, notConverged, cancelled), message, per-target desired and achieved poses with position (mm) and angle errors, and limited joints. Give 'task' (optionally with targets [{id, desiredOrigin, desiredRotationDeg, mode, enabled}] as unsaved overrides) or a complete ad hoc definition. On failure the last valid pose stays. Not allowed while DEWB drives the mechanism. A failed local search does not prove the target is unreachable.", ikSolve.c_str() },
        { "cad_ik_status", "Return the IK status: runtime authority (preview or DEWB simulation), the last solve result for the task and the errors of the pose currently shown.", ikOptionalTask },
        { "cad_ik_reset_preview", "Return every driver to its reference coordinate (the same reset as the joint preview). The document is not changed.", emptyObject },
        { "cad_ui", "Operate the visible CAD workspace one step at a time. Read state first: it supplies task fields, buttons, commands and selected ids. Actions: state, command (command name), set (field and value), click (button id, including apply/cancel), select (id; optional entity/second), sketch (phase begin/update/commit/cancel, x/y in sketch mm), camera (yawDeg/pitchDeg/distance/panX/panY; view -1 for model or zero-based 3D view), document (operation new/open/save, path, explicit discard/overwrite). Drawing requires an active sketch tool; preview can be held between requests. Use the same task fields and Apply as a human for a narrated demonstration. No automatic playback. Returns actual UI and document state.", R"({"type":"object","properties":{"action":{"type":"string"},"command":{"type":"string"},"field":{"type":"string"},"value":{},"button":{"type":"string"},"id":{"type":"string"},"entity":{"type":"string"},"second":{"type":"boolean"},"phase":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"},"yawDeg":{"type":"number"},"pitchDeg":{"type":"number"},"distance":{"type":"number"},"panX":{"type":"number"},"panY":{"type":"number"},"view":{"type":"integer"},"operation":{"type":"string"},"path":{"type":"string"},"discard":{"type":"boolean"},"overwrite":{"type":"boolean"}},"additionalProperties":false})" },
        { "cad_workspace", "Inspect or change presentation state without editing the engineering document. action: state, tool, guides, activate, float, dock or ui. The ui action takes a nested ui object with cad_ui arguments (shared by FRust and the CAD Workspace node). workspace: cad, views, frust, nodes, agent, fixtures or toolbox. tool: pointer, boxSelect, circleSelect, bodyMode, faceMode, edgeMode, sketchSelect, line, rectangle or circle (sketch tools require an active sketch). guides: floor/world/local/joints booleans; valid for cad or views. view: zero-based 3D view index or -1 for all. Floor is fixed world XY; local planes use the selected part's actual origin; joint frames and limits come from the solved mechanism. Box left-to-right encloses; right-to-left and circle cross projected geometry, including obscured items. Returns guide settings, active tools and document revision.", R"({"type":"object","properties":{"action":{"type":"string","enum":["state","tool","guides","activate","float","dock","ui"]},"ui":{"type":"object"},"workspace":{"type":"string","enum":["cad","views","frust","nodes","agent","toolbox","fixtures"]},"tool":{"type":"string"},"view":{"type":"integer"},"guides":{"type":"object","properties":{"floor":{"type":"boolean"},"world":{"type":"boolean"},"local":{"type":"boolean"},"joints":{"type":"boolean"}},"additionalProperties":false}},"additionalProperties":false})" },
        { "cad_export", "Export manufacturing and game-ready mesh files from evaluated CAD solids (a read operation: the document is not changed). format: 3mf (preferred for 3-D printing: mm units, one named object per part with placement), stl (binary, mm), step (AP214 B-rep solids in mm with part names and assembly placements) or glb (glTF 2.0 binary: metre units, indexed triangles, normals, separate named nodes, assembly hierarchy and joint pivot frames preserved). path: absolute output file. parts: body or instance ids; omit for every visible part of the modeled assembly. Placements are the authored reference configuration, never a simulation or IK pose. quality: low (0.2 mm, 28 deg), medium/standard (0.05 mm, 15 deg), high/fine (0.01 mm, 5 deg), custom = chordal_mm and angular_deg. overwrite must be true to replace existing files. Solids and meshes are checked; a failed or cancelled export leaves existing files unchanged. Returns files, overall and per-part size in mm, triangle counts, volumes and warnings.", exportArgs },
        { "cad_list_faces", "List the faces of a solid feature with their persistent names (e.g. box-1/top, extrude-5/end, extrude-5/side:line-7), whether each is planar and can support a sketch, its outward normal, centre (mm) and area. Names come from the model's construction history and survive parameter edits; an empty name means the face cannot be referenced.", faceList },
        { "cad_create_face_sketch", "Create a sketch attached to a planar face: feature is the solid feature id, face the persistent face name from cad_list_faces. The sketch's U/V frame lies in the face (normal pointing out of the body); it follows the face when the body's parameters change, and an edit that removes or splits that face is rejected rather than reattaching the sketch. Cuts from a face sketch go into the body unless reversed. Returns the sketch id.", faceSketch },
        { "cad_redo", "Redo the most recently undone CAD document command.", emptyObject },
        { "cad_sketch_bezier", "Add a cubic Bezier curve to a sketch (sketch-plane mm): start (x1,y1), control points (cx1,cy1) and (cx2,cy2), end (x2,y2). Optional startConnection/endConnection join an end to an existing endpoint such as line-5:p2. It chains with lines into closed profiles for Extrude and Cut.", sketchBezier },
        { "cad_sketch_move_point", "Move a sketch point: point is <entity>:p1, :p2 (endpoints; everything joined to that vertex moves with it, a rectangle corner reshapes the rectangle) or :c1, :c2 (curve handles). Refused if the sketch's constraints cannot hold. One undo step.", sketchMovePoint },
        { "cad_sketch_insert_vertex", "Insert a vertex into a sketch line at the point of the line nearest (x, y): the line ends there and a new joined line continues to the old end. The split line's own orientation/length constraints are removed and reported.", sketchInsertVertex },
        { "cad_sketch_smooth", "Make the join at one end (p1 or p2) of a Bezier curve tangent to the segment connected there, by turning that end's handle onto its direction (keeps the handle length).", sketchSmooth },
        { "cad_material_list", "List the document's physical materials and part assignments. With material: one definition and the parts using it. With feature: that part's assignment (null if none). Values carry explicit units (density kg/m^3, Young's modulus MPa, Poisson's ratio 1, yield strength MPa); absent properties are unspecified.", materialList },
        { "cad_material_create", "Create a physical material (isotropic: density kg/m^3, Young's modulus MPa and Poisson's ratio required; yield strength MPa optional). Physical materials are separate from appearance colours. Returns its stable id (mat-N).", materialCreate.c_str() },
        { "cad_material_update", "Edit a material definition; its id stays the same and its revision advances. Omitted fields keep their values; clear lists properties to make unspecified.", materialUpdate.c_str() },
        { "cad_material_delete", "Delete a material. Refused while parts use it unless clearAssignments is true (which also removes those assignments).", materialDelete },
        { "cad_material_assign", "Assign a material to a solid part (feature id); replaces any previous assignment. Does not change geometry.", materialAssign },
        { "cad_catalog", "Manage the application engineering catalog. Bundled references are read-only; duplicate to customize. Catalog edits save user JSON, outside document undo. useMaterial imports a new snapshot and optionally assigns feature in one CAD transaction. useStock imports stock plus material, atomically remapping IDs; setDefault defaults true. Repeated imports create independent copies. importFile merges portable catalogs without overwriting conflicting IDs. reload reads external changes.", catalogTool },
        { "cad_stock", "Manage document-local sheet stock and default stock (empty stock clears default). create/update/duplicate use definition. Edits are shared and undoable, revisions advance. Deleting the current default is refused. No sheet geometry is created.", stockTool },
        { "cad_material_duplicate", "Duplicate a document material with a new identity. Optional material fields override its copied properties.", materialUpdate.c_str() },
        { "cad_material_clear", "Remove a part's material assignment.", materialClear },
        { "cad_analysis_list", "List structural analysis cases (definitions only, no results). Each case reports status valid/invalid and problems: references to parts, instances or faces that no longer exist are reported, never re-attached. With case: one case.", analysisList },
        { "cad_analysis_create", "Create a structural analysis case (a problem definition; nothing is solved). parts: [{feature} or {instance}]. loads: [{type force|moment, target {feature|instance, face?}, frame \"world\" or an instance id, direction [x,y,z], magnitude {value, unit N|kN|lbf or N*m|N*mm|kN*m}, point? {value [x,y,z], unit mm|m|in}}]. supports: [{target, frame, kind fixed|pinned or fixed [ux,uy,uz,rx,ry,rz]}]. gravity?: {acceleration {value [x,y,z], unit m/s^2|g0}}. settings?: {analysisType linearStatic, options {}}. Stored in SI (N, N*m, m/s^2), points in mm. Faces use persistent tokens from cad_list_faces.", analysisCreate.c_str() },
        { "cad_analysis_update", "Edit an analysis case: given fields replace the stored ones (whole lists), others are kept; gravity null removes gravity. The id stays, the revision advances. References must resolve.", analysisUpdate.c_str() },
        { "cad_analysis_delete", "Delete an analysis case by id.", analysisCaseOnly },
        { "cad_fixture", "Manage independent protected-source engineering fixtures. create/open/list/duplicate/rename/save/close/delete manage .djfixture documents; fixture is the stable fixture ID. addSource(path) pins a saved CAD file by SHA-256; choices lists source features and mechanics. addModel definition {source,kind part|assembly,feature?,name?,placement? {x,y,z in mm,rx,ry,rz in degrees}} creates an occurrence. updateModel(model,definition) changes fixture placement only. substituteMaterial(model,material) uses a complete material record (from cad_catalog or cad_material_list); null restores source assignments; an assembly substitution applies to every component in that occurrence. upsertEquipment definition {id?,kind fixed|support|force|moment|pressure|distributedForce|point|displacement|reaction|stress|strain|safetyFactor|jointLock,target occurrence ID,component? source assembly instance ID,face? persistent token,pointMm? [x,y,z],anchor? point|faceCentre,direction? unit [x,y,z],magnitude?,unit?,fixed? [ux,uy,uz,rx,ry,rz],joint?,coordinate? rad or m}. Equipment points/directions are source-document coordinates, carried with the occurrence. Distributed loads require a face. settings definition {method,gravity? [m/s^2],options? {beamAxis X|Y|Z,bendingAxis X|Y|Z,visualizationScale >0}}. Axes are source-feature local; load directions are source-document coordinates. inspect/validate return current configuration, problems, method availability and stamped results. run executes geometricProperties, massProperties, source-assembly kinematics, or linearStatic for one rectangular Box cantilever with one fully fixed end and one transverse free-end point force, both at end-face centres. linearStatic computes neutral-axis displacement, reactions, bending stress/strain and a scaled deformation curve; no gravity, torsion, shear deformation, contact or general FEA. Supported displacement probes lie on the neutral axis; stress/strain are longitudinal at the configured fibre; reaction probes are at the clamp centre. Other measurements remain unsupported. Face points are validated against trimmed geometry. Explicit faceCentre anchors remap only to one surviving planar face; off-face/ambiguous attachments require manual correction and block calculation. acceptSource(source) explicitly accepts changed source revisions; relocateSource also needs path and retains source model identity. removeEquipment/removeModel/removeSource use item; dependent removals are rejected. undo/redo act only on fixture configuration. Existing file replacements need overwrite; close with unsaved changes needs discard; delete requires confirm. Source CAD is never edited.", R"({"type":"object","properties":{"action":{"type":"string","enum":["list","create","open","duplicate","rename","save","close","delete","addSource","acceptSource","relocateSource","choices","addModel","updateModel","substituteMaterial","upsertEquipment","removeEquipment","removeModel","removeSource","settings","inspect","validate","run","undo","redo"]},"fixture":{"type":"string"},"name":{"type":"string"},"path":{"type":"string"},"source":{"type":"string"},"model":{"type":"string"},"item":{"type":"string"},"definition":{"type":"object"},"material":{"type":["object","null"]},"overwrite":{"type":"boolean"},"discard":{"type":"boolean"},"confirm":{"type":"boolean"}},"additionalProperties":false})" }
    };
}
}

juce::var DjehutiAgentPanel::toolCatalogue()
{
    juce::Array<juce::var> result;
    for (const auto& tool : tools())
    {
        auto* item = new juce::DynamicObject(); item->setProperty("name", juce::String(tool.name));
        item->setProperty("description", juce::String(tool.description));
        item->setProperty("parameters", juce::JSON::parse(juce::String(tool.parametersJson))); result.add(item);
    }
    return result;
}

DjehutiAgentPanel::DjehutiAgentPanel(juce::ApplicationProperties& p,
                                     std::function<std::string(const std::string&, const std::string&, std::shared_ptr<DjehutiAgentRequestState>)> dispatch)
    : properties(p), dispatchTool(std::move(dispatch))
{
        history.push_back({ "system", "You are the DjehutiCAD engineering assistant. Be concise and only claim operations supported by the registered tools and their actual results. The OCAF-backed document supports origin-plane and planar-face sketches, dimensional constraints, extrude/cut, solid booleans, assembly instances, joints and mechanism couplings. Local forward/inverse kinematics and DEWB motion-link controls are implemented; this is kinematics and visualization, not force or contact simulation. Manufacturing export supports 3MF, binary STL and STEP in the authored reference configuration. Workspace menus, the dockable Toolbox and per-view reference guides are presentation state: use cad_workspace to inspect or change them without editing the engineering document. FRust and nodes use the same model commands. Failed local IK does not prove a target unreachable. Treat disconnected or unverified external simulation state honestly. Stay within the user's requested task." });
    transcript.setMultiLine(true); transcript.setReadOnly(true); transcript.setScrollbarsShown(true); transcript.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff10151a)); transcript.setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee));
    input.setMultiLine(false); send.onClick = [this] { sendMessage(); }; cancelButton.onClick = [this] { if (request != nullptr) request->cancelled = true; if (worker != nullptr && worker->joinable()) worker->detach(); worker.reset(); request.reset(); setBusy(false); appendTranscript("Agent", "Request cancelled."); };
    addAndMakeVisible(transcript); addAndMakeVisible(input); addAndMakeVisible(send); addAndMakeVisible(cancelButton);
    setBusy(false); appendTranscript("DjehutiCAD Agent", "BYOK agent ready. Configure a provider key in Settings > Agent Settings.");
}
DjehutiAgentPanel::~DjehutiAgentPanel()
{
    if (request != nullptr) { request->cancelled = true; request->alive = false; }
    if (worker != nullptr && worker->joinable()) worker->detach();
    worker.reset(); request.reset();
}
void DjehutiAgentPanel::setBusy(bool value) { busy = value; send.setEnabled(!value); cancelButton.setEnabled(value); input.setEnabled(!value); }
void DjehutiAgentPanel::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff11161b)); g.setColour(juce::Colour(0xffdce9ee)); g.setFont(juce::Font(20.0f, juce::Font::bold)); g.drawText("DjehutiCAD Agent", 24, 16, getWidth() - 48, 30, juce::Justification::left); }
void DjehutiAgentPanel::resized() { auto area = getLocalBounds().reduced(24); area.removeFromTop(42); auto bottom = area.removeFromBottom(34); cancelButton.setBounds(bottom.removeFromRight(90)); send.setBounds(bottom.removeFromRight(80)); input.setBounds(bottom.reduced(4, 2)); transcript.setBounds(area); }
void DjehutiAgentPanel::appendTranscript(const juce::String& speaker, const juce::String& text) { transcript.moveCaretToEnd(); transcript.insertTextAtCaret(speaker + ": " + text + "\n\n"); }

void DjehutiAgentPanel::sendMessage()
{
    const auto text = input.getText().trim(); if (text.isEmpty() || busy) return;
    const auto providerName = setting(properties, "agentProvider", "openai"); const auto profile = setting(properties, "agentProfile", "DjehutiCAD"); const auto model = setting(properties, "agentModel", "gpt-4o-mini");
    std::string key, error; if (!DjehutiCredentials::read(credentialName(providerName, profile), key, error)) { appendTranscript("Agent", juce::String(error)); return; }
    input.clear(); appendTranscript("You", text); history.push_back({ "user", text.toStdString() }); const auto messages = history; const auto dispatch = dispatchTool; const auto state = std::make_shared<DjehutiAgentRequestState>(); request = state; setBusy(true);
    const juce::Component::SafePointer<DjehutiAgentPanel> safe(this);
    worker = std::make_unique<std::thread>([safe, state, messages, key = std::move(key), model = model.toStdString(), dispatch] {
        ai_provider::ChatResponse response; auto round = messages;
        ai_provider::OpenAiProvider api(key, model);
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            if (state->cancelled) return;
            response = api.sendChat(round, tools());
            if (state->cancelled || !state->alive) return;
            if (!response.ok || response.toolCalls.empty()) break;
            round.push_back({ "assistant", response.content, response.toolCalls });
            for (const auto& call : response.toolCalls)
            {
                if (state->cancelled || !state->alive) return;
                const auto result = dispatch ? dispatch(call.name, call.argumentsJson, state) : "No workspace dispatcher is available.";
                if (state->cancelled || !state->alive) return;
                round.push_back({ "tool", result, {}, call.id });
            }
        }
        juce::MessageManager::callAsync([safe, state, response] {
            if (safe == nullptr || !state->alive || state->cancelled) return;
            safe->worker->detach(); safe->worker.reset(); safe->request.reset(); safe->setBusy(false);
            if (response.ok) { safe->history.push_back({ "assistant", response.content }); safe->appendTranscript("Agent", juce::String(response.content)); }
            else safe->appendTranscript("Agent", juce::String(response.errorMessage));
        });
    });
}

DjehutiAgentSettings::DjehutiAgentSettings(juce::ApplicationProperties& p, std::function<void()> saved) : properties(p), onSaved(std::move(saved))
{
    provider.addItem("OpenAI", 1); provider.setSelectedId(1); profile.setText(setting(p, "agentProfile", "DjehutiCAD")); model.setText(setting(p, "agentModel", "gpt-4o-mini")); key.setPasswordCharacter('*'); key.setTextToShowWhenEmpty("Enter a new key; existing key is retained when blank", juce::Colours::grey);
    providerLabel.setText("Provider", juce::dontSendNotification); profileLabel.setText("Profile", juce::dontSendNotification); modelLabel.setText("Model", juce::dontSendNotification); keyLabel.setText("API key", juce::dontSendNotification); status.setText("Keys use Windows user-scoped DPAPI and are never written to the settings file.", juce::dontSendNotification);
    saveButton.onClick = [this] { save(); }; removeButton.onClick = [this] { std::string e; DjehutiCredentials::remove(credentialName("openai", profile.getText().trim()), e); status.setText(e.empty() ? "Protected key removed." : juce::String(e), juce::dontSendNotification); }; cancelButton.onClick = [this] { if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->closeButtonPressed(); };
    addAndMakeVisible(providerLabel); addAndMakeVisible(profileLabel); addAndMakeVisible(modelLabel); addAndMakeVisible(keyLabel); addAndMakeVisible(provider); addAndMakeVisible(profile); addAndMakeVisible(model); addAndMakeVisible(key); addAndMakeVisible(status); addAndMakeVisible(saveButton); addAndMakeVisible(removeButton); addAndMakeVisible(cancelButton);
}
void DjehutiAgentSettings::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff171b20)); }
void DjehutiAgentSettings::resized() { auto a = getLocalBounds().reduced(24); auto row = [&](juce::Label& l, juce::Component& c) { l.setBounds(a.removeFromTop(28)); c.setBounds(a.removeFromTop(32)); a.removeFromTop(10); }; row(providerLabel, provider); row(profileLabel, profile); row(modelLabel, model); row(keyLabel, key); status.setBounds(a.removeFromTop(42)); auto b = a.removeFromTop(34); cancelButton.setBounds(b.removeFromRight(80)); removeButton.setBounds(b.removeFromRight(110).reduced(4, 0)); saveButton.setBounds(b.removeFromRight(80).reduced(4, 0)); }
void DjehutiAgentSettings::save()
{
    const auto profileName = profile.getText().trim(); const auto value = key.getText().trim(); std::string error; if (!value.isEmpty() && !DjehutiCredentials::write(credentialName("openai", profileName), value.toStdString(), error)) { status.setText(error, juce::dontSendNotification); return; }
    auto* s = properties.getUserSettings(); s->setValue("agentProvider", "openai"); s->setValue("agentProfile", profileName); s->setValue("agentModel", model.getText().trim()); s->saveIfNeeded(); status.setText("Agent settings saved.", juce::dontSendNotification); if (onSaved) onSaved();
}
