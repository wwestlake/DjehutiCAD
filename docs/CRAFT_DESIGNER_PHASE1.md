# Craft Designer — Phase 1

Craft Designer is a second view of the application's existing CAD document. It uses `CadWorkspacePanel`, the shared `CadCommandService`, `CadMechanicalInstance` occurrences, and the existing OCAF transaction/persistence path. It does not introduce another document format, component database, or physics engine.

## Using the workspace

1. Open **Window > Craft Designer**. The separately dockable **Craft Stock** browser opens alongside it. Use its Workspace menu to float/dock it; **Window > Craft Stock browser** reopens it.
2. Search/filter a template. Enter actual dimensions in mm, choose a material, and enter placement in mm/degrees. **Insert independent piece** creates a new editable definition and occurrence every time.
3. Select a piece in the model tree or viewport, then **Edit selected piece** in Craft Stock. Apply dimensions, material, and placement with their respective buttons. Normal CAD placement controls also work.
4. **Linked copy of selected** explicitly creates an occurrence sharing the selected definition. Dimension/material edits affect every linked occurrence; placement changes affect just one. The editor displays the number of linked occurrences.
5. **Materials...** opens the existing materials manager. Choose **Density only** for a material with no elastic properties. Switch to **Isotropic elastic** only when the required elastic values are known. Reload the browser after editing application catalog materials.
6. **Save template defaults** edits only the local component-library template. **Duplicate template** creates a custom template with a new stable identity. Existing document pieces retain their snapshots.
7. Use normal Undo/Redo and File Save/Open (`.djo`). **New CAD Document** closes/clears the shared active document; reopening restores the construction. CAD and Craft Designer show the same model.

## Geometry and data contracts

- Eight initial templates cover Wood, Plastic, Cardboard, and Foam board. Four profiles are implemented: rectangular prism, solid rod, hollow tube, and rounded-end stick.
- Local length follows +X. Rectangular profiles occupy positive X/Y/Z. Rod/tube axes follow +X and are centered on Y/Z. Rounded sticks have semicircular ends of radius width/2, extruded along +Z.
- `geometry.kind = craftStock`, `schemaVersion = 1` extends existing component records. Dimensions are explicit mm values. Each document snapshot records `templateId`, `templateRevision`, `templateGeometry`, provenance, and `authoredRevision`; edits update the existing component definition through `CadDocument::mutate` and the component evaluator.
- General imported/manufacturer components retain their frozen geometry behavior. New craft insertions are independent; existing instance feature IDs represent explicit links. No live catalog subscription exists.
- Geometry must form a valid OCCT solid. Dimensions range from 0.01 to 100000 mm; tube wall thickness is at least 0.01 mm; rounded-stick length must exceed width by at least 0.01 mm.
- Density uses kg/m³. The four bundled craft materials are **illustrative nominal educational examples**, not measured or supplier product data: Wood 500, Plastic 1050, Cardboard 700, Foam board 100 kg/m³. The source and approximation are recorded in each engineering material. Use measured mass/volume or documented supplier values for actual stock. Foam board/cardboard solid geometry and bulk density do not resolve their internal layers or corrugations.
- `densityOnly` is an explicit material model requiring finite positive density and excluding elastic/strength fields. Existing `isotropic` requirements remain unchanged. Missing properties never become zero or invented defaults. Older application versions that do not understand this material model/profile will reject it.
- Material assignments in the document's existing material container are authoritative; embedded template material records describe insertion defaults/provenance. Assignment changes do not modify the source catalog.

## Verification entry points and limits

`--cad-craft-check` runs focused numerical/geometry/document checks in the existing application target, using isolated catalogs and writing `build/cad-craft-check.result.json`. It checks analytical profile volumes, the tube bore, material validation, independent/linked edits, rejected inputs, OCAF history, native/JSON reopen, and template persistence.

`--cad-craft-native --component-library <isolated JSON path> --engineering-catalog <isolated JSON path>` opens a diagnostic instance of the real workspace for native interaction without changing the user's saved workspace settings. It does not automatically configure a construction or invoke buttons. Close this agent-owned process after the smoke test.

Phase 1 does not implement cutting workflows, adhesives, bonded joints, craft structural loading, failure analysis, or educational kits. Existing fixture mass services can use density with true solid volume; the existing rectangular-Box cantilever solver is not a general craft analysis backend. Stock geometry is exact for its authored idealized profile; material densities and homogeneous-material assumptions are explicitly approximate.
