# Mechanism Constraints and Dependent Motion

DjehutiCAD solves the motion of user-authored mechanisms: you prescribe some joint coordinates (drivers),
and the solver computes the remaining (dependent) coordinates so that every physical connection and coupling
holds. This is forward kinematics on a general constraint graph. Inverse kinematics (targets, dragging) is a
later assignment that will reuse the same solver (see the last section).

Examples: [`examples/mechanisms/gear_pair.djc`](../examples/mechanisms/gear_pair.djc) and
[`examples/mechanisms/slider_crank.djc`](../examples/mechanisms/slider_crank.djc) (File → Open CAD Document).

## Mechanical graph versus placement hierarchy

* **Authored graph (authoritative, stored in OCAF):** instances, joints, couplings, coordinate roles and
  grounding. A joint connects a parent instance (or the world) to a child instance. Several joints may meet at
  one instance and the graph may contain closed loops (four-bar, slider-crank).
* **Internal spanning forest (solver only):** at each solve the solver walks the graph from the world and the
  grounded instances. Tree joints position bodies; every remaining joint becomes a 6-equation loop closure.
  The authored graph is never rewritten to fit a tree.
* Every joint keeps its explicit frames: `parentToJoint` (joint frame in the parent instance frame) and
  `jointToChild` (child frame in the joint frame at the reference coordinate). Motion is rotation about, or
  translation along, the joint's +Z axis. Frames are rigid in their instances, so joint origins and axes stay
  correct as connected parts move. Everything is 3-D; planar examples use the general implementation.
* **Linkage dimensions** are the distances between a body's joint frames. Editing a joint's origin or axis
  (joint panel → Joint frame at reference) changes the linkage and therefore the solved motion. Body geometry
  is only appearance; it does not create connections.

## Grounding, drivers, dependents and locks

* **Grounded** instances are fixed to the world at their placement. Joints whose parent is *World* also
  connect to ground. Nothing is grounded implicitly: an instance with no path to ground is reported as
  *ungrounded* (6 unconstrained rigid-body degrees of freedom) and is shown at its authored placement.
* Each movable joint has a **role**:
  * **driver** — a prescribed input (local preview, or a DEWB-mapped signal). The solver never changes it.
  * **dependent** — solved from the constraints.
  * **locked** — held at its lock value.
* Several drivers are allowed. Inputs that over-determine the mechanism are a failure; the solver never
  averages, adjusts drivers or breaks a connection to make a pose look plausible.
* **Free coordinates**: when the constraints leave dependent coordinates undetermined, the solve is valid but
  `underconstrained`; free coordinates keep their last values (continuation policy) and are listed by name.

## Relationships and units

| relationship | joints | equation | parameters |
|---|---|---|---|
| fixed joint | parent/child | relative placement constant | frames |
| revolute joint | parent/child | rotation about joint +Z only | frames, coordinate in rad |
| prismatic joint | parent/child | translation along joint +Z only | frames, coordinate in m |
| gear coupling | A, B revolute | (qB − refB) = ratio · (qA − refA) + phase | signed ratio; phase in rad (UI: degrees) |
| linear coupling | A, B prismatic | (qB − refB) = ratio · (qA − refA) + offset | signed ratio; offset in m (UI: mm) |
| rack and pinion | A revolute, B prismatic | (xB − refB) = r · (θA − refA) + offset | pitch radius r in m per rad (UI: mm) |
| screw | A revolute, B prismatic | (xB − refB) = lead/(2π) · (θA − refA) + offset | lead in m per revolution (UI: mm/rev) |

Couplings are always authored explicitly; touching geometry never creates one. Angles are unbounded (turns
accumulate). Joint limits are **advisory** (warnings, as in Motion Interface v1) unless *Enforce limits as hard
bounds* is set: then a prescribed value outside the bounds is refused and a solution that would leave them is
reported as `boundViolation`.

## Numerical method

* Unknowns: dependent joint coordinates. Equations: 6 per loop closure (position error / mechanism size,
  orientation error as a rotation vector in rad) and 1 per coupling (in rad, or m / mechanism size).
* Jacobians are analytic: each joint contributes its screw motion (`ω = axis`, `v = axis × (p − origin)` for
  revolute; `v = axis` for prismatic) to the bodies downstream of it in the spanning forest.
* Steps: damped Gauss-Newton (Levenberg-Marquardt with column scaling), each step solved with OCCT `math_SVD`
  (minimum-norm least squares). Steps are limited (0.5 rad, a quarter of the mechanism size) so an iteration
  cannot jump to another assembly branch.
* Continuation: driver changes larger than 0.2 rad (or 5 % of the mechanism size) are split into at most 64
  substeps from the last valid configuration. Substeps are not published and never advance simulation time.
* Bounds: 60 iterations per substep and a 250 ms budget per solve; a cancellation flag is checked every
  iteration. Solves run only when inputs or the authored model change.
* Diagnostics: the eigenvalues of JᵀJ (OCCT `math_Jacobi`) give the rank, the free coordinates
  (`unknowns − rank`), redundant equations (`equations − rank`; three per planar loop in 3-D, consistent) and
  singular configurations (rank lower than at the last valid solve).
* Verification (independent of the iteration): closure position ≤ 1e-6 mm, closure orientation ≤ 1e-9 rad,
  coupling ≤ 1e-9 rad or m, drivers held exactly, all values and transforms finite, hard bounds respected.

### Solve statuses

| status | valid | meaning |
|---|---|---|
| `solved` | yes | every check passed, no free coordinates |
| `underconstrained` | yes | valid, but listed coordinates are not determined (kept at last values) |
| `singular` | yes | valid, but the constraint rank dropped; motion through this pose is ambiguous |
| `conflicting` | no | the inputs and constraints cannot all hold; with dependents this is the numerical finding that the search reached a residual minimum near the last pose (not a proof of impossibility) |
| `boundViolation` | no | a dependent coordinate would leave its hard bounds |
| `notConverged` | no | iteration or time budget exhausted |
| `invalidInput` | no | malformed model or input (unknown joint, prescribing a dependent coordinate, non-finite value) |
| `cancelled` | no | superseded |

A failed solve keeps the last valid pose on screen and reports the offending loops/couplings; the status bar
says "unsolved (last valid pose shown)". The authored document is never changed by solving.

## Persistence and migration

The mechanics JSON is stored in OCAF (one undo history) and in engineering JSON (`formatVersion` 5,
`mechanics.schemaVersion` 2), with stable `inst-`, `joint-` and `coupling-` ids. New fields: instance
`grounded`; joint `role`, `lockValue`, `enforceLimits`; `couplings[]`.

Migration of schema 1 (independent joints, single-parent tree): every instance that was a tree root becomes
grounded at its placement, every joint child's placement becomes its former chain-derived reference pose, and
every movable joint becomes a driver — existing documents behave exactly as before. Formats 3 and 4 still load.

## Motion link (DEWB) behaviour

The wire protocol is unchanged. Mapped joints must be **drivers** (binding reports `mappingConflict`
otherwise). For each validated state DCAD treats the mapped coordinates as prescribed inputs, solves the
dependent coordinates and publishes one coherent pose. DEWB never computes CAD's dependent coordinates.

* A `SynchronizeState` that cannot be solved is **not** acknowledged; DCAD sends `LinkStatus` error
  `mechanismUnsolved` and keeps the last valid pose. DEWB must send a new `SynchronizeState` (higher
  `sequence`, or a new timeline) once its state changes.
* A failed `StateFrame` keeps the last valid pose and reports `mechanismUnsolved` (rate-limited). The link
  status separates the received sequence/time from the last *posed* sequence/time.

## Interfaces and automation

Shared commands (`CadCommandService`): `setInstanceGrounded`, `setInstancePlacement` (with orientation),
`setJointRole`, `createCoupling`, `updateCoupling`, `removeCoupling`, plus the existing instance/joint
commands. Agent tools: `cad_set_instance_grounded`, `cad_set_instance_placement`, `cad_set_joint_role`,
`cad_create_coupling`, `cad_remove_coupling`; `cad_motion_status` includes the mechanism solve report. FRust:
`djehuti_cad_set_grounded`, `djehuti_cad_set_instance_placement`, `djehuti_cad_set_joint_role`,
`djehuti_cad_create_coupling`. Node editor: CAD Ground Instance, CAD Instance Placement, CAD Joint Role, CAD
Coupling.

## Extension boundary for inverse kinematics

`CadKinematics.cpp` assembles a system of `Term`s (residual rows + Jacobian columns over the unknown joint
coordinates) on top of a configuration evaluator that provides body poses, joint world frames, twists and the
spanning-forest paths. Loop closures and couplings are the current terms. Inverse kinematics adds a pose-target
term (position and/or orientation of an instance frame) to the same system and reuses the scaling, damped SVD
steps, continuation, rank diagnostics and verification. No IK controls exist yet.
