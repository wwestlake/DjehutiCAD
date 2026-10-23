# Sketch Editor usability completion

October 23, 2026. Scope: sketch interaction and entity management in DjehutiCAD.

## Delivered

- Compact vertical icon toolbox with descriptive tooltips, a draggable header,
  floating overlay, left docking, hide control and View-menu visibility/docking.
  Preferences survive application restart; existing Sketch menus remain intact.
- Automatic selection and immediate properties after creating a circle, line,
  curve or rectangle (first edge). Drawing tools remain active for repeated work;
  Select is available in the toolbox, menu and properties panel.
- Staged Escape: cancel an unfinished gesture, return an active drawing tool to
  Select, then clear selection. Completed geometry is retained.
- Expandable sketch entity rows sourced directly from document entities,
  including off-screen and degenerate legacy geometry. Canvas/tree selection
  share the existing properties editor. Shift-click retains the existing
  two-entity selection behavior, and collapsed branches survive document refresh.
- Entity deletion through Delete, tree/canvas context menus, toolbox, properties
  and Sketch menu. Geometry and attached constraints are removed in one OCAF
  transaction; tree/properties update immediately. Undo/Redo restores stable
  identifiers and constraints. Delete in text fields remains text editing.
- Finite-coordinate validation and a 0.000001 mm minimum circle radius for new
  circles and circle dimension changes. Short pointer gestures cancel with
  feedback. Existing degenerate entities remain discoverable and deletable.

The implementation reuses CadCommandService, the existing OCAF `mutate`
transaction/rebuild path, sketch solver, TaskForm properties, WorkspaceMenu,
JUCE ComponentDragger and workspace preference persistence. Native and JSON
document formats are unchanged. Automation calls the same GUI handlers through
`cad_ui`; no separate sketch model or command path was introduced.

## Findings

Creation previously discarded the returned entity identifier, leaving the
properties editor unselected. Sketch rows had no children, and deletion was
available for whole features rather than individual entities.

Exact zero-radius circles were already rejected by the model. Positive radii
arbitrarily close to zero were accepted, and very short pointer gestures were
silently discarded. These paths now have explicit tolerance/feedback. This
review cannot reconstruct which clicks occurred in the earlier hands-on session.

Numbers come from a shared allocator for features, entities, rectangle groups
and constraints. Circle 6 can therefore coexist with only three actual circles;
identifier gaps do not establish extra geometry or corruption. The tree now
shows the actual stored entities instead of requiring that inference.

## Validation

| Check group | Result |
|---|---|
| Debug delivery build | Passed |
| New sketch usability checks | 36/36 |
| Existing sketch-edit checks | 20/20 |
| Existing workspace checks | 20/20 |
| Existing modeling regression | 83/83 |
| Live GUI-control/API workflow | 18/18 |
| Actual application restart and reopen | 4/4 |

**181 checks passed.** The requested XY/concentric-circle workflow was exercised:
50 mm creation, automatic properties, tree/canvas selection, radius editing,
deletion, Undo/Redo, rejected zero/tiny circles, toolbox changes and document
reopen. Additional checks cover legacy zero-radius deletion, rectangle-constraint
cleanup, atomic failure, dependent-solid rollback and native/JSON persistence.

Live JUCE component captures were inspected for the floating/docked toolbox,
entity tree, properties and existing menus. Tree/canvas mouse callbacks and
Delete/Escape callbacks were also invoked programmatically. These checks are
**not native pointer-driven interaction**; native pointer/menu/tooltip behavior
and Release builds remain unverified. Native control was not acquired during
this batch, and the owned test application was closed with its test scene saved.

Build logs: `build/sketch-usability-build-delivery.log`. Results and captures:
`build/cad-sketch-usability-check.result.json`,
`build/sketch-usability-check/` and `build/sketch-usability-live/`.
Reproduce with `--cad-sketch-usability-check` and
`tools/cad_api/sketch_usability_check.py` (empty test document; `--reopen` after
restart). See the [user guide](SKETCH_EDITING.md).

## Limits

Selection remains limited to the existing primary/secondary sketch entities.
Deleting geometry that would invalidate a dependent solid is rejected atomically;
adjust or remove that dependency first. Surviving edges of a broken rectangle
become ordinary constrained lines. No unrelated modeling features or workspaces
were redesigned.

Implementation began with commit `856e7e5`, pushed before live validation.
The delivery follow-up corrects the Float/Dock label and presentation error
recovery, and records the verified behavior. Stop after this assignment.
