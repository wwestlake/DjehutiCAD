# DjehutiCAD Handoff for Claude

## Purpose and ownership

DjehutiCAD is an independent professional mechanical CAD application for
Windows. It is a JUCE desktop application using Open CASCADE Technology (OCCT)
and OCAF. The human user is the architect. The previous Codex chat served as
co-architect, reviewer, and order writer; implementation is now handed to
Claude. Read this briefing as context and wait for the user's next explicit
work order. Do not start a new modeling phase automatically.

Repository: `https://github.com/wwestlake/DjehutiCAD`

Repository root: `D:\DjehutiCAD`

Default branch: `main`

Build command:

```powershell
cmake --build build --config Debug --target DjehutiCAD --parallel 1
```

Build output: `D:\DjehutiCAD\bin\Debug\djehuti_cad.exe`

Primary dependencies in use:

- JUCE: `D:\JUCE`
- OCCT source/dependency tree: `D:\DjehutiCAD\deps\occt-src`
- FRustLang libraries: `D:\FrustLang\lib`
- LLVM used by FRust integration: `D:\000 Creation Suite\apps\CreationEngine\vcpkg_installed\x64-windows\share\llvm`
- WinFlexBison: `D:\tools\winflexbison`

Do not modify DEWB, FRust, or the original Creation Suite repository as part
of DjehutiCAD work.

## Assembly interference detection — October 19, 2026

Code:
- `Source/CadInterference.*`: the engine;
- `CadWorkspacePanel::checkInterference` / `clearInterference`: the one operation behind the UI and the API;
- `Viewport3D::setInspectionLines`;
- `CadMotionRuntime::shownInstanceWorld()` / `synchronize()`: read-only. The solver is unchanged.

### Behaviour

- **Components** follow the display rules: every assembly instance of a visible solid body, at its pose, plus
  every visible solid body without instances, at its placement. Features inside one finished body are never
  compared. The order is deterministic (instances, then bodies) and each pair i < j is checked once.
- **Pose:** `shown` (default) uses the runtime's double-precision instance frames for the pose currently
  displayed. That includes unsaved motion previews and IK results, and nothing is committed. `reference` uses
  the saved placements. `synchronize()` first applies any pending document edit to the pose. If the pose still
  doesn't match the document revision, the check reports an error rather than inspecting stale frames.
- **Geometry:**
  - Each body is copied once per inspection, so OCCT never touches the live shapes; copies are placed per
    component.
  - Broad phase: `Bnd_Box` rejection.
  - Narrow phase: `BRepAlgoAPI_Common` with a fuzzy value of 1e-5 mm.
  - Interference is a common volume greater than max(1e-3 mm³, 1e-6 × the smaller part's volume). Shared faces,
    edges or points are contact.
  - Full containment is reported with a note. A failed Boolean is listed under `failures` and makes the result
    "incomplete", never a false "no interference".
- **Display:** interfering parts are highlighted red through the existing `setHighlights`. Edges of the common
  volume are drawn as yellow lines over the model.
- **Results panel:** the pairs with their volumes, select buttons, Check again and Clear.
- **Staleness:** a new pose serial (`mechanismChanged`) or a document revision or instance change
  (`refreshFromDocument`) marks the result out of date. Highlights and outlines are removed, and the panel says
  so.
- **Entry points:**
  - UI: Assembly > Check interference / Clear interference; commands `interference` and `clearInterference`
    for `cad_ui`.
  - Agent/API: `cad_check_interference {pose, clear}`. It returns `ok`, `completed`, `result`
    (`noInterference` / `interference` / `incomplete` / `error`), `interferences[]`, `failures[]`, `poseSerial`
    and `documentRevision`.
  - Help item 7a.

### Verification

`--cad-interference-check`: 11/11.
- Separated boxes: none (rejected by bounding boxes).
- Overlap: 500 mm³ with its region outlined.
- Shared face: contact only.
- Contained 4 mm cube: 64 mm³, flagged as contained.
- Union of overlapping features: one component, no pairs.
- Jointed assembly:
  - at rest: none;
  - 10 mm preview: 500 mm³ at the shown pose, and the reference pose is still clear;
  - 5 mm preview: the faces touch after the joint transform and count as contact;
  - the document, joints and revision are unchanged.

`--cad-face-check` 19/19 and `--cad-export-check` 29/29 still pass.

Live, on `SCARA Pick and Place Robot.djo` through the API:
- Reference pose: 7 components, 21 pairs, 6 candidates, all contact; no interference, in 0.47 s.
- J3 previewed to −150 mm: Link 2 / Quill, 1884.96 mm³, exactly the collar ring π(14² − 11²) × 8. The screenshot
  showed both parts red, the ring outlined and the panel listing the pair.
- The UI command path gave the same result.
- Moving J1 removed the stale display.
- After every check, preview and clear, the document still reported no unsaved changes.

### Limits

- Static check of one pose: no swept, continuous or path collision, and no minimum-clearance measurement.
- Only the model view shows the highlights and outlines; separate 3D View windows don't.
- After switching to another task panel, the out-of-date result panel is only reachable by checking again or
  clearing.
- FRust and node access are not added. The capability is available to the agent and the localhost API.

## Geometry rebuild performance repair — October 19, 2026

### Cause

Measured on the 44-feature robot-arm document: creating a box took 27 s and undo 14 s, all on the message
thread, so the viewport could not repaint.

1. `evaluate()` recursed into operands with no reuse, and `rebuild()` evaluated every item. Each union
   recomputed its whole operand chain, so the Boolean work per rebuild grew roughly with the square of the
   chain length. Boolean history was also collected repeatedly.
2. `mutate()` rebuilt, committed, then called `syncItemsFromOCAF()`. That re-reads the item definitions from
   OCAF (which drops the derived shapes, face names and sketch frames) and unconditionally rebuilt a second
   time.
3. Even with fixes 1 and 2, every change re-ran every Boolean in the document once: 18.6 s on a 50-feature
   test chain. That showed a need to reuse results across changes.

### Fix (all in `CadDocument.*`)

- **Per-rebuild pass:** `evaluationPass` maps feature id to shape and face origins. Each feature is evaluated
  once per pass. Only completed results are stored, so cycle detection is unchanged.
- **Cross-change cache:** `geometryCache` maps feature id to signature, shape, face origins and that feature's
  Boolean history.
  - **Signature:** the feature's exact definition (bit-exact numbers, operands, sketch id, target), the
    operands' signatures recursively, the full content of the sketches used (plane, offset, entities,
    constraints), and the face-sketch support's signature.
  - **Invalidation:** an upstream change alters every dependent signature, so dependents recompute. Equal-looking
    but different features never share results, because the key is the id plus the full signature.
  - **Hits:** a hit replays the cached Boolean history inside the pass, so OCAF TNaming still sees each operation
    exactly once.
  - **Lifetime:** entries for deleted features are pruned after each rebuild; `clear()` empties the cache. OCAF
    remains authoritative, and the cache holds only derived results.
- **No second rebuild after commit:** `syncItemsFromOCAF(error, &evaluated)` still reads every definition back
  from OCAF. When each one equals the just-evaluated item (same order and fields), it reuses that geometry and
  those face names instead of rebuilding. `exportJson` does the same. Undo, redo and open rebuild through the
  cache.
- **Boolean validation:** the check that runs inside `booleanOp()` before a Boolean is added now uses the
  cache read-only, so it no longer recomputes the operand chain.

### Results

| | Before | After |
|---|---|---|
| Robot arm (live, Debug), read state | 11 ms | 9 ms |
| Robot arm, create 10 mm box | 27 s | **0.43 s** |
| Robot arm, undo | 14 s | **0.31 s** |
| UI Box task Apply / Undo command | — | 0.48 s / 0.31 s |
| 50-feature 24-union chain (`--cad-rebuild-check`), create box | 18.6 s (after memo only) | **35 ms** |
| Same, undo | 18.9 s (after memo only) | **19 ms** |

Checks:
- `--cad-rebuild-check` (new): 8/8. Six dependent unions equal an independent OCCT fuse; an upstream edit
  (60 → 80 mm) updates all six unions (10750 mm³) while an independent box is unchanged; undo, redo and
  save/reopen pass.
- `--cad-face-check`: 19/19 (face-sketch attachment, dependent pocket, undo/redo, save/reopen).
- `--cad-export-check`: 29/29.

Live, on a copy at `build/perf-check/Robot Arm working copy.djo`: the box appeared and disappeared on undo,
and the screenshot showed the arm correct with no stale or duplicate geometry.

### Remaining

- Opening a document still evaluates every Boolean once with an empty cache: 11.8 s for the robot arm in Debug.
- Adding a new Boolean runs its fuse twice (validation, then rebuild).
- About 0.3–0.4 s per change remains, probably render-snapshot meshing and OCAF sync; not profiled.
- Evaluation is still on the message thread; moving it off was not part of this repair.

## Local automation API for paced demonstrations — October 18, 2026

Reference: `docs/LOCAL_AUTOMATION_API.md`. Code:
- `Source/LocalAgentApi.*` (server);
- `DjehutiCAD::runToolCommand` (the shared dispatcher used by the API and the BYOK agent);
- `CadWorkspacePanel::automation/automationState` (`cad_ui`);
- `Viewport3D::controlCamera`.

Client and checks are in `tools/cad_api/`: `client.py`, `step.py`, `inspect_state.py`, `smoke.py`,
`semantics_check.py`.

### Recovered state

An interrupted session left the work uncommitted on `feat/local-cad-api` (at main's tip), and I continued it
there. It had already:
- added the loopback server with discovery, a bearer token, request ids, and the `/v1/status`, `/v1/tools`,
  `/v1/state`, `/v1/tools/call` and `/v1/requests/{id}` endpoints;
- added the `cad_ui` tool (state, command, set, click, select, sketch begin/update/commit/cancel, camera,
  document);
- added camera control for the model view and the 3D views;
- replaced the agent path's "Operation scheduled in DjehutiCAD." with real synchronous results;
- added the Help item;
- added the client and smoke scripts.

That code was unbuilt and unverified.

### Completed in this pass

- **Execution model:** requests run one at a time, in order, on a single worker (`juce::ThreadPool(1)`). The
  worker hands each step to the message thread through `runToolCommand` and waits for its outcome.
  - Statuses: `queued`/`running` (202), `completed` (200), `failed` (409), `cancelled` (200).
  - `POST /v1/requests/{id}/cancel` cancels queued work. Started work answers 409.
  - Export now writes on the worker instead of the UI thread.
  - Shutdown cannot deadlock: `keepWaiting` turns false and queued jobs are dropped.
  - The agent path's 120 s limit now reports "pending, may still complete" instead of a false failure.
- **Real bug fixed:** OCAF string fields were read back as Latin-1 (`juce::String(const char*)`), so
  non-ASCII names typed in the UI or sent through the API were corrupted on the next rebuild. `readString`
  now decodes UTF-8. This affected every OCAF string, not just the API.
- Removed a duplicated `ui` branch in `runWorkspaceCommand`.
- Wrote the API doc that the Help item already pointed to.
- Added the semantics check and the one-step client helpers.

### Verification

The Debug build passed.

Live HTTP checks against the running app:
- `smoke.py`: 17/17. It covers:
  - discovery and auth (a bad token gets 401; unknown tools get 400);
  - Create Sketch through the task panel;
  - a rectangle preview held without geometry, then committed as four edges;
  - Apply for extrude and cut through all;
  - camera moves without a document revision change;
  - a UTF-8 rename;
  - request-id replay that does not repeat an undo;
  - a truthful modeling failure, refusal of an invalid field, and protection of an unsaved document;
  - native save and reopen.
- `semantics_check.py`: 10/10. A slow export answers 202; the next request stays queued and is cancelled;
  status shows the export still running; the process reports Responding during it; cancelling the running
  export gets 409; completion returns the real result (7944 triangles per part); the cancelled request never
  ran; the document revision is unchanged.
- Discovery file: a 64-character token and a loopback URL. It is removed on clean exit.

Observed on screen while driving `step.py` one call at a time:
- The held rectangle preview (55 × 35 mm label) stayed visible between calls.
- Commit, Finish Sketch and Extrude (Distance 8) via `set` and `apply` worked.
- After the camera call (yaw −30°, pitch 25°, distance 160) and selection, the selected body showed magenta
  with its task panel.
- A wrong id was refused ("Unknown model tree id.") with the revision unchanged. An out-of-range pitch was
  refused with the camera unchanged.
- 3D view 0's camera was set through `view: 0`.

### Limits

- One controlling client at a time; calls are serialised.
- No pick-by-screen-coordinate. Faces are reached through `cad_list_faces` and `cad_create_face_sketch`.
- File-chooser and alert dialogs aren't driven; use the `document` action.
- A started step cannot be cancelled.
- No named view presets: yaw and pitch are in degrees.
- Not exercised live: the BYOK agent path (needs a provider key), FRust and nodes. They reach `cad_ui` through
  `cad_workspace` action `ui`, which uses the same dispatcher.

## Z-up camera correction — October 18, 2026

Bill's screenshot showed the XY floor looking like a wall. The camera still used a Y-up look-at frame
and Y-axis elevation, although modeling, sketches and reference geometry use Z-up coordinates.

- The shared camera now orbits about world Z, uses Z as its up direction and starts/resets at a
  45-degree azimuth / 35.264-degree elevation isometric view. Rendering, picking and target dragging
  consume that same view matrix; no model transform or authored part placement was changed.
- An empty document starts at a useful millimetre scale (200 mm viewing distance, 50 mm reference
  radius), rather than the old rotating-cube scale that produced a 0.1 mm default floor grid.
- The single-worker Debug application build passed. Only `--cad-camera-check` was run: it passed and
  exited normally in 1.19 seconds. It now checks that world Z projects vertically upward with no roll,
  alongside the existing basis, handedness, target-centering and distance-preservation checks.
- Native visual verification is pending; passing the math check is not a claim about observed pixels.

Bill wants to WATCH a part being designed through the CAD UI. A generated model file loaded into the
app is not the requested demo. Desktop capture/click control previously failed; reading accessibility
text did work. Do not substitute a generated/imported part for interactive Sketch/Extrude/Cut actions.
Bill has stepped away and explicitly asked that the live demo WAIT until he returns. No part has been
created through the app UI by Codex in this demonstration, and no generated bracket was loaded.

## Workspace menus, Toolbox and reference guides — October 18, 2026

Bill authorized Codex to take over implementation temporarily while Claude's session allowance resets.
Face sketching and manufacturing export were already complete and merged; this assignment builds on them.
Read `docs/WORK_ORDER_WORKSPACE_TOOLS.md` for scope and `docs/WORKSPACE_TOOLS_AND_GUIDES.md` for behavior and automation.

- The shell now uses the existing CreationDock library. It owns workspace contents and a dockable Toolbox.
  File/Window/Settings/Account/Help are application menus. CAD, FRust, nodes and 3D Views own local menus,
  including Float/Dock; these remain with the content when undocked. Apply/Cancel stay in the task inspector.
- The Toolbox selects persistent Pointer/Box/Circle tools and Body/Face/Edge filters, plus the existing
  sketch drawing tools in sketch mode. Area selection operates on projected 3D geometry, including obscured
  items; enclosing/crossing semantics and Ctrl-add are documented. The primary item drives the inspector.
  Tree selection also updates the shared geometric selection. A Parts Library is a separate future feature.
- Right-click model views expose appropriate inspect/sketch/export/hide/delete actions plus view controls.
  Instance hide/delete is not invented; instance context actions remain inspect/export.
- A default XY world floor and optional world/local planes and joint guides use the modern rendering path.
  Local frames and joint parent/moving frames come from the same authored/solved transforms as the model.
  These are presentation geometry, excluded from picking, Fit All bounds, OCAF history and exports.
- Dock/hidden/floating state and guide visibility persist in CAD settings. Restored floating bounds are
  constrained to available displays. FRust autosave now belongs to CAD, with a read-only legacy fallback.
- Agent `cad_workspace`, FRust `djehuti_cad_workspace(args_json)` and the CAD Workspace node share dispatch.
  The Help text and guide document describe menus, tools, context actions and guide visibility.
- `--cad-workspace-check` is the new focused diagnostic. It constructs an isolated workspace without saving
  workspace preferences or FRust source and writes `build/cad-workspace-check.result.json`. It covers changed services
  and components; it does not establish native menu/pointer/OpenGL behavior by itself.

Validation:
- Final single-worker incremental Debug application build passed (68.44 seconds), with no compiler warning
  or error diagnostics in its log. Executable: `D:\DjehutiCAD\bin\Debug\djehuti_cad.exe`.
- The focused diagnostic passed all 17 checks; the entire process exited successfully in 1.56 seconds.
  Its report reached `componentShutdownComplete`. Logs and timing are under `build/cad-workspace-check.*`.
- The initial diagnostic produced 17 passing checks but failed its 30-second process limit at shutdown.
  Its entry point now uses JUCE's event loop and normal quit/cleanup instead of `std::exit` during
  `initialise`; the bounded rerun verified clean component teardown and process exit.
- A normal CAD test instance launched, but the Windows computer-use helper failed both initial capture
  and one refreshed retry with `foreground window did not report a process id`. Native menu interaction,
  mouse gestures and rendered guide appearance remain unverified in this session. The untouched test
  instance was stopped. No other application process was stopped.
- No existing modeling, mechanism, IK, export or DEWB regression diagnostic was run. No real DEWB–CAD
  acceptance claim follows from these workspace checks.
- `git diff --check` passed. Closeout uses feature branch `feat/workspace-tools-guides`, followed by a
  merge into `main` and push. Preserve the pre-existing untracked `tools/motion_peer/__pycache__/`.

The next inspection should be a brief native GUI check of these changed controls when computer use is
available. Do not automatically start a parts library, supplier catalog or another modeling phase.

## Manufacturing export: 3MF, binary STL and STEP — October 18, 2026

Reference: `docs/MANUFACTURING_EXPORT.md`. Code: `Source/CadExport.*` (planning, checks, writers),
`Source/CadExportDialog.*` (UI), `CadCommandService::planExport/runExport`.

### Behaviour

Entry points:
- File > Export... (3MF, STL, STEP).
- Right-click a visible solid body or an assembly instance in the tree > Export... (instances had no context
  menu before).
- Agent tool `cad_export`, FRust `djehuti_cad_export(args_json)` (via a hook like IK), and node
  "CAD Export" (`args json`). All use the same JSON and `DjehutiCAD::runExportCommand`.

Parts and placement:
- Default scope is the visible modeled assembly. The dialog lists those parts with their sizes; all ticked
  means the whole visible model.
- An instanced body is replaced by its instances (no duplicate source body). Sketches, hidden operands and
  presentation helpers are never exported.
- Placements are the authored reference configuration only. `CadDocument::instanceShapeTransform` is now
  shared with `CadMotionRuntime::publish`, so viewport and export apply each placement once. No current-pose
  option was added.

Threads:
- `planExport` runs on the message thread. It resolves parts and copies every source B-rep with
  `BRepBuilderAPI_Copy` (geometry included, no mesh).
- `runExport` uses only those copies, so the dialog runs it on a worker thread with progress and Cancel. OCCT
  meshing and STEP transfer stop through a `Message_ProgressIndicator`.
- Export never mutates or dirties the document.

Formats:
- **3MF:** lib3mf, millimetre unit, one mesh object per part (name = part name, part number = id), placement
  as the build-item transform, Title and Application metadata.
- **Binary STL:** OCCT `StlAPI_Writer` on the placed copy. Several parts give one file per part:
  `<base>-<part>.stl`.
- **STEP:** OCCT `STEPCAFControl_Writer` with `StepData_ConfParameters`: AP214 IS, millimetres, names on.
  It writes from an export-only XCAF document: one product per source body, one named component per part.

Mesh and checks:
- Mesh quality presets: Standard 0.05 mm / 15°, Fine 0.01 mm / 5°, Custom 0.001–1 mm / 0.5–45° (validated).
  `BRepMesh_IncrementalMesh` runs on the copies, so the display mesh is untouched.
- Checks: solid present; `BRepCheck_Analyzer`; positive volume; finite coordinates; welded closed 2-manifold
  with consistent orientation (every edge used twice, in opposite directions); positive signed volume; and,
  for 3MF, lib3mf `IsManifoldAndOriented`. Failures name the part.
- Collapsed triangles are omitted and reported. A mesh/solid volume deviation over 2 % is a warning. No
  geometry repair.

Files:
- Output goes through `*.partial-xxxxxxxx.*` siblings, then `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`.
- Cancel and failure delete the temporary files and keep existing files.
- Automation needs `overwrite: true` to replace a file. The dialog lists existing destinations, including
  every STL batch file, and asks once.

### Dependencies

- **OCCT 7.8.0:** the existing `deps/occt-build` was reconfigured with
  `BUILD_ADDITIONAL_TOOLKITS=TKDESTEP;TKDESTL;TKBinXCAF` (no module switches). Debug builds were added for
  TKXSBase, TKDE, TKHLR, TKService, TKV3d, TKVCAF, TKXCAF, TKBinXCAF, TKDESTL and TKDESTEP; building a static
  target does not build its dependencies. Release libraries for these were not built.
- **lib3mf 2.5.0:** the official prebuilt SDK `lib3mf_sdk_v2.5.0.zip`; SHA-256 checked against GitHub's
  digest.
  - Unpacked (not committed) at `deps/lib3mf-sdk`, pinned in `deps/LIB3MF_VERSION.txt`.
  - BSD-2 notice in `deps/LIB3MF_LICENSE.txt` and `THIRD_PARTY_NOTICES.md`.
  - CMake fails clearly if the SDK is missing, and copies `lib3mf.dll` next to `djehuti_cad.exe`.

### Verification

`djehuti_cad.exe --cad-export-check` (working directory `D:\DjehutiCAD`): 29/29 passed. Results in
`build/cad-export-check.result.json`, files in `build/export-check/`.

Representative part: a 60 × 40 × 10 mm plate with a 12 mm through-hole and an 8 mm boss (23874.34 mm³). Read
back with lib3mf, OCCT `RWStl` and `STEPCAFControl_Reader`:

| Format | Readback |
|---|---|
| 3MF | millimetre unit; one manifold object named "Bracket plate"; 60 × 40 × 15 mm; 23874.69 mm³ (400 triangles, Standard) |
| Binary STL | binary size identity holds; 60 × 40 × 15 mm at the authored position; 23874.69 mm³ |
| STEP | one solid, 23874.336 mm³ exact; 60 × 40 × 15 mm; AP214 schema; `SI_UNIT(.MILLI.,.METRE.)`; product name kept |

The volumes are well below the 25005 mm³ the part would have without the hole.

Assembly: Left and Right plate instances (the right one turned 90° about Z at y 80) plus a Clamp block.
- 3MF: three named parts and no duplicate source. The Right plate spans x −40..0, y 80..140.
- STEP: three solids, component names kept, bounds x −40..120 and y 0..140.
- STL batch: three named files.

Also checked:
- The document revision was unchanged by every export.
- Cancel kept an existing file and left no partial output.
- A missing folder, an existing file without overwrite, and a chordal tolerance of 0 were refused.

In the running app (`build/export-check/export-check.djo`):
- File > Export… listed the two plate instances and the Clamp block (3 parts, 160 × 140 × 15 mm).
- With the clamp unticked: 3MF, then STEP, then Fine STL (two files) were exported.
- Exporting again to the same STL names was refused. After the change to confirm-first, a re-export showed
  "Replace existing files?" with the path, and Replace saved it.
- Right-click Clamp block > Export… preselected only that part.

An independent Python zip/XML reading of `gui-plates.3mf` showed `unit="millimeter"`, objects "Left plate"
and "Right plate" with part numbers and the 90° transform. The STEP text has one "Bracket plate" PRODUCT used
by two NAUOs named Left plate and Right plate.

Not observed:
- No slicer is installed (PrusaSlicer, Cura, Orca and Bambu were not found). Microsoft 3D Builder exists, but
  access to it was declined. **There is no real slicer observation and no physical print.**
- Agent, FRust and node export paths were compiled but not run live. They share `runExportCommand` with the
  checked service path.

### Limits

- Reference configuration only; no current-pose export.
- STL has no units or names inside the file. 3MF carries no colours or materials. STEP carries no feature
  history, sketches, scripts, constraints, joints or simulation.
- No geometry repair: invalid solids or meshes stop the export with the part named.
- Planning copies the geometry on the message thread, so very large models pause the UI briefly before the
  worker starts.
- The File menu's existing Close item has no handler (pre-existing, untouched).

## Idle CPU cleanup and face-based sketching — October 18, 2026

### Idle CPU

Cause: `MotionLink::handleAsyncUpdate` (10 Hz) re-serialised the whole status board (status, mechanism,
lifecycle and IK JSON) every tick, about 600 ms of message-thread time per 10 s. It now builds a small
`BoardKey` from the link snapshot, `CadMotionRuntime::statusSerial()` (bumped by `solveNow` and `acceptIk`)
and the document revision, and rebuilds the board only when the key changes. Heartbeats, timeouts, deadlines,
write-stall detection, pending-state publication and reconnect are untouched; the motion protocol and DEWB
are unchanged.

Process CPU, % of one core, Debug, `ik_arm.djc` open, no input:

| State | Before | After (instrumented build) | Final build, 20 s samples |
|---|---|---|---|
| CAD Workspace visible | 6.6 % | 1.0 % | 1.5–1.6 % |
| Minimised | 6.1 % | 0.9 % | 1.2 % |

The remaining timers total about 1.5 ms/s. An IK Solve still rebuilds the board once (8–11 ms). After the
change an IK Solve on the arm still updated the model view.

### Face and edge selection

`CadSelection` (`Source/CadSelection.h`) is shared by the model view and every 3-D view: mode
(Body/Face/Edge buttons on the right of the workspace toolbar), hover, selected pick and a serial.
`Viewport3D::pick` uses the same instance matrices and runtime pose as rendering:
- The nearest surface triangle gives the instance (or feature), the feature and the B-rep face index.
- Edge mode takes the nearest edge polyline within 7 px whose depth is not behind the surface under the
  cursor.

Selection details:
- A click selects only when the mouse did not move (a drag still orbits). During a command task the views
  still pick bodies (`wantsBodyPick`).
- Hover is light cyan and the selection is magenta in every view; the body highlight is suppressed while a
  subshape is picked. Picking in one view never moves another view's camera.
- The right panel inspects the pick: persistent face name, planar or curved, area and normal, or the edge
  type and length. It offers "Create Sketch on this face" and "Clear selection".

### Face-attached sketches and persistent references

Face names come from construction history, not indices:
- Box faces: `box-N/top|bottom|left|right|front|back`.
- Cylinder faces: `<cylinder id>/top|bottom|side`.
- Extrude and cut faces: `<id>/start|end|side:<sketch entity>`, from `BRepPrimAPI_MakePrism` history.
- Booleans carry names through `Modified`/`IsDeleted`, and placements through
  `BRepBuilderAPI_Transform`.

A sketch stores `supportFeature` + `supportFace`; the support feature is a dependency of the sketch and of
everything built from it, so cycles are rejected by the evaluation stack.

Each rebuild resolves the token to exactly one planar face:
- The frame: N is the outward normal; U is the first placed feature axis that is not nearly parallel to N,
  projected into the plane; V = N × U; the origin is the feature's placement origin projected onto the plane.
- Dimension edits therefore move the sketch without flipping its axes.
- A missing, split or non-planar support is an error naming the sketch and face. The edit is rejected; the
  sketch is never silently reattached.

Extrudes follow +N; cuts from a face sketch go into the body (−N); Reverse flips both. Deleting a body
that supports a sketch is refused.

Persistence and loading:
- OCAF: each face sketch has a reference label at `1000 + sketch number` (support feature, kind `face`, token,
  sketch id) with a TNaming named shape of the resolved face. Document `formatVersion` is now 6.
- Older documents load unchanged; their legacy edge references remain.
- No simulation pose is stored in an attachment.

### Automation

| Surface | Faces | Face sketch |
|---|---|---|
| Agent tools | `cad_list_faces {feature}` | `cad_create_face_sketch {feature, face}` |
| FRust | `djehuti_cad_list_faces(feature)` | `djehuti_cad_create_face_sketch(feature, face)` |
| Nodes | "CAD List Faces" | "CAD Face Sketch" |

- All go through `CadCommandService`; `listFacesJson` returns index, name, planar, canSupportSketch, normal,
  centre and area.
- Help item 6a describes the workflow.

### Verification

`djehuti_cad.exe --cad-face-check` writes `build/cad-face-check.result.json`: 19/19 passed. It covers:
- the top-face frame and the pocket volume;
- the plate height 10 → 16, with the pocket floor following to z 12;
- undo/redo, and native save/reopen;
- a right-face frame (N +X, U +Y, V +Z);
- refusal of a 20 mm through-cut that removes a sketch's floor support, with state kept;
- the delete guard.

Observed in the running app:
- Plate top face: selected, then a sketch with a dimensioned rectangle, a 4 mm cut and height 16; the pocket
  followed. The pocket floor was picked as `cut-18/end` and highlighted in all three 3-D views.
- Edge mode: picked and inspected a box edge. Picking another edge in View 2 left View 2's camera unchanged
  and highlighted it in View 1 too.
- Right side face (`box-1/right`): a circle sketch extruded 10 mm along +X. Width 20 → 30 moved the
  cylinder with the face, and undo/redo moved it back and forth.
- Native save to `build\face-gui-check.djo` and reopen; width 40 then still moved it.
- Deleting the support box was refused ("Cannot delete a body that supports Sketch 2…").
- The cylinder's side was reported as curved and not sketchable.

### Limits

- Only planar faces with history names can carry sketches; curved faces are inspect-only.
- A topology change that deletes or splits the support face is refused. There is no reattachment UI yet:
  undo, or delete and recreate the sketch.
- Edges are selectable and inspectable but not persistently referenced.
- Face names exist only for faces created by box, cylinder, extrude, cut and the Boolean history mapped from
  them; other faces show "no persistent name".
- Sketch outlines draw with the authored feature placement; they are not posed by runtime instances.

## Document framing and idle viewport activity — October 18, 2026

Framing: each view frames the visible geometry once when it is created and when a document is opened or
created (`Viewport3D::requestFit`). The fit waits until the view has a size, the render snapshot belongs to
the requested document, and a runtime pose computed from that same document state (instance and revision)
has arrived. Render and pose snapshots now carry the document instance and revision for this. Each view keeps
its own viewing direction. User camera input cancels a pending fit, and edits, IK results, selection and
repaints never refit. Fit All and Reset View are unchanged. An empty document keeps a neutral view and fits
its first geometry.

Rendering: `setContinuousRepainting(false)`. `prepareFrame` publishes a frame packet and requests one GL
render only when the projection or view matrix, the published pose (mechanism, IK or DEWB), geometry,
highlights, visibility, size or GL context changed. Hidden views (other tab, minimised window) do nothing
and catch up when shown. Until JUCE has created the context, the view keeps requesting renders, because
JUCE creates it inside a requested render. A failed publish (all three ring slots in flight) stays dirty and
retries. The workspace and 3D Views timers poll at 60 Hz while frames change and drop to 10 Hz after one
idle second; camera input, new geometry and published poses bring them back. The frame-resource ring,
shader pipeline, fences and context lifecycle are unchanged.

Measured (Debug build, `ik_arm.djc` open, no input, 10 s samples, process CPU as % of one core):

| State | Before | After |
|---|---|---|
| CAD Workspace showing | 97.5 % | 7.3 % |
| 3D Views tab showing | not measured | 6.2 % |
| Window minimised | 43.8 % | 7.5 % |

The remainder is all on the message thread with no GL work. Its main contributor, the motion link's 10 Hz
status board, was fixed in the idle CPU cleanup above.

Observed in the running app: the arm and slider-crank examples framed correctly on open, in either order
and repeatedly; 3D views that were hidden at open time, and a newly added view, framed correctly when shown.
Orbit (View 1) and zoom (View 3) changed only their own view. A window resize kept user cameras. Fit All
refit every view in its own direction. An IK Solve and a short target drag on the slider-crank updated the
model view and all three 3D views. Not exercised: pan (the automation could not hold Shift or the right
button during a drag; it uses the same camera path as orbit), DPI change, and a live DEWB stream.

Defects caught during the pass and fixed before merge: a comment inserted mid-line in the viewport
constructor disabled `attachTo`, so no context was created; a document switch briefly fitted the previous
document's bounds; and a view without its first pose fitted unposed bounds.

## IK in-app verification pass — October 18, 2026

This pass supersedes the "NOT observed" visual limitations in the closeout below. Observed directly in the
running Debug app, by screenshots and real mouse/keyboard input:

- `ik_arm.djc`, Reach full pose: Solve changed the arm configuration; the parts stayed connected; Result read
  "Solved and shown: solved. Targets reached" (0.0013 mm, 0.0002 deg). The UI stayed responsive.
- Markers and dragging: green desired and magenta achieved triads, the blue drag plane and the yellow diamond
  were drawn. With dragging on, moving the diamond on XY and then XZ moved the target within the chosen
  plane (the other coordinate held), and the arm followed. A run of quick moves ended on the last location
  (desired 170.07, 131.62, 39.92 mm; Result 0.0017 mm, reached): no older result replaced it. Dragging on
  empty canvas still orbited the camera and changed nothing in the assembly.
- 3D Views: all three views showed the same posed arm with markers; orbiting View 1 left Views 2 and 3 as
  they were. Workspace layout (tree, canvas, inspector) unchanged.
- Authoring: a new unsaved task (three adjustable joints, position only, Z changed to 60 mm) solved
  (0.0004 mm). Save Task stored it (Assembly tree entry); Undo removed it and kept the values as an unsaved
  draft without moving the pose; Save Task again, Save As `build/ik-ui-check.djo`, and reopening it showed the
  task. Closing the slider-crank after preview solves gave no unsaved-changes prompt.
- `ik_slider_crank.djc`: Slide to 74.162 mm solved with the crank, rod and slider connected. Beyond crank
  hard limit returned limitRestricted ("Stopped at the hard bounds of Crank pivot", "does not prove the
  target is unreachable"); the pose stayed exactly as before; the Result listed the rejected candidate
  (10.66 mm, outside tolerance) separately from the shown pose (24.16 mm, not reached).

Defects found and fixed (one Debug build after the first two, one after the last two):

- Crash: choosing another entry in the IK Task list (and the Target, Endpoint on, Reference and Mode lists)
  rebuilt the panel inside the drop-down's own change callback, deleting it mid-call (access violation,
  reproduced once). The rebuild is now deferred until the callback returns; switching tasks and modes was
  rechecked without a crash.
- The message area kept saying "Solving the IK task" after a background Solve or drag finished. It now shows
  the outcome ("Targets reached (solved)..." or the failure) when the result is published.
- Reopening a `.djo` saved earlier in the same session failed ("Unable to open native CAD document"): OCAF
  keeps the saved document registered under `<file>.cbf`, the same name the loader staged the copy under.
  Opens now stage under a unique name. Rechecked: Save, then Open of the same file succeeded.
- The Result's "Shown pose" errors were computed for the task displayed before the panel switched, so a newly
  selected task could report another task's error (0.0000 mm, reached). They are now computed for the
  panel's own task (rechecked: 30.40 mm, not reached, before solving).

Checks: two incremental Debug builds of the DjehutiCAD target (log `build/build-ik-ui-pass.log`).
`--cad-ik-check` run once after the load fix with the examples folder passed explicitly: 18/18 passed. An
earlier launch of it without the path argument reported a failure loading the arm example with an empty
error; the cause was not investigated further.

Test-environment note: the Claude desktop window overlapped the right part of the screen and was masked from
screenshots, so clicks there went to it and looked like dead IK controls and a dead Fit All. With the CAD
window moved clear of it, those controls worked. This is not a DjehutiCAD defect.

Not changed or checked: the model view's initial framing after opening the arm examples is very close
(Fit All corrects it); the status bar reads "Mechanism: unsolved (last valid pose shown)" after a rejected
IK attempt, which is accurate but terse. FRust, node and agent IK adapters were not exercised in this pass,
and no DEWB connection was made.

## Current inverse-kinematics closeout — October 17, 2026

This section supersedes older statements that IK is future work. Claude left the feature implemented but
not built; Codex completed it under Bill's temporary implementation authorization. This is still mechanical
kinematics, not a force/contact/dynamics solver.

Implemented:

- OCAF-authored IK tasks with stable task/target IDs, multiple targets, adjustable driver IDs, explicit tool
  and reference frames, position/orientation/full-pose modes, and position/angle tolerances. Task edits are
  undoable. Engineering JSON remains format 5, mechanics schema 3; old mechanics schemas still load.
- IK targets extend the existing constraint solver. Dependent coordinates, loop closure, couplings,
  grounding, locks, and enforced hard limits remain constraints. Only selected eligible drivers are released;
  saved roles and unselected prescribed inputs do not change. Orientation uses a 3-D rotation residual.
- The contextual IK panel supports authored task selection/editing, capture-current, desired/achieved
  markers, a target-plane drag handle, Solve, Reset Preview, and per-target candidate versus displayed-pose
  errors. Preview affects the shared runtime pose, never the OCAF geometry, dirty flag, or undo history.
- Interactive requests use one bounded worker, one replaceable pending request, cancellation, and request,
  document-instance, revision, context, and authority guards. Closing/changing the task or editing a target
  invalidates pending results. The worker sleeps while idle. Explicit Solve uses the worker with a 2 s budget;
  dragging uses 200 ms. Local IK is refused while DEWB owns the runtime.
- `CadIkCommands` backs seven agent tools, the `djehuti_cad_ik` FRust hook, and the CAD Inverse Kinematics
  node. Agent descriptions and Help describe the schema. Synchronous automation receives an actual result;
  its timeout path no longer reads an output concurrently with a late callback.

Completion fixes: an unsaved task may be validated/solved without a persisted task ID; saved definitions
still require one. New requests cancel/supersede active and queued older requests. Document changes cancel
obsolete work. Failed IK attempts report that the last valid pose is shown. Numeric pose arrays reject
non-numeric values instead of silently treating them as zero. Solve no longer blocks the panel or resets its
scroll position while waiting.

Focused checks actually performed:

- One affected-target incremental Debug build using `cmake --build build --config Debug --target DjehutiCAD
  --parallel 1` succeeded. Output is in `build/build-ik-completion.log`; no regression suite or Release build
  was run. LLVM/OCCT installations were reused, not rebuilt.
- The opt-in `--cad-ik-check` ran through the real document, runtime, solver, and shared command code in a
  few seconds, under a 30 s process deadline. All 18 checks passed; result is
  `build/cad-ik-check.result.json`.
- `ik_arm.djc`: position-only and orientation-only targets solved with remaining freedom reported as
  `underconstrained`; full-pose solved. Each mode met its applicable tolerance. Unsaved-task preview and
  stored-task JSON solve succeeded. Preview preserved the authored mechanics, revision, and dirty state.
- Creating a task, undoing it, redoing it, and saving/reopening the native OCAF document preserved the complete
  task definitions. The generated native check file was removed after use.
- `ik_slider_crank.djc`: inverse target solved at physical crank angle 90 degrees (60 degrees from its bent
  reference) while satisfying the loop. The hard-limit target returned `limitRestricted`; the off-rail target
  returned `targetNotReached`. Both retained every displayed instance transform of the previous valid pose.
- The runtime's simulation-ownership guard rejected local IK without changing its published pose. This was
  a direct runtime check, not a live DEWB connection or a new named-pipe acceptance run.
- The rebuilt application launched. The desktop helper could read its accessibility tree, but screenshot
  capture failed with `FrameArrived timed out` and `window capture timed out` after recovery; indexed mouse
  input failed with `coordinate input geometry is unavailable`. The unchanged test instance was closed.
  Actual visual markers, target dragging, camera interaction during drag, and visual coherence across all
  views were NOT observed in this session. Do not report these as visually tested.
- FRust/node/agent adapters were source-reviewed and build-verified. The shared JSON solve command was run;
  an actual FRust program, node graph, and BYOK provider invocation were not run. Latest-request cancellation
  is source-reviewed/build-verified; no claim of a visual drag stress test is made.

Examples and reading:

- `examples/mechanisms/ik_arm.djc` — six-axis spatial arm, three target modes.
- `examples/mechanisms/ik_slider_crank.djc` — inverse slider target, hard-bound and off-rail failures.
- `docs/INVERSE_KINEMATICS.md` — workflow, frames/units, persistence, automation, and limits.
- File -> Open CAD Document, choose an example, then select its IK task in the Assembly tree or press IK.

Executable: `D:\DjehutiCAD\bin\Debug\djehuti_cad.exe`.

## Architecture

There is one authoritative editable CAD document. `CadDocument` owns Djehuti
semantic feature IDs, engineering parameters, units, visibility, dependencies,
sketch data, and persistent-reference metadata. OCCT evaluates the features,
performs solid operations, and owns OCAF naming/persistence infrastructure.
`CadCommandService` is the shared command boundary used by the CAD workspace,
FRust, node graphs, and the BYOK agent. Do not create a second editable model
or bypass this service for user-facing modeling operations.

Each successful modeling command is one OCAF transaction. Undo and redo use the
OCAF document history. Render snapshots and the C++ feature list are derived
projections rebuilt after commands, undo/redo, import, and native load; they
are not a second source of truth.

Native `.djo` documents use BinOcaf persistence. The application uses a `.cbf`
staging adapter while calling the OCCT save/open path. Existing `.djc` files
remain versioned engineering JSON interchange, currently format version 3.
JSON import/export preserves public IDs, relationships, parameters, sketch
constraints, and stored reference metadata. Feature and schema ownership stays
in `CadDocument`; UI and agent surfaces should consume the command service.

## Implemented CAD scope

Current primitives are boxes and cylinders. Boolean operations include union,
subtract, and intersect. Features support placement, dimensions, visibility,
rename, deletion, undo, redo, native save/load, and JSON export/import.

Supported datum-plane sketches are bounded to XY, XZ, and YZ planes with an
optional plane offset. Implemented sketch entities are lines, rectangles, and
circles. Rectangle creation supplies horizontal, vertical, coincident, and
driving distance constraints. Circle dimensions support radius and diameter.
The compact constraint controls expose horizontal, vertical, coincident,
fixed/origin, length, radius, and diameter commands with validation/status
feedback. Endpoint snapping uses an 8-pixel screen tolerance and creates
coincident constraints in the same line command transaction. Shift-click can
select a second entity for two-entity constraints.

Sketch dimensions and numeric entity position/center edits regenerate connected
geometry. Closed planar profiles can be extruded into real OCCT prism solids.
Sketch tools can be cut from an explicit solid target, including finite,
reverse, and through-all cuts.

## CAD workspace layout and workflows

`CadWorkspacePanel` is a conventional CAD workspace. The model tree (Origin,
Bodies, Sketches, Features) is on the left. The centre is either the embedded
model view (a `Viewport3D` with its own camera, fed from the shared snapshot) or
the active sketch canvas. A contextual task panel is on the right, and status
plus units sit along the bottom. Column splitters are stored as width fractions
in the existing settings file (`cadWorkspaceLayout`). The task panel builds
only the fields for the current selection or command. Its instruction text,
validation/result message, and accept/cancel buttons stay pinned outside the
scrolling field area.

- Sketch: Create Sketch, choose XY/XZ/YZ (plus offset), then Create Sketch.
  This opens sketch mode: a mode banner, the Select/Line/Rectangle/Circle/
  Constrain tools and a prominent Finish Sketch. The full-size canvas zooms
  with the wheel, pans with right/middle drag, has Fit Sketch and an adaptive
  grid, and shows a live preview with dimension readouts. Drawing tools stay
  active until Select or Esc; Esc first cancels only the current gesture.
  Selecting an entity shows its length or radius/diameter, its plane
  coordinates, and a separate constraint group with its own value field.
  Finish Sketch keeps the geometry and selects the sketch, whose panel offers
  Edit Sketch, Extrude and Cut Sketch (double-click also edits).
- Extrude / Cut Sketch: role slots for Profile (sketch) and Target (body),
  plus distance/depth, Reverse and Through all.
- Subtract / Union / Intersect: Target (body kept) and Tool (cutting body)
  slots, filled by picking in the tree or by clicking a body in the model view.
  `Viewport3D::pickBody` does a screen-space triangle test against the
  snapshot meshes, which now carry `featureId`. Target/Tool are highlighted
  green/red in both the view and the tree. Accept stays disabled until two
  different solids are chosen. A subtract that leaves the volume unchanged
  reports that the Tool does not intersect.

The root cause of Subtract appearing to do nothing was in the old UI.
Operands were typed IDs filled by "Use Selected as..." buttons, and selecting
a feature cleared the second operand. The refusal message went to the bottom
of a long scrolling form, and the workspace had no model view to show a
successful result. `CadDocument::booleanOp` now also rejects non-solid
operands and results with no solid (all callers share this path). Boolean
kernels report `IsDone`/`HasErrors` failures instead of producing empty shapes.

Verified on Debug, interactively, in the running application. That check
covered: plane choice, a rectangle drawn with live preview, Finish Sketch,
Extrude, an overlapping cylinder, Target/Tool picked in the model view,
Subtract showing a real hole, Undo restoring both sources, and the disabled
Subtract with an instruction while Tool was missing.

This is not a complete arbitrary sketch solver. The current behavior is a
bounded constraint/validation implementation, not general free-form constraint
solving. There is no arbitrary face-attached sketch workflow or general
topology-selection UI.

## Mechanical motion and the DEWB motion link

Authored mechanics live in `CadMechanics.h/.cpp` and are stored in OCAF as the
`kMechanicsContainerTag` JSON string. They go through the same transaction,
undo, redo, native save/load and engineering JSON path as sketches; `.djc` is
now `formatVersion` 4 with `mechanics`, and version 3 still loads. The model:

- Assembly instances: stable `inst-N` ids with their own identity, each
  showing a solid feature.
- Joints: `joint-N` ids, fixed/revolute/prismatic, parent instance or world,
  child instance, `parentToJoint` and `jointToChild` frames (mm plus a
  Hamilton wxyz quaternion), reference coordinate and limits (rad or m).
- The connection profile: signal mappings.

`world(child) = world(parent) × parentToJoint × motion(q − reference) ×
jointToChild`, with motion about or along the joint +Z axis, is evaluated from
reference placements every time and never accumulated. Graph validation
rejects missing references, a child with two parents, and cycles.
`interfaceRevision` and `profileRevision` advance on interface and profile
changes, but not on renames.

The runtime pose is in `CadMotionRuntime`: one shared snapshot of per-instance
rigid matrices, consumed by the workspace view and every 3D View. The matrix is
the instance world frame times the inverse of the body's authored placement
point, so tessellated meshes are never placed twice. A body that has instances
is drawn only through them. Rendering, picking (which returns instance ids),
highlighting and posed bounds/clipping all use the same matrices, and motion
never refits the camera. Preview coordinates and simulation coordinates are
runtime state only; they never touch OCAF, undo or the dirty flag.

The DEWB endpoint is `MotionLink`: a JUCE named pipe (server), JSON messages,
connection-thread parsing, a bounded writer thread, a single latest-state
handoff to the message thread, heartbeat and timeout liveness, revalidation
on profile changes, and a discovery file in
`%LOCALAPPDATA%\DjehutiCAD\MotionEndpoints`. The specification is
`docs/DEWB_DCAD_MOTION_INTERFACE_V1.md`, with schemas and fixtures in
`docs/motion_interface_v1/`, the DEWB brief in `docs/DEWB_MOTION_HANDOFF.md`,
and the protocol peer in `tools/motion_peer/dewb_motion_peer.py`.

The workspace has Instance, Joint and Motion Link commands, an Assembly tree
group, instance and joint panels (frames, reference, limits, preview, signal
mapping editor) and a link panel; the status bar shows link status. Shared
commands go through `CadCommandService` (create/update/remove
instance/joint/mapping). The agent has `cad_create_instance`,
`cad_create_joint`, `cad_update_joint`, `cad_set_joint_mapping`,
`cad_motion_preview`, `cad_motion_listen` and `cad_motion_status`; status is
read synchronously from a thread-safe board. FRust has `djehuti_cad_create_instance`, `djehuti_cad_create_joint`,
`djehuti_cad_set_joint_mapping`, `djehuti_cad_motion_preview` and
`djehuti_cad_motion_status`. The node editor has CAD Instance, CAD Joint and
CAD Joint Signal nodes, and Help has a Motion section.

Verified in the running Debug app on 2026-10-17 with the protocol peer:

- Four bodies were loaded from v3 JSON. Instances and a revolute (crank),
  fixed (housing) and prismatic (carriage, ±30 mm limits) joint were authored
  in the UI.
- Previewing the slide carried the housing and crank, and the crank preview
  rotated about the housing axis. Undo removed only the last authored joint,
  so previews add no undo entries.
- A unit mismatch was refused with a clear `unitMismatch` message.
- Over the real pipe: binding, `SynchronizeState`/`StateApplied`, streaming
  (visible motion), limit warnings, the Paused display, single step, 4× speed,
  a missing-joint frame rejected, reset to a new timeline, a stale-timeline
  frame rejected, an abrupt drop shown as Disconnected with the pose held, and
  reconnect with full revalidation.
- Clicking a moving instance selected it. The 3D Views showed the same pose.
  After streaming on a reopened document, Undo reported "Nothing to undo".
- Save and reopen of `.djo` kept instances, joints and mappings, and the link
  reopened as Stopped.

A bug found during the check, where frames were treated as synchronizations
and produced repeated `StateApplied`, was fixed and re-verified (exactly three
`StateApplied` in the full scenario).

### Motion link corrections and lifecycle control (2026-10-17)

- Ordering: `MotionLink` keeps per-simulation-session watermarks (timeline,
  sequence, `(simTimePs, microstep)`, and the last sync sequence). They
  survive same-session revalidation and are cleared only by a new `Hello` or
  a new `simulationId`. Both `SynchronizeState` and `StateFrame` are checked:
  - Older timelines are always rejected.
  - A newer timeline must begin with a sync.
  - Within a timeline, the sequence must increase and time must not regress.
  - A duplicate sync is never reapplied; DCAD re-acknowledges it with
    `duplicate: true` if it was already published.
- Invalidation: queued states and commands carry a binding epoch. The epoch
  advances on Hello, disconnect, binding accept/reject/invalidation,
  SignalInterface, contract change, and document replacement (the new
  `CadDocument::documentInstance()`). Undo/redo can restore old revision
  numbers but never revives old work. `publishPending()` rechecks validity
  and applies the pose under the link lock. `StateApplied` is sent only for
  the published sync.
- Transport: the writer is event-driven and reports send failures. DCAD
  closes the connection when a write takes more than 3 s or 64 messages back
  up; the failed session is invalidated and a fresh pipe instance is served.
  Reads have no deadline, so idle listeners stay healthy. Shutdown joins
  threads with bounded waits (4 s), and the pose is held on loss.
- Lifecycle: `LifecycleCommand`/`LifecycleResult` with `requestId`,
  negotiated by `lifecycleControl.v1`, one outstanding request at a time. The
  request times out after 5 s (30 s once accepted), and Retry resends the same
  request id. Results never change the pose or lifecycle; states do.
  - UI: a Simulation group in the Motion Link panel (Start/Resume, Pause,
    Stop Simulation, Reset, Single Step with an interval in ms).
  - Automation: agent `cad_motion_lifecycle`, FRust
    `djehuti_cad_motion_lifecycle`, the CAD Simulation Command node, and Help.
- Test hook: `DJEHUTI_MOTION_TEST_PUBLISH_DELAY_MS` delays publication so the
  pending-state boundary can be exercised (used by the peer `invalidate`
  scenario).
- Checked with the protocol peer and observed in the running Debug app:
  - Duplicate sync re-acked, not reapplied. `staleSequence`,
    `timeRegression` and `staleTimeline` refused (including while awaiting
    sync after revalidation).
  - A queued sync was discarded when the session was replaced (no
    `StateApplied`).
  - Stalled writer: connection closed within ~2 s, then a clean reconnect.
  - Start, Pause (lost result → timeout → Retry with the same id → the peer
    deduplicated), Single Step 10 ms executed once despite duplicate
    delivery, Reset → timeline 2, Stop → final state held, Start after Stop
    → timeline 3. Undo of a mapping revalidated profile revision 9.
  - "Nothing to undo" afterwards. The no-lifecycle peer streams, and the
    panel shows why controls are off.
  - Graceful exit while listening completes in seconds.
- The peer's lifecycle answers are a protocol stand-in, not DEWB simulator
  controls. FRust, node and agent lifecycle entry points were built but not
  run interactively.

The FRust, node and agent motion entry points compile but were not run
interactively, and there is no end-to-end DEWB run yet: the DEWB side is a
separate assignment. The next boundary is DEWB implementing
`docs/DEWB_MOTION_HANDOFF.md`, then a joint DEWB-to-DCAD run.

### Mechanism constraints and dependent motion (2026-10-17)

The mechanical model is now a constraint graph (`mechanics.schemaVersion` 2,
`.djc` `formatVersion` 5). The full reference is
`docs/MECHANISM_CONSTRAINTS.md`; the examples are
`examples/mechanisms/gear_pair.djc` and `examples/mechanisms/slider_crank.djc`.

- Model: instances can be `grounded`. Joints may form closed loops and
  several joints may meet at one instance; the single-parent/acyclic rule is
  gone. Each movable joint has a `role` (driver / dependent / locked, with
  `lockValue`) and `enforceLimits` (hard bounds; limits stay advisory
  otherwise). `couplings[]` are explicit gear (ratio + phase), linear,
  rack-and-pinion (pitch radius) and screw (lead per revolution). Instance
  placement is the reference-configuration world frame.
- Migration of schema 1: tree roots become grounded, joint children get their
  former chain-derived placement, all joints become drivers, so old documents
  behave as before.
- Solver: `Source/CadKinematics.h/.cpp`, `kin::solve(model, request)`. It
  builds a spanning forest from world plus grounded instances, turns the other
  joints into 6-row loop closures, adds coupling rows, and uses analytic
  screw Jacobians, column scaling, damped least-squares steps with OCCT
  `math_SVD`, step limits, continuation substeps from the last valid pose, and
  `math_Jacobi` rank/free-coordinate diagnostics. Verification is
  independent: position ≤ 1e-6 mm, angle ≤ 1e-9 rad, coupling ≤ 1e-9, drivers
  exact, finite values, hard bounds. Statuses: solved, underconstrained,
  singular (valid); conflicting, boundViolation, notConverged, invalidInput,
  cancelled (invalid). The residual-minimum test (Jᵀr ≈ 0 with large r)
  classifies unreachable inputs as `conflicting`.
- Runtime: `CadMotionRuntime` solves only when inputs or the document revision
  change. Failed solves keep the last valid pose. Preview is driver-only and
  reverts on failure. The status bar shows `Mechanism: …`. Joint axes are
  drawn in the views (amber revolute, cyan prismatic). New and Open (a new
  `documentInstance()`) reset the runtime to the reference configuration, so
  no driver value or pose carries over between documents with repeated ids.
- Motion link: mapped joints must be drivers (`mappingConflict`). An
  unsolvable state sends `LinkStatus` `mechanismUnsolved`, and an unsolvable
  `SynchronizeState` is not acknowledged. Status separates received from
  posed sequence/time. The wire contract is unchanged.
- UI: Grounded toggle and full origin + rotation (degrees, extrinsic XYZ) for
  grounded or jointless instances; joint role/lock/hard-bounds; Coupling
  command and panel (with units); mechanism status group.
- Automation: commands `setInstanceGrounded`, `setInstancePlacement`,
  `setJointRole`, `createCoupling`, `updateCoupling`, `removeCoupling`; agent
  `cad_set_instance_grounded`, `cad_set_instance_placement`,
  `cad_set_joint_role`, `cad_create_coupling`, `cad_remove_coupling`; FRust
  `djehuti_cad_set_grounded`, `djehuti_cad_set_instance_placement`,
  `djehuti_cad_set_joint_role`, `djehuti_cad_create_coupling`; nodes CAD
  Ground Instance, CAD Instance Placement, CAD Joint Role, CAD Coupling; Help
  items Mechanisms and Simulation. These compile; they were not run
  interactively.
- IK boundary: the solver assembles `Term`s (rows + Jacobian columns). IK
  now adds a pose-target term and reuses the same solver. See the current IK closeout below.

Checked in the running Debug app (2026-10-17), with driver values entered in
the joint panel's Preview, and dependent values read from each joint's
"Solved value" and the Mechanism status group:

- `gear_pair.djc` (ratio −2): Gear A axle at 30° gave Gear B axle −60.000°,
  and at −135° gave 270.000°. Status was solved (unknowns 1, rank 1, closure
  0 mm / 0 rad). Both axles stayed at their world pivots, and the markers
  turned in opposite directions as expected. All three 3D Views showed the
  same pose. Opening another file afterwards gave no unsaved-changes prompt
  (preview did not dirty the document).
- `slider_crank.djc` (r 30 mm, L 80 mm; slide measured from x = 110 mm):

  | Crank | Slide (expected) | Slide (observed) |
  |---|---|---|
  | 90° | −35.84 mm | −35.838 mm |
  | 180° | −60.00 mm | −60.000 mm |
  | −120° | −49.34 mm | −49.336 mm |
  | 30° | −5.44 mm | −5.436 mm |

  At 90° the pins were checked too: crank pin −112.024° (expected −112.02°),
  wrist pin 22.024° (expected 22.02°). Every value solved with closure
  0 mm / 0 rad (unknowns 3, equations 6, rank 3; the 3 redundant planar rows
  are reported as consistent). The rail stayed fixed and the slider moved
  only along its rail axis. After Fit All, the 3D Views showed the same
  pose as the model view.
- Defect found and fixed: opening `slider_crank.djc` after previewing the
  gear pair inherited the gear pair's runtime state (both files use
  `joint-9`, `inst-7`, `inst-8`). The crank came up at −135°, the gear pose
  was shown as "last valid", and the solve reported `boundViolation`.
  `CadMotionRuntime` now resets on a new `documentInstance()`. After a
  rebuild, the same sequence opened the slider-crank solved at its
  reference configuration.
- Not exercised interactively: the `conflicting` (residual-minimum)
  classification, couplings other than gear, the locked role, hard-bound
  rejection from a preview, a DEWB-driven mechanism over the pipe, and the
  automation and FRust/node entry points (compiled only).
- The forward-kinematics checks above predate the IK assignment. Its implementation and actual checks are recorded in the current IK closeout below.

## Rendering and views

The application provides three independent 3D view cameras by default, with
independent orbit, pan, zoom, Reset, and Fit All behavior. Render geometry is
published through immutable snapshots and the existing three-slot frame
resource ring into the modern OpenGL shader renderer.

Camera framing is scale-aware. Snapshot mesh bounds determine the model center
and radius; initial nonempty views and explicit Reset/Fit All use the viewport
aspect ratio, field of view, and a visible margin. Zoom is proportional and no
longer limited to the old cube-sized 12-unit ceiling. Near and far clipping are
derived from camera distance and scene radius. Ordinary geometry revisions
refresh bounds without refitting a manually adjusted camera.

The orbit view uses a rigid model-centered look-at matrix: normalized forward,
orthonormal right/up axes, determinant +1, and one consistent eye-based
translation with view-space pan. Geometry is not rescaled or moved to hide
camera problems.

JUCE paints each viewport's component overlay after `renderOpenGL()` and
leaves `GL_DEPTH_TEST` disabled. Depth testing used to be enabled only at
context creation, so parts were layered in draw order while orbiting and
seemed to swap places or turn around their own centers. `renderOpenGL()` now
restores depth testing, back-face culling and no blending every frame. Before
the fix this was reproduced in the GUI (a far box drawn over a near box), and
afterwards near/far occlusion was observed to be correct during horizontal and
vertical drags, with only the dragged view moving. The full mouse-orbit check
is left to Bill.

Snapshot tessellation uses OCCT surface-property normals at triangulation UV
coordinates, applies face reversal and surface-location transforms, and checks
triangle winding against the oriented normal. When UV normals are unavailable,
nondegenerate oriented triangle normals are used as a finite fallback;
degenerate triangles are omitted. Face boundaries remain separate while curved
surfaces receive varying normals.

## Approved application icon

The approved pictorial icon is a machined-metal gear housing / wireframe emblem
in the suite's cyan-violet family. The user explicitly rejected the earlier D
monogram and CAD-letter badge. The canonical raster master is:

`D:\DjehutiCAD\Resources\djehuti-cad-icon-master.png`

Derived transparent source sizes are in the same directory at 16, 32, 48, and
256 pixels. `CMakeLists.txt` uses JUCE's installed `ICON_SMALL` and `ICON_BIG`
arguments, which invoke JUCE's `juceaide winicon` path and embed the icon in
the Windows executable. Do not replace this artwork with a monogram, lettering,
or an unrelated generic symbol.

## Verification record and boundaries

The focused service workflow created a 120 x 80 x 8 mm mounting plate, cut
four 10 mm through-holes, changed the driving width to 140 mm, checked bounds
and volume, rejected an invalid edit without changing the valid model,
exercised OCAF undo/redo, reopened the native document, and checked the JSON
constraint value. That service diagnostic passed.

The explicit `--cad-camera-check` diagnostic calls the actual camera helper.
It passed the default orientation, the former level quarter-turn, and a
nonzero model target for unit axes, pairwise orthogonality, determinant +1,
target center/depth mapping, and distance preservation. The final focused
Debug build passed, and the approved executable icon was verified by extracting
the associated icon resource from the built EXE.

Mesh normal/winding behavior was source-reviewed and build-verified. There is
no existing direct mesh-orientation diagnostic path. Native pointer, keyboard,
orbit, lighting, and culling behavior have not been observed through automated
GUI access. The user has been manually trying the application; the reported
camera squashing, limited zoom, and backwards-normal corrections have landed,
but this handoff must not claim that automated GUI observation occurred.

Useful entry points:

- `Source/CadDocument.h/.cpp`: document, sketches, evaluation, snapshots,
  persistence, and schema behavior.
- `Source/CadWorkspacePanel.h/.cpp`: CAD workspace controls and sketch UI.
- `Source/Viewport3D.h/.cpp`: OpenGL views, camera math, frame packets, and
  snapshot upload.
- `Source/Main.cpp`: application startup and opt-in diagnostics.
- `Source/FrameResourceRing.h`: bounded frame-resource handoff.
- `--cad-foundation-check`: opt-in service/persistence diagnostic; do not run
  unless a future order explicitly requests it.
- `--cad-camera-check`: opt-in camera-helper diagnostic.

## Honest remaining limits

- The representative persistent reference remains the bounded
  `box-bottom-x-edge` semantic role.
- There is no arbitrary face-attached sketch system.
- There is no general topology-selection UI or broad persistent face/edge
  guarantee.
- Loft and sweep are not implemented. Parametric Revolve is implemented; see
  `docs/PARAMETRIC_REVOLVE.md` for the current GUI, API and verification record.
- Mesh and GUI behavior still need focused direct observation when the native
  application is available through an appropriate UI surface.
- Sketches are not drawn in the 3D model view; they appear only in the sketch
  canvas. Picking is body-level only (no face/edge selection).
- New primitives are created at the origin, and their placement is edited
  afterwards in the body panel. The sketch Esc behavior, constraint group, and
  splitter persistence were checked only by source review and build.
- Mechanisms support forward and local inverse kinematics. There is no
  contact, cams, belts or force/feedback co-simulation, and v1 has no
  interpolation between states. `conflicting` is a numerical finding near the
  last pose, not a proof of impossibility.
- Instance placement is editable only for grounded or jointless instances;
  jointed instances are placed by their joints. There is no slider or
  animated sweep for preview. Ctrl+Z in the model view did not undo (the
  toolbar Undo/Redo buttons work).
- The motion pipe uses JUCE named pipes with default security. JUCE exposes no
  `PIPE_REJECT_REMOTE_CLIENTS`, and it allocates a payload before the 1 MiB
  check. A JUCE server pipe silently accepts the next client after a
  disconnect, which is why liveness relies on heartbeats and the 3 s timeout.
  A force-killed DCAD leaves a stale discovery file.

## Working rules for future orders

1. Follow the authorized scope and do not add unrelated features or start the
   next phase without an explicit order.
2. Inspect the repository and established infrastructure before editing.
3. Keep changes small and coherent; avoid broad refactors and duplicate
   abstractions.
4. Conserve context and tokens. Keep narration, reads, and logs focused.
5. Use Windows, Debug, incremental affected-target builds, and single-core
   builds. Do not rebuild LLVM/OCCT unnecessarily or replace the toolchain.
6. Redirect build output to logs. Do not repeatedly poll timers, processes, or
   unchanged logs.
7. Use minimal focused verification. Do not run broad regression suites or
   exhaustive matrices without explicit authorization.
8. Distinguish source/build evidence from observed runtime behavior.
9. Fix root causes; do not weaken checks, disable rendering correctness, or
   substitute canned operations for real user workflows.
10. Preserve unrelated edits and other agents' work. Never reset/discard or
    blindly stage everything.
11. Never terminate global compiler/linker/MSBuild processes. Only stop a task
    process tree that you own when necessary.
12. Preserve application identity, credentials, settings, licensing,
    attribution, and unrelated behavior.
13. Maintain parity for changed user-facing functionality across UI, FRust,
    nodes, and agent surfaces through the shared command service where
    applicable.
14. Use the desktop Git workflow: feature branch, focused build/check,
    commit/push branch, fast-forward merge to `main`, push `main`, and report.
    Never develop directly on `main`; never force-push or rewrite history.
15. Reconcile stale instructions with current human orders, keeping corrections
    focused.
16. Discuss material architectural expansion before implementing it.
17. Treat this file as context only. Wait for the user's next explicit order.
18. Report delivered behavior, actual checks, limitations, executable path, and
    main merge/push status concisely. Do not make the user track commit hashes.
19. Stop when the authorized assignment is complete. Do not continue
    automatically.

## Transition

Codex temporarily completed Claude's interrupted IK assignment at Bill's explicit request. The user controls
which agent implements the next assignment; this handoff is context, not an automatic continuation order.
Do not start another feature, create a coding chat, or send cross-chat messages without the user's direction.
