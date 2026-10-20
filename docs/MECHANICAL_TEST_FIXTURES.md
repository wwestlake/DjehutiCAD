# Mechanical and materials test fixtures

Open **File → New Test Fixture**, **File → Open Test Fixture…**, or **Window → Test Fixtures**.
The Test Fixtures workspace docks/floats through CreationDock. Its tabs are independently named engineering
documents. Save each as a `.djfixture` file. Fixture changes never edit CAD features or their material assignments.

## Workflow

1. Save the source CAD model (`.djo` or the existing CAD JSON interchange format).
2. Create a fixture, give it a name, and choose **Add CAD source…**. Select a saved source file.
3. Choose a solid feature and **Add part**, or **Add assembly** for the complete saved mechanical system.
   Add further sources or repeated occurrences as needed. Assembly occurrences retain the source instances,
   joints, coordinate roles, couplings and reference configuration. Their root placement moves the whole system.
4. Select an occurrence in the inspector. Edit XYZ in mm and rotations in degrees; **Apply occurrence placement**.
5. Pick a face in the 3D view to select its occurrence, assembly component and persistent face token.
   The equipment point starts at that face's centre. Alternatively enter a source component ID and persistent
   face token explicitly (available from the existing face API). Points/directions use source-document axes;
   occurrence placement carries them into fixture-world coordinates.
6. Choose equipment: fixed or directional support, force, moment, pressure, distributed force, reference point,
   displacement/reaction/stress/strain/safety-factor request, or assembly joint lock. Enter applicable properties
   and **Save equipment**. Select a saved equipment entry to edit it, or **New item** for another definition.
   Fixed supports constrain all six DOFs; directional supports list `ux,uy,uz,rx,ry,rz`. Distributed loads require
   a named face. Force/moment/pressure units are converted by `CadEngineeringUnits` to N / N*m / Pa.
7. Source material properties are visible in the inspector. Choose a catalog material and **Substitute** for a
   fixture-only comparison. **Source materials → Substitute** clears the override. An assembly-occurrence
   substitution applies to every component in that occurrence and is identified in the inspector and results.
   To compare individual bodies independently, add part occurrences. Source stock and material assignments
   remain authoritative and are never rewritten by fixtures.
8. Set gravity (m/s²) in the inspector and apply it. Validate, select an analysis method and Run.
9. Save, duplicate for a variation, or open another fixture using the same CAD source. Fixtures have independent
   configurations, undo histories, substitutions and results. Duplication preserves the setup and clears results.

Supports use green markers, loads amber markers/direction lines, and instrumentation blue markers. Marker lengths
are for identification, not magnitude scaling. The geometry view shows the saved reference configuration. The
kinematic result table reports verified joint coordinates; it does not animate or publish a structural deformation.
The source navigation button opens the original in the ordinary CAD workspace, respecting unsaved CAD changes.
Editing there is a separate explicit CAD operation; save it and return to the fixture to review the source update.

## Ownership, persistence and revisions

`CadTestFixture` owns fixture setup and results; `CadFixtureService` dispatches the same mutations for GUI and
agent/local HTTP calls. `CadFixturesPanel` owns the CreationDock workspace and per-fixture editor tabs.

Authored fixture configuration is stored in an independent OCAF document (JSON UTF-8 attribute, label 1).
Each successful edit is one OCAF command. Undo/redo restore the complete setup. Source `CadDocument` objects
are private runtime resolutions exposed only through const access; their mutators are never called.
Fixtures introduce no alternate source-model feature/history system.

The versioned `djehuti-test-fixture` JSON envelope, schema 1, stores configuration and the last execution record.
It contains source paths, identities and revision digests, occurrence transforms, equipment, optional material
substitution snapshots, settings and result provenance. It contains no copied source geometry or feature history.
Source geometry is staged in TEMP for the existing native loader, so read-only source directories are supported.
Fixture file writes are atomic. Concurrent external edits are detected before replacing an already open file.

Each source reference pins the saved file's SHA-256 digest and persistent mechanics model ID. This conservative
revision test includes geometry, materials, joints and other authored changes. Unsaved source CAD changes are
outside the fixture's saved-model contract. Any saved-file change invalidates references/results until explicitly
reviewed and accepted. Missing source files or missing/ambiguous feature, instance, face or joint references make
the setup invalid; configurations still open and remain editable. The source relocation command preserves model
identity. Accepting a revision never reattaches broken references to different geometry.

Run records include a unique run ID, UTC timestamp, exact configuration/input stamp, pinned source revisions,
effective material snapshots with substitution flags, parameters, method, validation, status and numerical rows.
Changes to fixture configuration or source revisions mark previous results stale. The GUI suppresses stale
numerical tables. Failed/unsupported executions publish no partial numerical solution. OCAF undo changes authored
configuration; run records remain stamped derived data and are rechecked against the restored setup.

## Analysis availability and limits

| Method | Available calculation |
|---|---|
| `geometricProperties` | Existing validated OCCT service: solid volume, centroid, inertia integrals and principal axes, carried into fixture-world axes. |
| `massProperties` | Density × volume, centroid, per-component mass inertia and gravity self-weight; analytic box checks cover mm-to-SI conversion. Components add independently; this is an additive assembly inventory, not a Boolean union of overlapping bodies. |
| `kinematics` | Existing verified joint/loop/coupling solver, using source assembly relationships and fixture-owned joint locks. Coordinates are rad/m; root fixture placement is retained separately. |
| `linearStatic`, `modal`, `thermal`, `dynamics` | Setup retained; execution explicitly unsupported. No installed validated backend. |

These available methods do not calculate response to supports, forces, pressure, moments or contact. They do not
produce reactions, stress, strain, deflection, vibration or safety factors. Those measurement requests remain
explicitly unsupported. A reference-point request can report its fixture-world position under geometric/mass
methods; that is geometry, not a displacement response. Assembly joint locks are kinematic boundary conditions,
not load-transfer or joint-force models. Materials with missing density prevent mass calculations.
Joint locks apply to a connected revolute or prismatic joint; fixed joints have no adjustable coordinate.
Duplicate locks on the same joint in one occurrence are rejected, so conflicting boundary conditions never
silently replace one another.

The common equipment list and analysis `options` object leave extension points for contact, mounting models,
joint forces and load transfer. Schema upgrades must validate those new types; unknown equipment types are
currently rejected rather than silently ignored.

## Agent/local API

Use registered `cad_fixture` with `action`. All GUI edits call this same service.

- Management: `list`, `create` (`name`), `open` (`path`), `duplicate`, `rename`, `save`, `close`, `delete`.
  Subsequent operations identify `fixture`; save/open paths are absolute. Unsaved close needs `discard:true`,
  deletion needs `confirm:true`, existing file replacements need `overwrite:true`.
- References: `addSource` (`path`); `choices` lists solid features and source mechanics;
  `acceptSource` (`source`), `relocateSource` (`source,path`), `removeSource` (`item`).
- Occurrences: `addModel` (`definition:{source,kind:"part"|"assembly",feature?,name?,placement?}`);
  `updateModel` (`model,definition:{placement?,name?}`); `removeModel` (`item`).
  Placement is `{x,y,z,rx,ry,rz}` in mm/degrees. Repeated occurrences have independent IDs.
- Materials: `substituteMaterial` (`model,material`); material is a full `CadMaterial` JSON record from the
  material/catalog query API, or null to clear. The source material record is never modified.
- Equipment: `upsertEquipment` (`definition`), `removeEquipment` (`item`). Definitions carry `id?`, `kind`,
  `target` occurrence ID, `component?` source assembly-instance ID, `face?`, `pointMm?`, `direction?`, and applicable
  `magnitude/unit`, `fixed` DOFs or `joint/coordinate`. Returned IDs allow editing the same definition.
- Analysis: `settings` (`definition:{method?,gravity?,options?}`), `inspect`, `validate`, `run`, `undo`, `redo`.
  Inspect returns availability and source/reference problems. A successful request dispatch is distinct from
  the execution record's `status` (`completed`, `invalid`, `error`, `unsupported`) and its `stale` flag.
- `cad_workspace` can activate/float/dock `workspace:"fixtures"`.

## Focused diagnostics and next phase

`--cad-fixture-check` uses isolated files under `build/fixture-check/` and writes
`build/cad-fixture-check.result.json`. It covers independent fixtures, parts/assemblies, source protection,
placements, loads/supports/measurement definitions, substitution, numerical unit boundaries, persistence,
undo/redo, solver availability, invalidation and existing CAD operations.
`--cad-fixture-ui-check` exercises actual editor controls in a JUCE event loop and renders the editor to PNG under
`build/fixture-ui-check/`. Component rendering does not verify OpenGL pixels or Windows mouse/keyboard input.
Add `--interactive` to leave that isolated diagnostic window open for a desktop smoke test. Fixture diagnostics
can run alongside an ordinary CAD session without taking over its document. Close the diagnostic process after use.

Recommended next phase: a separately approved structural solver adapter with explicit model idealization,
element/mesh generation, boundary-condition mapping and independent benchmark verification. Decide whether the
first supported problem is a beam/frame idealization or continuum solids before selecting a mesher/solver.
Add fixture-scoped component contacts and joint load transfer only with a backend that can validate them.
Preserve the current result stamps and unsupported measurement states when introducing deformation/stress plots.
