# Sketch UI repair

Scope: sketch entity context-menu placement and numerical line properties.

## Changes

The context menu previously targeted the entire CAD workspace, so JUCE placed it
at the workspace boundary instead of the click. TreeView also supplies a
row-relative event position while retaining the original event component. The
repair converts the retained mouse-down point to screen coordinates for tree
clicks, and the current event point for canvas clicks. An explicit screen anchor
is applied after the target component; JUCE supplies scaling and display work-area
clamping. Keyboard/accessibility requests anchor to the entity row. Commands are
unchanged.

Selected lines now expose start/end coordinates, length, direction and a
degree/radian selector together. Endpoint edits calculate length/direction;
length/direction edits preserve the start and calculate the endpoint. Start edits
keep the endpoint fixed. Coordinates use the active sketch frame: XY, XZ, YZ or
face UV. Direction is counterclockwise from its positive horizontal axis. The
unit choice persists in existing workspace preferences; other CAD angle editors
currently use degrees and there is no global angular-unit preference.

One Apply calls `CadCommandService::setSketchLineGeometry` and one existing OCAF
transaction/rebuild. The existing endpoint representation remains authoritative.
Joined endpoints and curve handles follow; an existing length driver updates.
Orientation, fixed-start and connected constraints remain active. Conflicts roll
back the entire edit, including dimensions and dependent solids, with a visible
error. Native and engineering JSON schemas are unchanged. User and local API
documentation describe the shared fields and command behavior.

## Validation

Debug build passed. All **253 checks passed**: 65 focused repair checks, 36 sketch
usability checks, 20 sketch-edit checks, 20 workspace checks, 83 modeling checks
and 29 live API smoke checks. Results are recorded in the repair check JSON,
existing regression reports and the live application smoke-test report under
`build/`. The focused check exercises real property callbacks and native JUCE
popup peers using synthetic events, including translated/resized parents,
floating peers and the screen boundary. It also checks all three origin planes,
both input methods, units, malformed/nonfinite/degenerate inputs, constraints,
dependency rebuilding, Undo/Redo, stable identifiers and both persistence formats.

The live application smoke test drives the existing loopback `cad_ui` service and
captures actual JUCE component rendering. Native saves/reopens and loading a
verified engineering JSON fixture are exercised. The current `cad_ui` save action
accepts only `.djo`; JSON round trips use the existing document command service
in the focused check. Live floating and redocking preserve the line editor.

Native pointer/visual verification remains unverified: the Windows computer-use
helper failed screen capture twice (`FrameArrived timed out` / `window capture
timed out`). No pointer actions were issued. The computer-use kernel was reset
immediately. These synthetic-event/API checks are not pointer-driven verification.
The owned test application was saved and closed, and the user's original workspace
settings were restored. No computer input session remains active.

## Limits

Existing sketch-solver capabilities are preserved. Rectangle width/height drivers
may hold a non-driving edge; edit its driving edge or use the existing selected
vertex controls. Fixed constraints retain their current fixed-start semantics.
Lines require finite coordinates and length at least 0.00000001 mm. The repair
does not add constraint types or redesign the solver, docking or document system.
