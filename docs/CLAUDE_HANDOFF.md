# DjehutiCAD handoff to Claude

Prepared 2026-10-24. Repository: `D:\DjehutiCAD`.

## Start here

Current branch: `feat/sketch-click-line-drawing`.
Implementation HEAD at handoff preparation: `fa81e17` — default sketch lines to connected click placement. The documentation commit containing this handoff follows it; use `git log -2` for the final HEAD.

The requested implementation is complete and committed. No merge or push was performed. There are no tracked source changes awaiting completion. Preserve these unrelated untracked items exactly as found:

- `examples/mechanisms/coil_winder.djo`
- `motor_test_out/`

Do not resume old DEWB, robot demonstration, structural engineering, or other historical work orders without a new user request. This handoff does not authorize another implementation assignment.

## Latest completed assignment: Sketch line drawing

Default input is now click-click. The first click sets the start, button-free mouse movement previews, and each subsequent click commits a segment and uses its exact endpoint as the next start. Repeated endpoint clicks do not create zero-length geometry.

Escape, Enter, or the **End line sequence (Enter)** button discards the uncommitted preview and keeps completed segments. The Line tool remains available in Sketch; another click begins a separate sequence. Selecting another tool or finishing Sketch also ends drawing.

The Line properties expose **Line input**: **Click-click (connected)** or **Click-and-drag**. The choice uses existing workspace settings persistence; changing it cancels only the transient preview. Drag mode retains the original press-drag-release behavior. Existing snapping, direction guides, geometry commands, document history and persistence are reused. Explicit automation begin/update/commit semantics are unchanged. Node-editor connections were not modified.

Files changed in `fa81e17`:

- `Source/CadWorkspacePanel.cpp`: Sketch mouse input, line preview/sequence completion, preference UI and serialization, focused component checks.
- `Source/CadWorkspacePanel.h`: default click-click preference field.
- `Source/SketchToolPalette.h`: Line tooltip.
- `Source/CadAdvancedSketchChecks.inc`: explicitly select optional drag mode for legacy drag guide checks.
- `docs/SKETCH_LINE_INTERACTION.md`: behavior and validation reference.

## Accepted validation

Debug executable: `D:\DjehutiCAD\bin\Debug\djehuti_cad.exe` (built 2026-10-24 at 10:55 local time).
Accepted build log: `build/sketch-click-line-build-accepted.log`.

All final suites exited normally with code 0:

| Argument | Passed |
| --- | ---: |
| `--cad-sketch-usability-check` | 56/56 |
| `--cad-sketch-line-repair-check` | 65/65 |
| `--cad-advanced-sketch-check` | 51/51 |
| Total | 172/172 |

Results: `build/cad-<suite>-check.result.json`.
Process logs: `build/click-line-<suite>.log` and `.err.log`.

Coverage includes actual production component mouse callbacks for both modes, a connected closed profile, free-movement preview, endpoint snapping, Shift guides, repeated-click rejection, Escape/Enter/button completion, individual-segment undo/redo, save/reopen, settings round-trips, existing constraints and advanced editing.

Earlier advanced-suite failures were three legacy drag gestures assuming drag was still the default. The fixture now explicitly selects drag mode; the final rerun passes. An accidental overlapping build attempt failed on a PDB lock; subsequent serialized final builds succeeded. Use the accepted log and final result files above, not intermediate failed logs.

No test CAD processes remained running at handoff verification. No user document was opened or modified by validation.

## Remaining acceptance limits

There is no known failing automated check in the focused final validation. Physical mouse use in a fresh native window was not performed for the latest assignment. Persistence was validated through existing settings serialization/restoration and the application shutdown save path was inspected; a full application restart preference test was not performed.

If asked to perform manual acceptance, draw a connected closed profile with clicks, end it with Escape/Enter/button, switch to drag mode and draw a segment, then restart and verify the preference. Exercise undo/redo and saving the profile. Do not claim these manual checks have already happened.

## Earlier completed work retained on this branch

- `f48b2a7`: main 3D Selection toolbox restoration, edge region/Select All selection for Fillet/Chamfer, pointer context menus and tooltips, Fly Mode, Focus Selected/orbit point, sketch visibility eyes and global controls. Reference: `docs/3D_WORKSPACE_USABILITY.md`.
- `398bb0a`: main 3D toolbar presentation repair, balanced lighting and unsaved-document protection.
- `6bc4129`: Sketch toolbox icons and collapsible docking.

The 3D workspace assignment previously passed 487 checks at its own commit. That is historical evidence, not a claim that all 487 were rerun on the latest executable. Native capture repeatedly timed out during that assignment; its pointer/hover/native-menu visual acceptance remains unverified. GPU/component diagnostic images are not native desktop screenshots.

Useful existing shortcuts: Shift+F focuses selected geometry; Ctrl+Shift+F toggles Fly Mode; Ctrl+Shift+O picks an orbit point. Full mappings and earlier validation are in the 3D workspace document.

## Build and continuation discipline

Use targeted inspection and preserve unrelated work. Do not run concurrent builds. If another source change is requested, use one focused Debug build with redirected output, then only the relevant checks:

```powershell
$env:_LINK_='/DEBUG:FASTLINK /OPT:REF /INCREMENTAL:NO'
cmake --build build --config Debug --target DjehutiCAD -- /m:1 *> build/claude-debug-build.log
```

No further build is needed just to receive this handoff. Source matches the accepted executable; only this handoff documentation is added afterward. Do not merge or push without user authorization. Close only test instances you own.
