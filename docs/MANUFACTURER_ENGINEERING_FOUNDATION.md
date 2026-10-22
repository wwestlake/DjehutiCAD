# Manufacturer geometry and engineering references

This extension uses the existing `djehuti-component-library` version 1 records, typed `properties`, local library import/export, BRep snapshots, and OCAF insertion/save/undo. It does not change the native document format or introduce a solver.

## Usage

Import `manufacturer-reference-library.json` from the application executable directory using the existing Component Library import command/browser. The three records are catalog-only until their supplied original STEP files are attached using **Attach CAD**. Choose the matching file in the adjacent `manufacturer-foundation` directory. Then insert independent occurrences through the normal component browser.

The same operations are available through `cad_component`:

```json
{"action":"importFile","path":"D:\\DjehutiCAD\\Resources\\manufacturer-reference-library.json"}
{"action":"engineering","component":"motor-dc-pololu-2365","verifyAssets":true}
{"action":"attachModel","component":"motor-dc-pololu-2365","path":"D:\\DjehutiCAD\\Resources\\manufacturer-foundation\\mmgm-pm.step"}
```

`engineering` returns original claims, SI conversions, parameter readiness and optional asset verification. Its optional `path` overrides the asset directory after a package is moved. Import remembers the directory locally; exported packages should include their original assets. Existing document geometry and specification snapshots survive without that directory; opening original curves requires the assets.

## Data contract

`engineering.specification.schemaVersion = 1` is optional. Legacy `engineering` fields remain supported. The component ID, manufacturer and exact part number remain on the existing record; the specification supplies the selected variant, sources, curves and model parameter references.

Each structured property retains `id`, `label`, `quantity`, `value`, `unit`, `conditions`, `variant`, `provenance`, `verification` and source references. Optional tolerance uses the claim's unit. Conditions are text or typed numerical quantities. Numeric values must be finite and match an explicitly supported quantity/unit pair. Unsupported units fail with an error instead of being guessed. Original values are retained; SI values are returned by interpretation.

Provenance distinguishes manufacturer publication, manufacturer CAD extraction, third-party measurement, analytical derivation, assumption and unknown. Unknown values are null, never zero. Assumptions cannot be marked reviewed. Discrepancies reference separate retained claims and remain `needs_review`. A reviewed source means reviewed transcription, not an independently measured product guarantee.

Sources retain original filename, revision (including explicitly unspecified revisions), URL, relative package path and SHA-256. Hash verification detects missing/changed originals; it does not certify the truth of published specifications. The eight included assets are copies of the previously acquired originals, without re-downloading or editing them.

Curves retain original source references, axes, units, conditions and variant. Optional sampled points require matching dimensions and digitization notes. These examples retain original curves and the manufacturer's printed fit rather than inventing digitized data. Models name a bounded scope, parameter references and curve references. Availability of listed parameters never asserts solver validation or a complete dynamic model.

The initial checked analytical derivation is effective resistance = voltage/current, using reviewed manufacturer inputs at identical conditions. Its assumptions must be explicit. Adding other derivations or units requires extending the shared validator and its checks. This is a deliberately bounded interpreter, not automatic PDF extraction.

`EngineeringSpecification.h` and its focused checks are mirrored byte-for-byte in DEWB. The identical portable library provides these identities:

| Component | Shared ID | Important qualification |
|---|---|---|
| Pololu 2365 | `motor-dc-pololu-2365` | MP 6 V, 51.45:1, standard shaft, no encoder; nominal 420 rpm and fitted 390 rpm remain separate conflicting claims |
| Actuonix L12-50-100-6-P | `actuator-linear-actuonix-l12-50-100-6-p` | Five-wire P feedback interface, not the R RC variant; force-speed curve is not a current curve |
| Pololu 2130 | `driver-pololu-2130` | Carrier limits: approximately 1.2 A continuous/channel; TI bare-IC and paralleled ratings are not substituted |

## Import behavior and diagnosis

STEP uses OCCT's existing STEP/XCAF toolkits and a temporary transfer document. Imported geometry remains an exact placed compound/solid snapshot in the authoritative CAD OCAF document. Source hierarchy, names, definition references and local transforms are retained as metadata. Imported children are not converted into native parametric CAD assembly occurrences or joints. No approximate replacement geometry is generated.

The importer explicitly normalizes STEP units to millimetres, retains source unit declarations, checks topology and solid counts, and records stage durations/source hashes. Record mutation occurs only after full transfer, geometric validation and snapshot size validation. Self-contained STEP files and millimetre BRep files are supported, with 32 MB input/snapshot limits. External-file assemblies fail explicitly; export a self-contained STEP assembly. IGES is not available in this pinned OCCT configuration because TKDEIGES is absent; `.igs`/`.iges` attempts identify that limitation.

The earlier acquisition timeout was in a separate inspection process with a 90-second caller limit. In repeated Debug measurements the micro motor completed read/transfer/validity/conservative bounds/volume in 70.36 s. Tight bounds instead took 107.42 s (read 8.80 s; transfer complete 54.60 s; validity complete 75.16 s; tight bounds complete 102.23 s; volume complete 106.87 s). The old utility also performed per-face tight bounds and repeated validity checks. The file was slow, not stalled or invalid. The application importer avoids that inspection overhead. Conservative bounds are labelled as such and are not precision envelope measurements. Large imports are currently synchronous in the existing browser; stage logging and precise failures are available, but background UI import/cancellation is not introduced here.

Focused checks: `djehuti_cad.exe --cad-component-manufacturer-check` from the repository root. This headless mode imports the three original files, checks topology and volume against independent diagnostics, exercises OCAF insertion/undo/save/reopen with placement, and tests an inch-unit placed compound plus atomic failures. Native interactive GUI operation is a separate validation and was not performed for this assignment.

Validation status for this handoff: the completed first run passed library semantics, all three real STEP transfers, topology/solid counts, diagnostic volumes, source hashes, OCAF insertion/undo/save/reopen, atomic failure handling, and explicit IGES reporting. Its only failure was a test comparison that treated equivalent quaternion signs and JSON formatting as unequal; both assertions were corrected. The final rerun was intentionally stopped when the session time budget changed, so its exit status is incomplete and must not be reported as passed.
