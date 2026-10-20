# Sketch editing architecture

Findings recorded before the sketch-editing work, followed by the design that work implemented.

## 1. Findings (before the change)

### Data model (`CadDocument.h`)
- A sketch holds `entities` (line or circle) and `constraints`.
- A line stores its own two endpoints, `(x1, y1)` = `p1` and `(x2, y2)` = `p2`. **There is no shared vertex
  object.** Connectivity is expressed by `coincident` constraints between endpoint references such as
  `line-5:p2` and `line-7:p1`. These are added by Line when an end snaps to another end, and by Rectangle for its
  four corners.
- Rectangles are four lines with a common `groupId`, `horizontal` and `vertical` constraints, two `distance`
  constraints (width and height) and four corner `coincident`s.
- The profile builder (`sketchProfiles`) chains lines **by position**: ends within 1e-7 mm form a wire.
  Circles are separate profiles. Coincidence of position is what OCCT sees, whether or not a constraint exists.

### Edit path and history
- Path: input event, then `CadWorkspacePanel` / `SketchCanvas`, then a `CadCommandService` call, then
  `CadDocument::mutate`. `mutate` opens an OCAF command, applies the edit, runs `solveSketch`, `rebuild()` and the
  OCAF sync, writes the sketch JSON to its container and commits.
- **Every committed sketch edit is one OCAF command, so undo and redo already existed** and restore sketches from
  OCAF: creation, rectangle, circle, line, coordinate and dimension edits, constraints.
- `Edit > Undo`, `Edit > Redo`, the toolbar buttons and `cad_undo` / `cad_redo` all reach `CadDocument::undo` and
  `redo`. Undo works while a sketch is open; `refreshFromDocument` leaves the sketch if its creation is undone, and
  drops selections that vanished.
- Redo is cleared by OCAF when a new command is committed after an undo, which is correct behaviour.
- There was no dragging of existing geometry, so no drag history existed.

### Root cause of "Undo does not work"
- **No keyboard binding existed.** `CadWorkspacePanel::keyPressed` and `SketchCanvas::keyPressed` handled only
  Escape, and nothing else in the window handled Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z. The history mechanism worked; the
  keys were never routed to it.
- **Keyboard focus routing (found during the live test).** Adding the binding to the panel was not enough.
  - JUCE's `ComponentPeer::handleKeyPress` walks from the *focused* component up through its parents.
  - After toolbar clicks or tree selections, keyboard focus is often nowhere inside the CAD panel. The keys then go
    to the top-level window and never reach the panel.
  - Fix: the panel registers itself as a `juce::KeyListener` on its top-level component (`parentHierarchyChanged`),
    so undo keys work whatever has focus.
  - Windows may deliver Ctrl+letter as a control character (1–26). The handler maps that back to the letter.
  - The automation state now reports `keyboardFocus` so routing can be inspected.
- Secondary: applying a position and a dimension change together in the entity panel ran **two** commands, so one
  Apply needed two Undos.

### Other defects found
- `solveSketch` treated **any sketch made of exactly four lines** as an axis-aligned rectangle. Its first entity's
  start point and lengths overwrote the geometry, so a hand-drawn trapezoid became a rectangle on the next edit.
- Endpoint snap rings were drawn only while a drag was active (`snapTarget` was set in `mouseDown` and
  `mouseDrag`), so endpoints could not be discovered by hovering.

## 2. Design adopted

- **One history:**
  - Every new interaction is one `mutate` command: vertex move, vertex insertion, curve creation, handle move
    and combined entity edits.
  - Drags show a local preview and commit once on release, giving one undo step per drag.
  - No second history system was introduced.
- **Keyboard:**
  - Ctrl+Z undoes; Ctrl+Y and Ctrl+Shift+Z redo.
  - They are handled by `CadWorkspacePanel::handleHistoryKey`. It is reached from the panel's own `keyPressed`
    when focus is inside the panel, and otherwise from its `KeyListener` on the top-level window.
  - Text fields keep their own editing undo, because a focused `TextEditor` consumes those keys first.
  - An active drag gesture is cancelled before undoing.
- **Vertices:**
  - A vertex is an endpoint reference (`entity:p1` or `entity:p2`).
  - Moving one moves every endpoint joined to it by `coincident` constraints (transitively), plus endpoints
    exactly at the same position, because the profile builder already treats those as joined. A closed
    profile therefore stays closed.
  - The result must satisfy every remaining constraint, otherwise the move is refused with the reason.
  - Rectangle corners move with the opposite corner fixed, and the width and height dimensions follow.
- **Insertion:**
  - Splitting line L at point P keeps L's id for the first part (p1 to P) and creates a new line for P to p2.
  - Constraints on L's old p2 move to the new line's p2, and a coincident joins the two parts.
  - The split line's own orientation and length constraints are removed and named in the result message, never
    silently. A split rectangle becomes an ordinary polygon.
- **Curves:**
  - `bezier` is a cubic Bézier sketch entity: endpoints p1 and p2, control points c1 and c2.
  - It is built as an OCCT `Geom_BezierCurve` edge, chains into profiles like a line, and takes part in
    `coincident` constraints at its ends.
  - It is saved with the sketch (new `cx1, cy1, cx2, cy2` fields; older sketches have none) and included in
    the rebuild signature.
  - Handles are free by default. *Smooth join at start/end* makes a one-time G1 (tangent) join with the
    neighbouring segment and keeps the handle's length. It is not a persistent tangency constraint.

## 3. Gestures (Sketch select tool)

| Gesture | Result | History |
|---|---|---|
| Hover near an endpoint | Yellow ring; cursor changes | none |
| Click an endpoint | Selects the vertex (filled orange); panel shows Vertex U/V and *Move vertex* | none |
| Drag an endpoint | Joined endpoints follow; rectangle corners reshape the rectangle | one step on release |
| Double-click a line | Inserts a vertex at the clicked point and selects it | one step |
| *Insert vertex at midpoint* (line panel) | Same, at the midpoint | one step |
| Curve tool: drag start to end | New curve; ends snap to existing ends and add coincident constraints | one step |
| Drag a square handle of the selected curve | Reshapes the curve | one step on release |
| Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z | Undo / redo / redo | moves through history |

API equivalents: `cad_sketch_bezier`, `cad_sketch_move_point`, `cad_sketch_insert_vertex`, `cad_sketch_smooth`.

## 4. Verification

- `djehuti_cad --cad-sketch-edit-check` writes `build/cad-sketch-edit-check.result.json` and covers 20 checks:
  - the trapezoid is kept;
  - one revision per edit;
  - undo, redo and redo cleared after a new edit;
  - insert, move and undo;
  - rectangle corner reshape and split;
  - line + curve extrude (exact volume, Bézier edges present);
  - handle drag, an endpoint carrying its handle, smoothing;
  - save, reopen and edit;
  - a single command per panel edit;
  - refusals.
- Live interaction test (maximized app, real mouse input), with evidence in `build/sketch-live/`:
  - hover rings;
  - vertex drag;
  - Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z from the keyboard;
  - a new edit after undo leaves nothing to redo;
  - double-click insertion and dragging the new vertex;
  - Curve tool snapping, handle drag, extrude;
  - Save As, New, Open with the curve and handles restored.

## 5. Known limitations

- Smoothing is a one-time operation, not a persistent tangency constraint.
- The live drag preview of a rectangle corner is approximate; the committed result is solved exactly.
- A drag that conflicts with driving dimensions is refused with the reason rather than relaxing the dimension.
- There is no UI for deleting individual constraints; a split line's own constraints are removed and reported.
- Curves accept coincident constraints at their ends only (no horizontal, vertical or length constraints).
- Documents with curves cannot be read by app builds older than this change; older documents load unchanged.
