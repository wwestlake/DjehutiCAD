# Sketch tools and entity management

Double-click a sketch to edit it. Its compact vertical toolbox appears inside
the CAD drawing area. The existing Sketch menu continues to offer the same
commands: Select, Line, Rectangle, Circle, Curve, Constrain, Fit and Finish.
The toolbox also includes Delete selected geometry. Hover over an icon for its
description.

Use **Float** to place the toolbox over the sketch, then drag its dotted header.
Use **Dock**, or drop its header near the left edge, to reserve space alongside
the drawing. **View > Sketch toolbox** shows/hides it; **View > Dock sketch
toolbox** changes docking. Position, docking and visibility are workspace
preferences remembered across application sessions. A hidden toolbox stays
hidden when another sketch is opened; show it again from View.

Completed geometry is selected automatically and immediately opens the existing
properties editor. A rectangle selects its first edge. Drawing tools stay active
for repeated construction, such as concentric circles. Use the pointer icon,
Sketch > Select, the properties panel's **Return to Select**, or Escape to stop
drawing.

Escape works in stages:

1. Cancel an unfinished drawing gesture or point drag, keeping completed geometry.
2. If a drawing tool is active, return to Select while keeping the current entity.
3. In Select, clear the entity selection.

Expand **Sketches > Sketch 1** to see all its actual entities, including off-screen
and degenerate legacy geometry. Rows show type and stable number; tooltips show
the full identifier. Selecting a row opens that sketch in Select mode and uses
the same properties editor as canvas selection. Canvas selection highlights the
matching row. Shift-click selects a second entity, matching the existing sketch
constraint workflow.

Delete selected geometry with **Delete**, the toolbox's trash icon, the
properties button, Sketch > Delete selected entities, or a tree/canvas context
menu. With two entities selected, deletion removes both in one transaction.
Attached constraints are removed too. Removing one rectangle edge leaves its
surviving edges as ordinary constrained lines. Undo/Redo restores/removes the
geometry, identifiers and constraints together. Delete inside a coordinate text
field edits text. Delete during an unfinished canvas gesture first cancels that
gesture. If deletion would invalidate a dependent solid, the existing dependency
rebuild rejects it and rolls back the entire change; remove or adjust that
dependency first.

New circles require finite centre coordinates and a radius of at least
**0.000001 mm**. Pointer gestures shorter than three pixels are cancelled with
feedback, preventing incidental clicks from creating geometry. Numeric creation
uses the geometric tolerance independently of zoom. Existing smaller/zero-radius
circles remain in the tree, with an amber label and explanatory tooltip, and can
be selected and deleted there.

Circle numbers are stable identifiers from the document's shared allocator,
which also allocates features, rectangle groups and constraints. **Circle 6 does
not mean six circles currently exist.** Deletions and other allocations can
leave normal gaps. The tree lists the entities stored in the document.

Automation uses the existing `cad_ui` path:

```json
{"action":"select","id":"sketch-1","entity":"circle-2"}
{"action":"command","command":"deleteSketchEntities"}
{"action":"command","command":"undo"}
{"action":"sketchToolbox","value":{"visible":true,"docked":false,"x":120,"y":40}}
```

Without `id`, entity selection refers to the active sketch. UI state includes
`selectedEntity`, `secondEntity`, `sketchToolbox`, and each entity's `treeName`
and `treeSelected`. `sketchToolbox` without `value` inspects its state. The
`sketchToolbox` and `dockSketchToolbox` commands toggle visibility/docking through
the same handlers as the View menu. These presentation operations do not change
the engineering document.

`--cad-sketch-usability-check` exercises the circle workflow, shared GUI callbacks,
deletion/constraints/Undo/Redo, legacy entities, toolbox layout and native/JSON
roundtrips. It writes `build/cad-sketch-usability-check.result.json` and a component
render under `build/sketch-usability-check/`. Component callbacks and component
renders are programmatic GUI verification, distinct from native pointer input.
