# Workspace menus, tools and reference geometry

Modify includes Create Surface, Trim Surface, Extend Surface, Sew Surfaces,
Solidify Shell and Thicken Surface. All use the existing source-selection task
form, isolated Preview, Apply/Cancel and inspector editing workflow. Open surfaces
display from both sides. Trim supports picking a region point directly in model
views; sewing reports unmatched boundaries and leaves solidification explicit.
See the [surface modeling guide](SURFACE_MODELING.md).

## Application and workspace commands

File, Window, Settings, Account and Help remain application commands. Each editing workspace owns its own command bar: CAD has Edit/Create/Modify/Sketch/Assembly/View, FRust has Run, the node editor has Graph/Build, and 3D Views has Views/Reference guides. The Workspace menu floats or docks that content. Menus remain inside floating windows. Closing/hiding a dock panel preserves its content; the application Window menu can reopen it.

CreationDock owns panel content. Dock positions, floating windows and the Toolbox are stored in the application's existing settings. Hidden panels stay hidden on restore, and floating bounds are constrained to an available display. The first layout puts the Toolbox on the left and the editing workspaces in the center, with CAD active. Window > Restore default workspace layout restores that arrangement.

The old permanent command rows are removed. Apply/Cancel remain in the task inspector, and Finish Sketch remains visible during sketch editing. FRust source now saves under DjehutiCAD's own application-data directory. If that file does not yet exist, the prior shared autosave is read as a migration fallback; CAD never writes to DEWB's autosave.

Mirror, Linear Pattern and Circular Pattern also appear under **Modify**. Their
source/target slots use the existing tree and view selection, reference controls
support principal/custom definitions and picked faces/edges, and Preview runs an
isolated draft. Select a result and **Edit sources and parameters** to reopen it.
See [Mirror](MIRROR.md), [Linear Pattern](LINEAR_PATTERN.md), and
[Circular Pattern](CIRCULAR_PATTERN.md).

**Modify** also provides [Sweep](SWEEP.md), [Loft](LOFT.md) and
[Virtual Router / Mill](VIRTUAL_ROUTER_MILL.md), with source slots, isolated
Preview, Apply/Cancel and inspector editing. Sweep paths use sketches or connected
model edges. Loft sections can be reordered. Router uses an accessible planar
entry reference and region/path selection.

## Toolbox and selection

The CAD **Modify** menu includes [Virtual Lathe](VIRTUAL_LATHE.md),
[Chamfer](CHAMFER.md), and [Drill Press / Hole](HOLE_DRILL_PRESS.md). Lathe edits
outside/bore profiles through the existing Revolve command. Chamfer supports
successive-click selection of multiple edges and a temporary preview. Drill Press
uses a selected planar face with numeric U/V positioning and plain, counterbore
or countersink geometry. Select the resulting body to edit parameters; dedicated
buttons reopen profile/edge/support selection. Each tool shares the existing
command service, OCAF history, native save and engineering JSON.

The dockable Toolbox chooses persistent interaction tools for the shared CAD document. Pointer, Box select and Circle select operate in the model/3D views; Bodies/Faces/Edges choose the selection filter. Sketch Select, Line, Rectangle and Circle become available while a sketch is being edited. The active tool/filter is highlighted. Components and hardware are not Toolbox tools; a Parts Library is a separate future capability.

- Pointer click selects the geometry under the cursor. Dragging still orbits; Shift/right-drag pans. Wheel zooms.
- Box left to right encloses projected geometry; box right to left crosses geometry. Circle crosses projected geometry. These area tools include obscured geometry, rather than only visible surfaces. Ctrl adds to an area selection.
- Area selection is a model-view operation in this milestone. The existing sketch editor retains its entity selection behavior.
- Esc in a 3D view cancels the area gesture and returns to Pointer. F fits that view; Shift+F frames the selected geometry.
- Selection highlights are shared across views; cameras remain independent. The primary selected item drives the inspector and local-plane display. Right-click actions target the item under the pointer; multi-selection does not imply a bulk Delete or bulk modeling operation.

Right-click a 3D view for Fit all, Fit selection, Reset and its guide visibility. Selection framing uses the selected bodies' bounds, including solved instance positions. A selected solid body also offers inspect/export/hide/delete; an instance offers inspect/export; a face offers Create Sketch (the existing service checks planarity and persistent support). Right-drag remains navigation and does not open a menu after a drag.

## Reference geometry

- Floor: the XY world plane at Z=0, visible by default. A procedural shader covers the visible plane without a finite line-patch edge. World-aligned 1/2/5 millimeter levels blend continuously with pixel scale; major lines occur every five intervals. The grid fades toward the horizon and stays behind model geometry. The HUD spacing is approximate because perspective changes the scale across the floor.
- Camera: world Z is up; the default/reset isometric view looks down onto the XY floor. Orbit changes the view around the shared model. Pan scales with viewing distance, and wheel zoom retains its exponential response. Clipping follows visible posed model bounds; unusually wide depth ranges use logarithmic depth to reduce Z-fighting. Fit respects the viewport aspect ratio. Selection framing permits inspecting a small component within a large assembly.
- World planes: optional bounded XY/XZ/YZ grids and labeled axes at the world origin.
- Selected local planes: optional grids and XYZ axes at the selected part's authored local origin, or the selected instance's solved local frame. A local origin is not a computed geometric center.
- Joint frames and limits: optional parent/reference and moving XYZ triads, a local rotation-plane grid, current coordinate and role, and configured limits. Revolute joints show an angular guide and current radial direction. Prismatic joints show the travel axis and limit ticks. Labels distinguish hard from advisory limits. Selected joints are emphasized by showing their guides; otherwise all supported moving joints are shown.

Coordinates and frames come from the same solved pose as the rendered assembly. The muted triad is the parent/reference joint frame; the bright triad is the moving frame. Reference angles are respected, and linear display converts runtime meters to millimeters. Angular labels use degrees. Very large angular spans are visually bounded to two revolutions; numerical limits retain their full values.

Each view has its own visibility settings, available from its right-click menu. The 3D Views reference menu changes all views together. Settings and the number of open 3D views persist between ordinary application sessions. The floor is a background pass independent of model clipping; world/local/joint guides retain their depth-tested overlays. Guides are excluded from geometry selection, Fit All bounds, document history and manufacturing exports. Grid visibility does not change snapping.

## Automation

Agent tool `cad_workspace`, FRust host `djehuti_cad_workspace(args_json)` and node `CAD Workspace` share the same command. The result reports `ok`, current guide settings, active tool flags and document revision.

Examples:

```json
{"action":"tool","tool":"boxSelect"}
{"action":"guides","workspace":"cad","guides":{"floor":true,"local":true,"joints":true}}
{"action":"guides","workspace":"views","view":1,"guides":{"world":true}}
{"action":"float","workspace":"cad"}
{"action":"dock","workspace":"cad"}
{"action":"state"}
```

Workspace identifiers: cad, frust, nodes, views, agent, toolbox. Guide operations apply to cad or views; view indices are zero-based, and -1 means every 3D view. Tool operations change the CAD tool state, and unavailable sketch tools return an error.

## Verification boundary

`--cad-workspace-check` is a focused application diagnostic for local menu ownership, floating/redocking, persistent tool state, guide commands and independent settings, unchanged document revision, region picking and floor geometry. It uses an isolated in-memory workspace and does not save its preferences. Results are written to `build/cad-workspace-check.result.json`.

`--cad-viewport-range-check` checks adaptive clipping, scale extremes, aspect-aware fit, posed selection framing, camera callbacks and picking. Results are written to `build/cad-viewport-range-check.result.json`. See [Order 6 validation and limitations](VIEWPORT_RANGE_ORDER6.md).

This diagnostic establishes service/component behavior. It does not substitute for observing native pointer interaction, rendered guides or menus in the running application. Actual build/check/GUI results are recorded in the handoff at closeout.
