# Inverse Kinematics

IK solves the joint coordinates needed to place an engineering tool frame at a requested position,
orientation, or full pose. It uses the same mechanism solver as forward kinematics: joints, loop closure,
couplings, grounding, locks, and hard limits remain constraints. It does not solve forces, contact, or dynamics.

## Try the examples

1. Open `examples/mechanisms/ik_arm.djc` using File → Open CAD Document.
2. Press **IK** and choose a saved task: **Reach position**, **Match orientation**, or **Reach full pose**.
3. Press **Solve** in the contextual panel. The selected drivers move; the Result section reports target errors
   and whether the pose was applied. All views show the same mechanism pose, with independent cameras.
4. Enable **Drag the target handle in the model view**, choose an XY/XZ/YZ plane, and drag the yellow diamond.
   Dragging elsewhere continues to orbit the camera. Orientation-only targets have no position handle.
5. **Reset Preview** returns the mechanism to its authored reference coordinates.

The arm is a six-axis spatial chain with a tool offset and a bent reference pose. Its three tasks share a
goal generated from joint changes of 15°, 10°, −15°, 5°, 10°, and 20°. The target modes constrain only their
named quantities; position-only does not secretly demand an orientation, or vice versa.

`examples/mechanisms/ik_slider_crank.djc` is a closed-loop mechanism with a 30 mm crank and 80 mm rod.
It starts at a physical crank angle of 30°, away from dead centre. The first target places the wrist at
world X = 74.161984871 mm, requiring a physical crank angle of 90° (60° from the file's reference).
The other tasks request a position beyond the crank's enforced limits and a position off the slider rail.
Rejected attempts keep the previous valid assembly pose.

## Define a task

A task contains a name, stable saved identity, adjustable driver-joint IDs, target list, and position/angle
tolerances. New unsaved tasks can be solved before saving. **Save Task / Save Changes** stores the definition
as one undoable document transaction; the ordinary document Save then persists authored tasks to disk.
Editing the draft or moving its target does not save those changes automatically.

Each target specifies:

- the instance carrying the endpoint;
- a tool frame rigidly attached to that instance, independent of the body's mesh centre;
- a reference frame: World or another instance, evaluated in the same candidate configuration;
- a desired tool-frame pose expressed in that reference frame;
- position-only, orientation-only, or full-pose mode, and an enabled flag.

**Capture Current Pose** copies the displayed endpoint pose into the desired pose; it does not move the
machine. Disabled targets remain authored but contribute no solve equations. All enabled targets must meet
their applicable tolerances before a pose is applied.

Tool and desired origins use millimetres. UI rotations use extrinsic XYZ degrees. Internal frames use unit
quaternions; joint coordinates remain radians for revolute joints and metres for prismatic joints. Desired
orientations are solved with a three-dimensional rotation residual, not by subtracting Euler angles.

## What may move

Only explicitly selected, movable **driver** joints are released for an IK solve. Unselected drivers keep
their prescribed/current values; dependent joints continue to follow the mechanism. Locked/fixed joints and
grounded instances remain constrained. Saved joint roles are never rewritten by a preview.

Hard bounds participate in the search; a result is not clipped afterward and called successful. The local
search starts from the last valid pose to preserve continuity. Redundant tasks can leave remaining freedom;
the report identifies it. Singular poses, competing targets, and unreachable requests may defeat a local
search. A failed search is not proof that no solution exists elsewhere in configuration space.

## Results and interaction

The Result section separates the last attempted candidate from the pose actually shown. It reports each
target's desired/achieved frames, positional error in millimetres, angular error in degrees, reached state,
hard-bound joints, and solver diagnostics. `targetNotReached` and `limitRestricted` are failures, not
approximate successes. A candidate is published only after independent mechanical and target checks pass.

Desired and achieved frame markers show the gap. The selected position target has a yellow diamond on a
blue drag plane. The plane belongs to the chosen reference frame and passes through the desired point.

Solve and drag requests run on one sleeping worker with at most one active job and one replaceable pending
job. New requests cancel/supersede older ones. Changes to the target, task context, document, or pose authority
invalidate stale deliveries. Drag solves have a 200 ms budget; an explicit Solve has a 2 s budget. The solver
also has finite iteration/substep limits and cancellation checks. No work is done by this worker while idle.

## Document and DEWB boundaries

Task definitions are stored in the authoritative OCAF document and versioned engineering JSON
(`formatVersion` 5, mechanics schema 3). Schemas 1 and 2 continue to load without IK tasks. Creating, updating,
or deleting a task is undoable; preview solves and rendered motion create no undo entries, modify no B-rep
geometry, and do not dirty the engineering document.

While a bound DEWB motion link owns the runtime, local IK solves and target dragging are unavailable. Its
mapped driver coordinates remain prescribed. Task definitions may still be edited/saved. This feature does
not change the motion wire protocol or establish that the real DEWB/DCAD integration has been accepted.

## Shared automation, FRust, and nodes

`CadCommandService` owns authored task edits. `CadMotionRuntime` owns preview solving/publication.
`CadIkCommands` supplies the same JSON command implementation to the agent, FRust, and node surfaces.

| Agent tool | Shared command |
| --- | --- |
| `cad_ik_create_task` | `create` |
| `cad_ik_update_task` | `update` |
| `cad_ik_remove_task` | `remove` |
| `cad_ik_inspect` | `inspect` |
| `cad_ik_solve` | `solve` |
| `cad_ik_status` | `status` |
| `cad_ik_reset_preview` | `resetPreview` |

FRust uses `djehuti_cad_ik(command, arguments_json)` and receives the structured JSON result. The node editor's
CAD IK command node calls that same host function. Agent tool descriptions include the accepted schema.

Create/update arguments use `name`, `adjustableJoints`, `positionToleranceMm`, `angleToleranceDeg`, and
`targets`. A target uses `instance`, `referenceInstance` (`world` or an instance ID), `toolOrigin`,
`toolRotationDeg`, `desiredOrigin`, `desiredRotationDeg`, `mode`, `enabled`, and optionally `captureCurrent`.
Origins and rotations are numeric three-element arrays. Update/remove/inspect/status identify a saved task
with `task`. A saved-task solve may supply target overrides by target ID; those overrides are preview-only.
An ad hoc solve may provide a complete unsaved definition instead.

Successful solves return `ok: true`, `published: true`, and the full result/status. Rejected solves return
`ok: false`, `published: false`, and diagnostics, keeping the displayed pose unchanged. Inspection/status
also expose target errors of the shown pose, which can differ from a rejected candidate's errors.

## Focused diagnostic

The existing application target has an opt-in `--cad-ik-check` diagnostic. Run it from the repository root
after the affected Debug build; it reads the two examples above and writes `build/cad-ik-check.result.json`.
It covers the target modes, unsaved preview/shared command, document immutability, task transaction and native
save/reopen, runtime ownership guard, closed-loop inverse motion, and pose retention for the two rejected
slider targets. It is not a regression suite or a substitute for the recorded GUI check in the handoff.
