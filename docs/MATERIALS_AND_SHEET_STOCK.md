# Engineering materials and sheet stock — Phase 1

Open **CAD Workspace → Edit → Materials and Sheet Stock...**, or the same button in a solid's property panel.
The manager has two scopes (Application catalog / Current document) and two browsers (Engineering materials / Sheet stock).
Select an entry to inspect it; use New, Edit, Duplicate, Save and Cancel edit. Bundled entries are read-only and can be duplicated.
Editing a shared definition changes it for every user of that definition; duplicate it to make an independent variant.

## Typical workflow

1. Create or duplicate an engineering material in the application catalog. Supply density (kg/m³), Young's modulus (MPa),
   Poisson's ratio, and optional yield strength (MPa), with family, alloy/specification, condition, source and notes.
2. Create sheet stock referencing that catalog material. Enter substrate thickness in mm. Finished coated thickness is a
   separate optional field, never the geometry thickness. Enter both sheet dimensions or neither.
3. If a gauge is recorded, also specify its gauge standard. Gauge is descriptive; it never overrides thickness.
4. Leave unknown bend radius and K-factor blank. A reference or shop-qualified K-factor requires a qualification source;
   those labels record user-supplied evidence, not an application certification.
5. Import stock into the document. Its engineering material is copied and remapped in the same undoable transaction.
   “Use as default stock” also selects the imported snapshot for future sheet-metal features.
6. For ordinary solids, choose a part in the manager's part selector, then assign a material. Catalog material assignment
   imports and assigns in one document transaction. The current assignment is visible in the manager and solid property panel.

Stock selection does not create, convert, or change any solid geometry. Sheet-part ownership and material/stock inheritance
through successive sheet features are reserved for Phase 2.

## Storage and identity

Bundled JSON is embedded from `Resources/engineering-reference.json`. Writable entries are saved atomically to
`%APPDATA%/DjehutiCAD/engineering-catalog.json` (JUCE's Windows user-application-data directory).
Catalog IDs are stable UUID-based strings; edits retain the ID and increment its revision. Bundled IDs remain stable across releases.
An explicit `--engineering-catalog ABSOLUTE-JSON-PATH` launch argument selects an alternate writable catalog, including
an isolated catalog for interactive validation. The normal launch uses the application-data location above.
Reload reads changes made outside this session; conflicting external writes are refused instead of overwritten.

Document material IDs retain the existing `mat-N` convention; local stock IDs use `stock-N`. Each explicit catalog import
creates independent local copies, recording the source catalog ID/revision. Imported definitions remain available offline,
and catalog edits never silently update them. Edit the document snapshot explicitly, or import another copy.

Catalog import/export uses a self-contained portable JSON format. Import preserves nonconflicting IDs, reuses identical
entries, and remaps conflicting identities and their stock references without overwriting existing entries.
Application catalog edits are saved outside CAD undo. Document edits, imports, assignments and default-stock selections
use the existing OCAF command/undo path.

Document stock data is optional OCAF main-label container **8**, with independent stock schema 1. Materials retain container
6 and now write material schema 2; schema 1 remains readable. JSON interchange includes optional `sheetStocks` data
(format version 7). Older documents without these containers load with empty libraries and no default stock.
Open, undo and redo restore materials and stock before evaluating geometry.

Deleting a material required by stock is refused, including when `clearAssignments` is requested. A default stock must be
cleared or changed before deletion. Existing explicit material-assignment clearing remains available.

## Automation

GUI actions and agent/local HTTP tools use the same `CadCommandService` and validators.

- `cad_material_list/create/update/duplicate/delete/assign/clear`: document materials and assignments. Metadata fields:
  `family`, `specification`, `condition`, `source`, `note`; numeric field names include their units.
- `cad_catalog`: `action` is `list`, `reload`, `createMaterial`, `updateMaterial`, `duplicateMaterial`, `deleteMaterial`,
  `createStock`, `updateStock`, `duplicateStock`, `deleteStock`, `useMaterial`, `useStock`, `importFile`, or `exportFile`.
  Edit fields are supplied in `definition`; identify entries with `material` or `stock`.
  `useMaterial` optionally takes `feature`; `useStock` takes `setDefault` (default true). Existing export destinations require `overwrite:true`.
- `cad_stock`: document-local `list`, `create`, `update`, `duplicate`, `delete`, `setDefault`. Empty `stock` clears the default.
  Stock definitions use `material`, `thicknessMm`, optional `finishedThicknessMm`, `sheetWidthMm`, `sheetLengthMm`,
  `gauge`, `gaugeStandard`, `finish`, `coating`, `insideRadiusMm`, `kFactor`, `formingMethod`, `qualification`,
  `formingSource`, `source`, `note`. Optional numeric fields may be null to clear them.
- `cad_workspace` with `action:"tool", tool:"materials"` opens the human manager.

Example catalog operations:

```json
{"action":"createMaterial","definition":{"name":"Shop aluminum","family":"Aluminum","specification":"Shop specification","condition":"Annealed","densityKgPerM3":2700,"youngsModulusMPa":69000,"poissonsRatio":0.33,"source":"Shop material record"}}
```

```json
{"action":"createStock","definition":{"name":"Shop 1.5 mm sheet","material":"RETURNED-CATALOG-MATERIAL-ID","thicknessMm":1.5,"sheetWidthMm":1000,"sheetLengthMm":2000}}
```

No manufacturing K-factor, bend radius, allowance or deduction is inferred from alloy or gauge. The forming method is tagged
as `kFactor`; later allowance/deduction methods can extend this schema without replacing stock identity or references.

## Bundled references

The five material templates cite supplier property guides for A36, 304 annealed, 6061-T6, C110 annealed and C260 soft annealed.
Density and modulus are converted from the supplier's published units, retaining their limited precision; yield strengths
are deliberately unspecified. The stock reference cites a supplier's nominal 6061-T6 sheet dimensions and mill finish.
Its K-factor, bend radius, gauge and coated thickness are intentionally unspecified. Production thickness and forming data
must be supplied for the actual stock/process. Sources and access date are retained with each entry.

## Focused validation

`djehuti_cad.exe --cad-stock-check` writes `build/cad-stock-check.result.json`; it uses isolated catalogs under
`build/stock-check/`, never the user's live catalog. It exercises data/command validation, referential integrity,
atomic imports, revisions, offline native/JSON round trips, undo/redo, legacy loading and catalog conflict remapping.
Existing `--cad-material-check` covers the preserved material command behavior. A separate visual application smoke test
is necessary to verify the interface; command diagnostics alone do not establish that the GUI works.
`--cad-stock-ui-check` uses an active JUCE event loop and an isolated manager dialog. It exercises the actual editor,
choice and button handlers, checks that agent queries see GUI-authored data, and renders the actual components to PNGs
under `build/stock-ui-check/`. Its report is `build/cad-stock-ui-check.result.json`. Inspect those images for layout QA;
this diagnostic does not establish that Windows mouse/keyboard input or desktop screen capture works.
