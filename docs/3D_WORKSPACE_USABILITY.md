# 3D workspace selection, navigation and sketch visibility

This increment follows `398bb0a` and preserves the existing geometry commands,
OCAF transactions, document save protection and toolbox infrastructure.

## Tool access and popups

Main CAD has an actual model Selection toolbox, separate from the Sketch Selection
toolbox. It uses the existing `CadFloatingToolbar`, with pointer, box, circle,
multiple, body/face/edge modes, Select All and Clear. View > Selection toolbox
reflects the toolbox for the active workspace. Each instance has independent
visibility, placement, docking and collapse state. Restored coordinates clamp into
the available viewport.

Scoped desktop tooltip windows replace child tooltip popups. JUCE's parent-peer
check previously excluded the nested native OpenGL peer, and child popups could be
covered by that peer. Tooltip lookup stays scoped to its owning workspace, while
popup placement uses the display's available bounds. Existing modeling toolbar
layout and command bindings are unchanged.

All 3D viewport context menus now use the event's screen position as a one-pixel
anchor, rather than targeting the entire viewport. JUCE positions the menu within
the current display. Right-click in an edge operation preserves the current edge
set. The same menu path serves the primary and additional 3D views.

## Edge selection

Existing projected region picking is used for Fillet and Chamfer:

- Left-to-right rectangle: the entire sampled edge must be enclosed.
- Right-to-left rectangle: any projected segment intersection selects the edge.
- Circle: any projected segment intersection selects the edge.
- Regions include obscured edges of visible geometry, reject geometry outside the
  camera clipping slab, and add to the active target's current edge set.

Bulk selection is distinguished from individual clicks, so a region containing
one edge cannot accidentally toggle it. Changing a selection tool does not toggle
an existing edge. Existing click/Ctrl-click edge toggling remains available.
Selected edges are red; face selection retains its existing color.

Select All Edges is available in the operation form, context menu, Selection
toolbox and Ctrl+A. It selects nondegenerate edges shared by two distinct faces.
Seams, degenerate and non-manifold/boundary edges are excluded with a count. Preview
and Apply still validate the complete edge set at the chosen radius/distance;
topological eligibility cannot guarantee that every radius/distance will succeed.
No Fillet or Chamfer geometry algorithm was changed.

## Camera

Focus Selected retains the existing world/posed-bound framing calculation and
makes the selected center the orbit target. Orbit Selected uses the same framing.
Set Orbit Point (View/context menu, Ctrl+Shift+O) picks a surface point or a point
on the world XY floor; camera automation also accepts explicit finite XYZ values
with `operation: orbitPoint`. Escape cancels point picking.

Fly Mode moves in the camera's forward/right basis and world Z. Looking around
keeps the eye position fixed instead of orbiting. Navigation is Z-up with no roll;
pitch is constrained near the poles. Movement is timed, pauses while focus is
elsewhere or Ctrl/Alt is held, and does not modify geometry, revision or document
dirty state. Entering orbit-point picking exits Fly Mode, so look/move input cannot
mask a requested point pick. Existing
orbit/pan controls remain in use outside Fly Mode.

## Shortcut reference

| Context | Shortcut | Action |
| --- | --- | --- |
| Main CAD | Ctrl+A | Select all in the active body/face/edge context; in Fillet/Chamfer, all eligible target edges |
| Main CAD | Ctrl+Shift+A | Deselect/Clear selection |
| Main CAD | 1 / 2 / 3 | Body / Face / Edge mode |
| Main CAD | V / B / C / M | Pointer / Box / Circle / Multiple selection |
| Main CAD | Ctrl-click | Toggle an individual item; edge-operation clicks retain their existing toggle behavior |
| Main CAD | Delete | Delete selected whole objects through existing guarded commands; in an edge operation, clear edge selection |
| Main CAD | Esc | Cancel active operation, or clear selection and return to pointer |
| 3D viewport | F | Fit all |
| 3D viewport | Shift+F | Focus Selected and orbit its center |
| 3D viewport | Home | Reset standard camera view |
| 3D viewport | Ctrl+Shift+O | Pick orbit point |
| 3D viewport | Ctrl+Shift+F | Enter/exit Fly Mode |
| Fly Mode | W/S, A/D, Q/E | Forward/back, left/right, down/up |
| Fly Mode | Mouse drag or arrow keys | Look around |
| Fly Mode | Shift | Faster movement |
| Fly Mode | Esc | Exit Fly Mode |
| CAD | Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z | Existing document undo / redo / redo |
| CAD | Ctrl+S / Ctrl+Shift+S | Save / Save As |
| Sketch | Ctrl+C / Ctrl+V | Existing supported geometry copy/paste |

Text editors retain their own selection and undo shortcuts. No unsupported 3D
geometry copy/paste operation is introduced. Sketch editing retains its existing
shortcuts and command paths.

## Sketch visibility

Every sketch row has an eye control. Show All Sketches / Hide All Sketches are
available directly in View. These update existing document visibility through the
command service; geometry and editability are preserved. The active Sketch canvas
remains editable even if that sketch is hidden in the model viewport. Reference
controls and eyes use the same per-sketch document visibility, so undo cannot leave
a stale reference override masking the restored state. Global visibility updates
use the existing per-sketch transactions.

## Validation

Build: Debug DjehutiCAD target, one build worker, output redirected to
`build/workspace-navigation-build-accepted.log` (initial and test-compatibility
build logs: `workspace-navigation-build.log` and `workspace-navigation-build-final.log`).

Focused regression entry point: `--cad-workspace-navigation-check`.
It exercises production component callbacks, projected selection, off-origin
camera math, tool access, contextual keys, and sketch visibility. The separate
`--cad-navigation-live` mode opens disposable geometry in a test-only window for
native interaction checks; it does not load or change a user document.

Native validation was attempted in the disposable test window. Computer Use
returned `FrameArrived timed out: timed out waiting on channel`; a fresh-window
retry returned `window capture timed out: timed out waiting on channel`. No stale
coordinates or accessibility indexes were used. The owned test window was closed
normally. Native pointer/keyboard and hover behavior is therefore not claimed;
production component events, camera/selection mathematics and rendered inspection
provide the validation below.

Validated on 2026-10-24 with the final Debug executable:

| Focused suite | Passed |
| --- | ---: |
| New workspace selection/navigation/visibility checks | 109/109 |
| Existing viewport range, camera and interaction checks | 59/59 |
| Existing modeling/Sketch workflow UI checks | 135/135 |
| Existing Chamfer geometry/history checks | 20/20 |
| Existing Sketch toolbox checks | 48/48 |
| CAD save protection | 22/22 |
| Nested independent-fixture save protection | 10/10 |
| Existing toolbar and GPU-render checks | 84/84 |

All 487 checks passed; all seven check processes returned exit code 0 and exited.
The owned disposable native test window also closed normally. An additional attempt
with explicit window activation still timed out during capture, so it did not
provide evidence of pointer, hover or native menu placement.

Result files follow `build/cad-<suite>-check.result.json`; final process logs follow
`build/navigation-<suite>-accepted.log`. The new suite reports separately that its
input events were component-level rather than native pointer automation.

The actual GPU/model plus component diagnostic at
`build/main-toolbar-check/gpu-and-components.png` was visually inspected with the
new Selection toolbox present. The tooltip render and sketch-eye component render
were also inspected. GPU/component composition is a diagnostic, not a native
window screenshot. Exact menu anchoring and desktop-tooltip bounds were checked
through production component methods; native menu/hover presentation remains an
acceptance limitation of the unavailable desktop capture.

The new suite additionally covers existing Fillet execution and undo, circle and
box gestures through the operation boundary, single-result bulk selection,
wrong-target/obsolete edge rejection, seam exclusion, modifier-key routing in Fly
Mode, the transition from Fly to point picking, and preserving text-editor shortcuts.
No unsupported 3D copy/paste function was added, and no geometry or save architecture
was changed.
