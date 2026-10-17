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
  will add a pose-target term and reuse everything else. No IK exists.

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
- Inverse kinematics is future work. It is not implemented.

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
- Loft, sweep and revolve are not implemented.
- Mesh and GUI behavior still need focused direct observation when the native
  application is available through an appropriate UI surface.
- Sketches are not drawn in the 3D model view; they appear only in the sketch
  canvas. Picking is body-level only (no face/edge selection).
- New primitives are created at the origin, and their placement is edited
  afterwards in the body panel. The sketch Esc behavior, constraint group, and
  splitter persistence were checked only by source review and build.
- Mechanisms are forward kinematics only: no inverse kinematics, dragging,
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

This is the final Codex implementation closeout for this session. The
architect will write future orders, and the human will relay them to Claude
manually. No automatic Codex continuation, new coding chat, or cross-agent
messaging is authorized for the incoming agent.
