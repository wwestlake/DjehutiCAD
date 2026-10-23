# Order 5 completion report â€” October 23, 2026

Professional surface modeling is implemented on baseline `0577600`. Implementation
commit: `f855c2e`; curved-geometry, validation and face-reference follow-up:
`5b8eb9b`. Periodic extension-limit guard and checks: `a5eb63a`.

| Capability | Delivered behavior |
|---|---|
| Surface Creation | Planar boundaries; open/closed profile extrusion and revolution; uncapped Sweep and Loft using the existing builders; editable dimensions, axes, paths, ordered sections, orientation/correspondence and normal reversal. |
| Trim and Extend | Boundary/intersection trimming with picked keep/remove regions and persistent face/UV anchors; distance-based rectangular U/V extension using analytic or eligible bounded-surface C1 continuation. |
| Sew and Solidify | Configurable-tolerance sewing into connected open/closed shells; unmatched boundary endpoints/lengths and nonmanifold diagnostics; separate validated closed-shell solidification. |
| Thicken | OCCT offset and boundary-wall construction of solids from faces/open shells; editable positive, negative and symmetric thickness; controlled invalid-offset/self-intersection failures. |

Six Modify-menu commands provide source selection, isolated Preview, Apply/Cancel,
inspector reopening and undoable deletion. Surfaces render from both sides; trim
supports region-point picking in model views. Surface-produced solids support
downstream solid modeling, including verified parametric Hole machining.

The six `cad_surface*` tools, FRust hosts and compiled CAD nodes call the same
`CadCommandService::modelingCommand` used by the GUI. APIs provide structured
CRUD, previews, reference inspection and topology diagnostics. OCAF remains
authoritative: existing transactions, Undo/Redo, dependency invalidation and
engineering topology history are retained. Native `.djo` and engineering `.djc`
JSON version 14 preserve definitions in the established feature `modeling` field
(OCAF child 28). Existing solid-feature command conventions are preserved.

Validation passed on the final Debug binary:

- **139/139 focused surface checks:** valid/invalid geometry, all creation
  methods, trimming, analytic and curved C1 extension, source-cache isolation,
  gap/nonmanifold detection, closed-shell conversion, all thickness directions,
  excessive thickness, periodic-extension limits, editing/rebuilds, Undo/Redo, native/JSON persistence,
  distinct face references, downstream Hole, actual FRust/node execution and
  GUI controls/selection callbacks.
- **274/274 existing checks:** modeling 83, replication 74, Hole 27, Chamfer 20,
  Revolve 38, Lathe 13 and face attachments 19.
- **30/30 live application checks:** shared GUI/API creation/editing, isolated
  previews, trim-anchor/source movement, exact solid STEP volumes, Undo/Redo,
  controlled failures, native reopening and deletion. Curved open-profile Sweep
  area also matched independent numerical integration.

**443/443 checks passed.** GUI checks were programmatic controls/callbacks and
live HTTP operations. **Actual native pointer/visual interaction and Release
builds were not verified.** The owned test instance was closed after confirming
its generated document was saved. Reproduce focused checks with
`bin/Debug/djehuti_cad.exe --cad-surface-check`; results are written to
`build/cad-surface-check.result.json`.

Limits: one connected profile chain (one closed contour for planar faces); Loft
sections must consistently be open or closed. Controlled Sweep twist remains
limited to straight Transport paths. Trim selects individual split-face regions;
curves must lie on the surface or intersecting geometry must divide it. Extend
supports rectangular U/V boundaries, not arbitrary trims/holes or closed-direction
continuation. Sew requires one connected shell at 1e-7â€“0.1 mm tolerance. Thicken
accepts faces/open shells and rejects unsupported or invalid offsets; closed
shells use Solidify. Topology changes that remove/split references require
reselection. No failing checks remain in the exercised scope.

Guides: [Surface modeling](SURFACE_MODELING.md),
[API/FRust/nodes/interchange](SURFACE_MODELING_API.md). Order 5 is complete;
no later modeling batch is authorized.
