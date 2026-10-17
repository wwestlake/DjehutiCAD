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
reverse, and through-all cuts. Finish and Cancel controls are implemented;
Cancel discards only an unfinished gesture. The properties panel is vertically
scrollable so sketch and modeling actions remain reachable at the existing
window sizes.

Property inputs carry persistent captions (with mm units) that follow the
selection (`CadWorkspacePanel::updateCaptions`): box width/depth/height along
X/Y/Z, cylinder radius/height, box corner or cylinder base-center world
position, extrude distance/cut depth, sketch line length or circle radius,
and sketch entity positions in local plane coordinates. The shared dimension
field states when Add Constraint reads it differently (for example radius for
Apply, diameter for Add Constraint). A circle driven only by a diameter
constraint shows and edits its diameter, matching what Apply already did.
Verified by Debug build and a brief GUI check of line and circle captions.

This is not a complete arbitrary sketch solver. The current behavior is a
bounded constraint/validation implementation, not general free-form constraint
solving. There is no arbitrary face-attached sketch workflow or general
topology-selection UI.

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
- Assemblies, mechanisms, loft, sweep, revolve, and simulation integration are
  not implemented in this CAD foundation.
- Mesh and GUI behavior still need focused direct observation when the native
  application is available through an appropriate UI surface.

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

This is the final Codex implementation closeout for this session. The
architect will write future orders, and the human will relay them to Claude
manually. No automatic Codex continuation, new coding chat, or cross-agent
messaging is authorized for the incoming agent.
