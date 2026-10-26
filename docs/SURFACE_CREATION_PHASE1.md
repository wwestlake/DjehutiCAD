# Surface creation — Phase 1

Implemented on `main`, October 26, 2026.

## Delivered

- **Planar Surface:** bounded faces from a rectangular, circular or arbitrary
  closed planar contour; selected sketch entities or a persistent model-edge wire.
- **Extruded Surface:** open/closed curves, signed distance and world direction,
  plus a source-sketch-normal shortcut. No caps or automatic solid conversion.
- **Revolved Surface:** open/closed curves, partial/full angle, sketch U/V,
  world/custom axes and selected straight sketch lines, including construction
  lines. Source and axis references drive regeneration.
- **Ruled Surface:** two compatible curves/wires with source-direction reversal
  and explicit compatibility/geometry errors.

The Surfaces menu and modeling toolbar expose all four commands. Expanded sketch
tree entries select individual curves; Edge mode selects connected model edges.
Source slots, numeric controls, isolated Preview, Apply and Cancel use the
existing modeling command service. After starting Preview, edits regenerate a
draft after a 250 ms pause. Source snapshots refresh viewport geometry and bounds.

Surfaces have a distinct tree group, double-sided display, normal selection and
Hide/Show controls. **Edit sources and parameters** updates the selected object
without changing its ID. Definitions, references and visibility use existing
OCAF transactions, Undo/Redo, dependency rebuilds and native/JSON persistence.
The existing `cad_surface` API/LLM tool, FRust host and CAD node share this path.

## Validation

Debug build passed. **56 focused checks passed, 0 failed**, using
`djehuti_cad.exe --cad-surface-creation-check`. The checks cover
geometry, controlled failures, edits, dependencies, stable IDs, deletion,
Undo/Redo, native `.djo` and JSON `.djc` save/reopen, and actual GUI form/selection
callbacks. **7 live loopback API checks passed, 0 failed**, exercising the running
application's timed preview, Apply, property editing and Cancel. Additional live
Planar/Revolved creation, editing, preview and Cancel smoke flows passed.
Build/check logs and generated
documents remain under `build/`.

GUI verification is programmatic. Native pointer/visual interaction and Release
builds are unverified. Broad modeling/advanced-surface suites were not run for
this creation-only order. The local Debug validation link disables dead-code
removal and identical-code folding to reduce linker memory pressure.

## Limits

- One connected, unbranched source wire per slot, with the existing 256-segment
  path limit. Planar Surface takes one boundary; nested hole loops are not added.
- Ruled inputs need equal segment counts and matching open/closed status. The
  tool does not automatically split or align incompatible curves.
- Individual sketch-entity and rotation-line picking uses the expanded tree;
  viewport sketch picking selects the whole sketch.
- No trimming, sewing, thickening or NURBS editing was developed in this phase.

See the [user guide](SURFACE_MODELING.md) and
[API reference](SURFACE_MODELING_API.md) for controls and parameter schemas.
