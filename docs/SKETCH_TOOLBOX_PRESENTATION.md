# Sketch toolbox presentation

Drawing and Selection reuse `SketchToolPalette` as separate workspace-owned
instances. Selection exposes all eleven existing commands as stroked icons with
operation-specific tooltips, including Esc, Ctrl+C, Ctrl+V and exact-angle entry.
The command handlers and geometry operations are unchanged.

The minus button collapses either toolbox to a 36-pixel launcher. Click the
launcher (or focus it and press Enter/Space) to expand. Dragging the launcher moves
it without expanding. Dragging to an edge docks it; top/bottom docking lays out
expanded icons horizontally. Docking reserves canvas space; floating overlays the
workspace. Drawing symmetry controls retain their expansion setting.

Each toolbox persists its own collapse state, docking edge and floating position
in existing workspace view preferences. Expansion restores normal dimensions and
clamps the toolbox into the current available workspace. View > Sketch toolbox
and View > Selection toolbox provide explicit restoration after intentional menu
hiding or older hidden preferences. Header collapse never hides the launcher or
invokes a drawing/edit command. The existing Snap and Modify toolbar framework
and their View restoration entries remain unchanged.

Focused validation uses `--cad-sketch-toolbox-check` on the Debug application.
It exercises real component callbacks and synthetic JUCE mouse events, and renders
actual workspace components to `build/sketch-toolbox-check/expanded.png` and
`collapsed.png`. It does not claim native-pointer automation. The result is
`build/cad-sketch-toolbox-check.result.json`.

Validated on 2026-10-23:

- Debug target build passed; log: `build/sketch-toolbox-build.log`.
- Toolbox component checks: 48/48 passed.
- Advanced Sketch geometry/editing checks: 51/51 passed.
- Existing Sketch usability checks: 36/36 passed.
- All three check processes returned exit code 0 and exited normally.
- Expanded and collapsed workspace renders were visually inspected.

The toolbox checks cover all eleven icon identities/tooltips and command callbacks,
independent collapse, restoring floating bounds and expanded symmetry dimensions,
retaining active tool/selection/document state, compact-launcher movement, docking
and expansion at all four edges, canvas reservation, preference round trips,
View-menu restoration and keyboard expansion. Existing usability checks also
exercise expanded header dragging and docking. No native-pointer walkthrough was
performed; validation used the production component checks and rendered views.
