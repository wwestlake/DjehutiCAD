# Workspace menus, tools and reference geometry

## Application and workspace commands

File, Window, Settings, Account and Help remain application commands. Each editing workspace owns its own command bar: CAD has Edit/Create/Modify/Sketch/Assembly/View, FRust has Run, the node editor has Graph/Build, and 3D Views has Views/Reference guides. The Workspace menu floats or docks that content. Menus remain inside floating windows. Closing/hiding a dock panel preserves its content; the application Window menu can reopen it.

CreationDock owns panel content. Dock positions, floating windows and the Toolbox are stored in the application's existing settings. Hidden panels stay hidden on restore, and floating bounds are constrained to an available display. The first layout puts the Toolbox on the left and the editing workspaces in the center, with CAD active. Window > Restore default workspace layout restores that arrangement.

The old permanent command rows are removed. Apply/Cancel remain in the task inspector, and Finish Sketch remains visible during sketch editing. FRust source now saves under DjehutiCAD's own application-data directory. If that file does not yet exist, the prior shared autosave is read as a migration fallback; CAD never writes to DEWB's autosave.

## Toolbox and selection

The dockable Toolbox chooses persistent interaction tools for the shared CAD document. Pointer, Box select and Circle select operate in the model/3D views; Bodies/Faces/Edges choose the selection filter. Sketch Select, Line, Rectangle and Circle become available while a sketch is being edited. The active tool/filter is highlighted. Components and hardware are not Toolbox tools; a Parts Library is a separate future capability.

- Pointer click selects the geometry under the cursor. Dragging still orbits; Shift/right-drag pans. Wheel zooms.
- Box left to right encloses projected geometry; box right to left crosses geometry. Circle crosses projected geometry. These area tools include obscured geometry, rather than only visible surfaces. Ctrl adds to an area selection.
- Area selection is a model-view operation in this milestone. The existing sketch editor retains its entity selection behavior.
- Esc in a 3D view cancels the area gesture and returns to Pointer. F fits that view.
- Selection highlights are shared across views; cameras remain independent. The primary selected item drives the inspector and local-plane display. Right-click actions target the item under the pointer; multi-selection does not imply a bulk Delete or bulk modeling operation.

Right-click a 3D view for Fit/Reset and its guide visibility. A selected solid body also offers inspect/export/hide/delete; an instance offers inspect/export; a face offers Create Sketch (the existing service checks planarity and persistent support). Right-drag remains navigation and does not open a menu after a drag.

## Reference geometry

- Floor: a fixed XY world grid at Z=0, with adaptive 1/2/5 millimeter spacing. It is visible by default.
- Camera: world Z is up; the default/reset isometric view looks down onto the XY floor. Orbit changes the view around the shared model.
- World planes: optional bounded XY/XZ/YZ grids and labeled axes at the world origin.
- Selected local planes: optional grids and XYZ axes at the selected part's authored local origin, or the selected instance's solved local frame. A local origin is not a computed geometric center.
- Joint frames and limits: optional parent/reference and moving XYZ triads, a local rotation-plane grid, current coordinate and role, and configured limits. Revolute joints show an angular guide and current radial direction. Prismatic joints show the travel axis and limit ticks. Labels distinguish hard from advisory limits. Selected joints are emphasized by showing their guides; otherwise all supported moving joints are shown.

Coordinates and frames come from the same solved pose as the rendered assembly. The muted triad is the parent/reference joint frame; the bright triad is the moving frame. Reference angles are respected, and linear display converts runtime meters to millimeters. Angular labels use degrees. Very large angular spans are visually bounded to two revolutions; numerical limits retain their full values.

Each view has its own visibility settings, available from its right-click menu. The 3D Views reference menu changes all views together. Settings and the number of open 3D views persist between ordinary application sessions. Guide geometry uses the shader/depth pipeline and is excluded from geometry selection, Fit All bounds, document history and manufacturing exports. Grid visibility does not change snapping.

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

This diagnostic establishes service/component behavior. It does not substitute for observing native pointer interaction, rendered guides or menus in the running application. Actual build/check/GUI results are recorded in the handoff at closeout.
