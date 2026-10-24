# Main 3D toolbar, lighting and save-protection repair

## Toolbar presentation

`CadFloatingToolbar` now uses one layout calculation for both its desired size
and square 32-pixel button placement, with a 36-pixel pitch. Vertical is the main
3D default; additional columns wrap within the available height. Horizontal remains
an explicit option. The previous translucent `0xf0` background is replaced by an
opaque full-bounds fill and an opaque-component declaration. Icon drawing scopes
its graphics transform. Chrome has separate header/footer space, and icon controls
remain inside the toolbar bounds.

Modify and Snap reuse their existing command bindings and OpenGL viewport host.
They default to opposite workspace edges, below the viewport caption. Both support
floating, grip dragging, edge docking, independent 36-pixel collapsed launchers,
click/keyboard expansion and View-menu restoration. Legacy layout preferences
migrate to vertical while retaining visibility, positions and docking edges;
current preferences also retain explicit orientation and collapse state.

The shared Snap component retains a horizontal presentation and header controls in Sketch. Floating Snap placement
also respects space reserved by docked Sketch toolboxes, preventing overlap with
expanded symmetry controls. Floating Snap also moves to a free adjacent position
when its restored or dragged bounds intersect another visible toolbox. Drawing and
Selection Sketch toolboxes are unchanged.

## Lighting

The ordinary and logarithmic-depth fragment shaders share the same illumination
function: ambient 0.42, directional key 0.40, opposing upper fill 0.16, and lower
fill 0.10. Contributions remain directional and are capped at 1.0. Lights and
normals remain in world space, preserving shading as the camera orbits. Geometry,
material colors, clipping and other rendering paths are unchanged.

## Save protection

CAD mutation tracking already existed, but application exit did not consult it;
New/Open UI paths also lacked protection. Save As applied its extension before
checking for cancellation. Successful saves now establish the saved-content
baseline centrally. Undo/redo compares the restored OCAF-authored content with
that baseline. Selection, camera and toolbar state do not dirty the document.
Native save finalization replaces the destination without first deleting it.

CAD replacement, CAD/Craft workspace-tab close gestures, File Close and operating
system close all enter the Save / Don't Save / Cancel policy. Save completion must
succeed before proceeding. Cancel, failed writes, canceled Save As, obsolete chooser
callbacks and concurrent close requests cannot proceed with closure.

The current architecture has one authoritative CAD document shared by CAD/Craft
and 3D views, plus independently open fixture documents. This repair does not add
multiple CAD-document tabs. Fixture tabs and application exit use the same policy,
check each independent fixture in sequence, and permit Save or cancellation at
each step. Canceling a later fixture leaves the application open; previously
saved fixtures remain saved. Fixture undo/redo also tracks its saved-content baseline.
Workspace-tab closure hides that view; it does not destroy the shared CAD model.

## Validation

Build log: `build/main-toolbar-lighting-save-build-validated.log` (initial build:
`build/main-toolbar-lighting-save-build.log`; compatibility correction:
`build/main-toolbar-lighting-save-build-final.log`).

Focused checks:

- `--cad-main-toolbar-check`: production-component layout, bindings, opacity,
  collapse, synthetic drag/dock, restoration, and four actual GPU viewport renders.
- `--cad-save-protection-check`: CAD dirty/save/undo/redo, close decisions, failed
  writes, canceled/stale Save As, replacement, and multi-fixture close/exit policy.
- Existing workflow UI and Sketch toolbox checks cover shared toolbar compatibility.

The desktop Computer Use helper failed both capture attempts (`FrameArrived timed
out` and `window capture timed out`). Native pointer interaction is therefore not
claimed. Component event tests, actual component renders, and the existing GPU
framebuffer capture are used instead. The GPU/component composite is a diagnostic
composition, not a native desktop screenshot.

Validated on 2026-10-23 with the final Debug executable:

| Focused check | Passed |
| --- | ---: |
| Main 3D toolbar and actual GPU lighting checks | 84/84 |
| CAD save/close protection | 22/22 |
| Independent fixture save/close protection | 10/10 |
| Existing modeling/Sketch workflow UI checks | 135/135 |
| Existing Sketch toolbox checks | 48/48 |

All 299 checks passed; all four check processes returned exit code 0 and exited.
The fixture checks are nested in the save-protection report. Results are saved as
`build/cad-main-toolbar-check.result.json`, `build/cad-save-protection-check.result.json`,
`build/cad-workflow-ui-check.result.json` and `build/cad-sketch-toolbox-check.result.json`.

Actual GPU captures `front.png`, `side.png`, `back.png` and `other-side.png` under
`build/main-toolbar-check/` were visually inspected. Each capture also passed
pixel checks for visible fill illumination and a curved-surface shading gradient.
`gpu-and-components.png` combines the GPU model pass and actual toolbar components;
`tooltip.png` verifies the rendered operation description. Expanded/collapsed
component renders and the final Sketch symmetry/Selection/Snap coexistence render
were inspected. Native window compositing, pointer hover, and clicking the operating
system close/save dialogs remain unverified because desktop capture was unavailable.

The existing running CAD session was preserved. Its executable was retained as
`bin/Debug/djehuti_cad.before-toolbar-save-repair.exe` while the updated executable
was built at the normal `bin/Debug/djehuti_cad.exe` path. Save current work before
restarting that older running session to load the new build.
