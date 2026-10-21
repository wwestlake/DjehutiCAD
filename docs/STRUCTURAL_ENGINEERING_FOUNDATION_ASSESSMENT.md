# Structural Engineering Foundation Assessment

Read-only architecture assessment, 19 October 2026. No source was changed, built or executed for this report. It supports a decision on the
first structural-engineering implementation. Nothing here is an authorized work order.

## Summary

- DjehutiCAD has no physical materials, mass properties, loads, supports or structural analysis today.
- It does have the right foundations:
  - stable feature and instance identities;
  - OCAF transactions that make every edit one undo step;
  - persistent face names;
  - a geometry-signature cache;
  - a separate runtime layer for derived results (kinematic poses, interference reports).
- Recommended direction: add structural data as new, versioned document data beside the mechanics data. Keep every computed quantity (mass
  properties, section properties, analysis results) derived and outside OCAF. Introduce analysis through a solver interface whose first
  implementation is a small, dependency-free linear 3-D frame solver.
- Smallest practical first task: a **read-only geometry mass-property service** (volume, centroid, inertia tensor) for bodies and instances.
  It needs no document-schema change, so it can be verified against analytic boxes and cylinders.

## 1. Existing implementation findings

| Area | Status | Evidence |
|---|---|---|
| Physical materials, density | **None** | No `material` or `density` in document code. The only "materials" are the colour palette `CAD_Material_n` written to glTF (`CadExport.cpp:875-891`) and a node-editor field (`NodeCompiler.h:176`). |
| Volume | Implemented, used for checks only | `BRepGProp::VolumeProperties` in export validation (`CadExport.cpp:470-472`), interference volume (`CadInterference.cpp:32-34`), Boolean outcome messages (`CadWorkspacePanel.cpp`, `volumeOf`) and self-tests (`Main.cpp:138, 309, 431`). Not exposed to UI or API. |
| Area, face centroid, edge length | Implemented | `CadDocument::describeFace` gives area and `CentreOfMass` (`CadDocument.cpp:1142`); exposed by `cad_list_faces` as `areaMm2` and `centre` (`CadDocument.cpp:1389-1400`). Edge length: `CadDocument.cpp:1167-1169`. |
| Solid centroid, inertia, principal axes, cross sections | **None** | No `MatrixOfInertia`, `PrincipalProperties` or section code anywhere in `Source`. |
| Feature identity | Implemented, stable | `CadFeature::id`, e.g. `box-1` or `extrude-17`, from one document counter (`CadDocument.h:43-55`, `newId` at `CadDocument.cpp:310`). Ids survive edits, save and undo. |
| Persistent topology names | Implemented, partial | Face tokens such as `extrude-17/end` and `extrude-34/side:line-20`, from `faceOrigins` and TNaming history (`CadDocument.cpp:62-64, 298-303`). Fillet edges use persistent edge tokens (`CadFeature::edges`). Box-edge references are a prototype only (`makeRepresentativeReference`, `CadDocument.cpp:1386`). |
| Assembly identity and frames | Implemented | `CadMechanicalInstance {id, featureId, placement, grounded}`. `CadFrame` stores mm plus a unit quaternion (`CadMechanics.h:18-37`). Instance ids are independent of modeling history. `instanceShapeTransform` maps a body shape into an instance pose (`CadDocument.cpp:1197-1203`). |
| Joints, couplings, roles | Implemented (kinematic only) | `CadJoint`: fixed, revolute or prismatic; `parentToJoint` and `jointToChild` frames; limits; driver, dependent or locked roles (`CadMechanics.h:39-58`). Couplings (`:60-70`). No compliance, stiffness, friction or force data. |
| Kinematic solver | Implemented | `kin::solve(model, request, options)` → `SolveResult` (`CadKinematics.h:47-91`). Damped least squares built on OCCT `math_SVD` and `math_Jacobi` (`CadKinematics.cpp:6-9, 666-738`). No external linear-algebra dependency. |
| Runtime and derived state | Implemented pattern | `CadMotionRuntime` publishes poses (`shownInstanceWorld`) without writing to the document (`CadMotionRuntime.h:65-121`). The header comment states "solved coordinates and poses are derived at runtime and never written back" (`CadMechanics.h:9-12`). |
| Analysis abstraction | One precedent | `interference::check` returns a `CadInterferenceReport` stamped with `documentRevision`, `documentInstance` and `poseSerial` (`CadInterference.h:36-54`). The panel marks it stale when any of them changes (`CadWorkspacePanel.cpp:719, 2211, 2893-2906`). Results are never saved. This is the model to follow for analysis results. |
| Force or dynamics co-simulation | Explicitly out of scope today | DEWB interface v1: "Driven relationships only … no force/feedback co-simulation" (`docs/DEWB_DCAD_MOTION_INTERFACE_V1.md:357`). |
| UI and API for engineering properties | None for mass or materials | Agent and local-API tools (`DjehutiAgent.cpp:63-108`) cover modeling, assembly, motion, IK, faces and export. The body panel shows dimensions and placement only (`CadWorkspacePanel.cpp:1007-1050`). |

## 2. Persistence, transactions and rebuild (as implemented)

- **OCAF layout** (`CadDocument.cpp:47-49`), under `Main()`:
  - meta (tag 1);
  - features (2), one label per feature with `TDataStd` attributes and a `TNaming` shape at tag 30, plus generated, modified and deleted
    history at tags 31-33;
  - references (3), including face-support labels;
  - sketches (4), a single JSON string;
  - mechanics (5), a single JSON string, `CadMechanics::kSchemaVersion = 3`.
  - File: BinOcaf `.djo`. JSON export `formatVersion 6`, `units "mm"` (`CadDocument.cpp:1374-1381`).
- **Transactions:**
  - Every edit goes through `CadDocument::mutate` (`:316-327`): `NewCommand`, then the action, `rebuild()` and `syncOCAFFromItems`, then
    rewrite the sketch and mechanics JSON, then `CommitCommand`. Any failure restores the in-memory snapshot and aborts.
  - Undo and redo replay OCAF, then re-read sketches, features and mechanics (`:1384-1385`). The undo limit is 100 (`:308`).
- **Rebuild and caching:**
  - `rebuild()` evaluates every feature in order (`:1108-1129`).
  - `evaluate()` reuses `geometryCache` entries whose `geometrySignature` (definition plus dependencies) is unchanged (`:959-1104`).
  - These signatures are a ready-made invalidation key for derived properties.
- **Schema gate:** `mech::fromJson` refuses a newer schema (`CadMechanics.cpp:377-378`). There is no migration framework beyond this
  version check.
- **Units:**
  - Geometry and frames are in mm.
  - Joint coordinates and limits are stored in SI (rad, m) (`CadMechanics.h:52-56`).
  - The UI converts between the two.
  - A structural layer must pick one internal unit system explicitly.

## 3. Recommended boundaries and data ownership

| Layer | Owns | Persistence | Notes |
|---|---|---|---|
| Geometry (existing) | Features, sketches, shapes, face and edge names | OCAF (authoritative) | Unchanged. |
| Mechanics (existing) | Instances, joints, couplings, roles, IK tasks | OCAF JSON, schema v3 | Unchanged by stages 1-5. Connector compliance (stage 7) would be a schema bump. |
| **Materials (new)** | Material library entries: id, name, version, ρ, E, ν, G, yield, etc., with units and source. Assignments body→material, with an optional instance override. | New OCAF container (tag 6), JSON with its own `schemaVersion` | Edits go through `mutate`, so they get undo, redo and save for free. Assignments reference **feature ids** and **instance ids**, never shapes or face indices. |
| **Analysis model (new)** | Analysis cases: supports, loads, settings, idealization (beam members, section choice) | New OCAF container (tag 7), versioned JSON | Authored input, so it is undoable and saved. It references instance ids, joint ids, persistent face tokens and instance-frame points. |
| Mass and section properties (new, derived) | Volume, mass, centroid, inertia, principal axes; section A, Iy, Iz, centroid | Runtime cache keyed by the geometry signature plus material assignment | Never written to OCAF. |
| Solver (new) | `StructuralSolver` interface → result | None | Pure function of (analysis model, derived properties, poses). Runs off the message thread like IK (`requestIkAsync`). |
| Results (new, derived) | Displacements, rotations, reactions, member forces and stresses, diagnostics | Runtime only, stamped with document revision, instance and case revision (interference pattern) | Optional explicit "export results" later; never saved inside the authoritative model. |

**Referencing rules**

- Prefer the most stable handle available:
  1. instance id or joint id;
  2. feature id;
  3. persistent face token, e.g. `extrude-17/end`;
  4. a point or frame expressed in the instance frame.
- Never use `faceIndex` or `edgeIndex`, which are transient TopExp order (`CadDocument.h:83, 92`).
- Validate references on every rebuild. An unresolved reference makes the analysis case **invalid**; it is never silently re-attached.
  This mirrors the existing face-sketch rule (`CadDocument.cpp:1342-1351`).
- Analysis runs on the **reference pose** by default. An explicit "shown pose" option can reuse `CadMotionRuntime::shownInstanceWorld()`,
  as interference already does.

## 4. Reusable OCCT facilities

| Need | Facility | Library (already linked) |
|---|---|---|
| Volume, centroid, inertia tensor | `BRepGProp::VolumeProperties` → `GProp_GProps::Mass`, `CentreOfMass`, `MatrixOfInertia`, `MomentOfInertia` | TKTopAlgo, TKG3d |
| Principal moments and axes | `GProp_GProps::PrincipalProperties` → `GProp_PrincipalProps` | TKG3d |
| Assembly mass properties | `GProp_GProps::Add(other, density)` to combine transformed instance properties | TKG3d |
| Cross sections | `BRepAlgoAPI_Section`, or `BRepAlgoAPI_Common` with a thin slab, then `BRepGProp::SurfaceProperties` on the planar section face (area, centroid, area moments) | TKBO, TKTopAlgo |
| Distance and contact between parts | `BRepExtrema_DistShapeShape` | TKTopAlgo |
| Bounds, oriented bounds | `BRepBndLib`, `Bnd_Box` (in use); `Bnd_OBB` (available) | TKTopAlgo, TKMath |
| Shape validity before properties | `BRepCheck_Analyzer` (in use in export) | TKTopAlgo |
| Dense linear algebra | `math_Matrix`, `math_Gauss`, `math_SVD`, `math_Jacobi` (SVD and Jacobi already in use) | TKMath |
| Surface triangulation for display | `BRepMesh_IncrementalMesh` (in use) | TKMesh |

**Not available in the linked OCCT:** volume (tetrahedral) meshing and FE solvers. Continuum analysis (stage 8) will need a separate
dependency decision (for example an external mesher and solver behind the same solver interface). Torsion constant J is not available
from `GProp`; it needs either closed-form expressions for standard sections or a 2-D warping solve.

## 5. Risks

1. **OCAF transaction size.** One undo step per `mutate` holds because whole JSON containers are rewritten. Analysis cases with many loads
   stay small as JSON. Do not store meshes, results or large arrays in OCAF: they inflate undo memory (limit 100) and file size.
2. **Undo coupling.**
   - Material assignments and analysis cases must be restored in `undo` and `redo` exactly as mechanics are (`restoreMechanicsFromOCAF`).
     Otherwise they will desynchronize from geometry after an undo.
   - Each new container needs the same restore call in `undo`, `redo`, `load` and `clear`.
3. **Rebuild cost.** `mutate` calls `rebuild()` on every edit. Mass and section properties must be computed lazily and cached by geometry
   signature, never inside `mutate`, or every edit pays for `VolumeProperties` on all bodies.
4. **Reference fragility.**
   - Face tokens survive parameter edits but are rejected when a face is split or removed.
   - Side-face tokens on Boolean results currently have a known frame-orientation defect (Manual 02 engineering findings).
   - Loads and supports should prefer instance frames and joints, and treat face tokens as optional, validated attachments.
5. **Units.** Frames are in mm; joint values are in SI. A structural model mixing them silently is the largest correctness risk. Recommend:
   - SI internally in the solver (N, m, Pa, kg);
   - explicit conversion at the geometry boundary;
   - units stored with every material property.
6. **Serialization compatibility.**
   - New containers must be optional. Older `.djo` files without them load as "no materials, no analysis".
   - Newer files opened by older builds keep the existing behaviour: unknown OCAF labels are ignored, but a newer mechanics schema is
     refused (`CadMechanics.cpp:378`).
   - Keep structural data out of the mechanics JSON, so mechanics schema v3 and DEWB interface v1 stay unchanged.
7. **Assembly semantics.**
   - Joints are ideal kinematic constraints. A static analysis of a mechanism that is not fully constrained is singular.
   - The analysis model must state how each joint is treated (rigid, released DOF, or later compliant).
   - Locked and driver coordinates should be taken from an explicit pose, not inferred.
8. **Multiple instances of one body.** Mass belongs to instances, not bodies: the "Spare tool" case in Manual 04. Assembly totals must sum
   instances and exclude bodies that are only instanced.

## 6. Staged implementation sequence

Each stage is independently testable and leaves existing files and behaviour unchanged.

| Stage | Content | Testable milestone |
|---|---|---|
| 0 | Conventions note: unit system, reference handles, staleness rules, schema policy | Reviewed document; no code |
| 1 | **Mass-property service** (derived, unit density): volume, centroid, inertia and principal axes per body and per instance in world frame | Analytic box and cylinder values within tolerance; instance transform checks; no file change |
| 2 | **Materials**: versioned library plus assignments in OCAF container 6; mass via density; UI and API read and write | Assign and undo, redo, save, reopen round trip; old files open unchanged; mass = ρ·V |
| 3 | **Assembly mass properties** at reference and shown poses (reuse `shownInstanceWorld`) | Two-instance and moved-pose cases match hand calculation |
| 4 | **Section-property service** (derived): planar section at a frame, giving A, centroid, Iy, Iz, Iyz | Rectangle and hollow-rectangle sections vs closed form |
| 5 | **Analysis-case model** in OCAF container 7: beam members (endpoints in instance frames, section source, material), supports, point and distributed loads, settings; reference validation | Round trip, undo and redo; invalid-reference detection when geometry changes |
| 6 | **Solver interface plus built-in linear 3-D frame solver** (direct stiffness, 6 DOF per node, `math_Gauss` or a small sparse solver), async like IK | Cantilever, simply-supported and portal-frame benchmarks vs textbook; singular-model diagnostics |
| 7 | **Results and visualization**: stale-stamped result store, deformed-shape overlay through a pose-like snapshot, reaction and member-force tables, API access | Results go stale on any edit; overlay matches solver output |
| 8 | Future, each a separate decision: joint and connector compliance (mechanics schema v4), geometric nonlinearity and prestress (geometric stiffness, iteration), continuum FEA via external mesher and solver behind the same interface | Defined per stage |

## 7. First three development assignments (recommended order, not yet authorized)

1. **Mass-property service (read-only).**
   - Add a `CadMassProperties` computation (volume, centroid, inertia tensor, principal moments and axes) for a body's evaluated shape and
     for an instance via `instanceShapeTransform`. Unit density, mm units.
   - Cache it by geometry signature. Expose it read-only through one API tool and an informational section in the body and instance panels.
   - **Acceptance:** box, cylinder and Boolean-result benchmarks; two instances of one body give identical local and correctly transformed
     world values; no OCAF or schema change; existing tests unaffected.
2. **Material library and assignment.**
   - Versioned material records with explicit units, plus assignments by feature id (with optional instance override) in a new optional
     OCAF container. Edits through `mutate`; restore in undo, redo, load and clear.
   - UI to assign and view; API read and write; mass = density × volume from task 1.
   - **Acceptance:** undo, redo and save/reopen round trip; files without the container load unchanged; deleting a body removes or flags
     its assignment.
3. **Structural conventions and analysis-case schema (design plus validation only).**
   - Specify the SI-internal unit policy, the reference-handle rules and the analysis-case JSON schema (members, supports, loads, settings).
   - Implement only the data structure, its JSON round trip and its reference validation against a document, with no solver.
   - **Acceptance:** the schema document; round-trip and invalid-reference tests; no UI beyond what is needed to inspect a case.

The solver (stage 6) should not start until these three are accepted. Its benchmarks depend on correct mass, material and reference
handling.
