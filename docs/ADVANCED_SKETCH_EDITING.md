# Advanced Sketch editing

The existing Sketch toolbox now has a Selection panel. Single selects an object;
Shift-click adds/removes it. A rectangle is selected as a group. Box, Circle region,
and Freehand select intersecting sketch geometry; Shift adds a region to the current
selection. Multiple toggles individual objects without holding Shift.

- Copy / Ctrl+C snapshots the selection and its internal constraints. Paste / Ctrl+V
  previews a copy anchored at the first selected object’s start/centre. Click to place;
  click again to repeat. Copies have independent entity, group and constraint identities,
  and can be pasted into another sketch.
- Rotate: select geometry, then pick a centre and drag toward the desired angle, or
  click the centre then a direction. Press A for an exact angle in degrees. The canvas
  previews the transformed selection before committing. One operation is one Undo.
- Mirror: select geometry and pick/drag two points defining the axis. The result is
  independent editable geometry, without a new persistent mirror relationship.
- Split / Node: click a line or Bézier curve. Lines split at the projected point;
  curves use exact de Casteljau subdivision. Existing far-end joins follow the second
  segment and the new node joins both segments. Splitting a rectangle edge releases
  its rectangle grouping, matching the existing line insertion semantics.
- Ellipse: drag from the centre to set its two semiaxes. Select a circle or ellipse
  and drag its axis handles; a stretched circle becomes an ellipse. Properties exposes
  exact centre, semiaxes and rotation. Ellipses use exact OCCT ellipse profiles.
- Rectangles have corner and edge resize handles, including after rotation. Dimensions
  and Properties refresh with each committed edit.
- Lines: H locks horizontal, V locks vertical, Shift snaps to 45-degree increments,
  and A enters an exact direction. These are drawing-time aids, without new persistent
  parametric constraints. Existing line Properties still supports exact degrees/radians.

Rotation releases absolute horizontal/vertical drivers on the transformed objects,
while retaining dimensions and joins. The UI reports this. Transformations conflicting
with constraints to unselected objects roll back and explain the conflict.
All committed edits use the existing document/OCAF undo-redo and persistence paths.

## Focused validation

The application has `--cad-advanced-sketch-check` for mathematical subdivision,
production selection/edit callbacks, clipboard remapping, analytic ellipse volume,
native save/reopen and undo/redo. It writes
`build/cad-advanced-sketch-check.result.json` and renders the actual workspace to
`build/advanced-sketch-check/workspace.png` without opening a user document.
Existing `--cad-sketch-edit-check` and `--cad-sketch-line-repair-check` cover the
previous vertex, rectangle and line-properties behavior.

Validated on 2026-10-23 with the Debug build (single MSBuild worker):

| Check | Passed |
| --- | ---: |
| Advanced Sketch geometry and production widgets | 51/51 |
| Existing Sketch edit regression | 20/20 |
| Existing line/property repair regression | 65/65 |
| Existing Sketch usability regression | 36/36 |

All 172 checks passed with exit code 0. The rendered workspace was visually
inspected, including the Selection panel, ellipse handles and synchronized Properties.
The combined rectangle position/dimension regression was corrected so translating a
rectangle carries its whole group in the same undo transaction. All test processes
exited; no test application was left running.

Final build log: `build/sketch-edit-build-regression-fix.log`.
The checks use synthetic events on production widgets, rather than native mouse input.

