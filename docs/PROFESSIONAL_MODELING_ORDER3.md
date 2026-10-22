# Order 3 completion report — October 22, 2026

Mirror, Linear Pattern and Circular Pattern are implemented in commit `2f7d1aa`.
All three are accessible in the Modify menu, editable from the feature inspector,
and support Preview, Apply/Cancel and normal GUI deletion.

| Tool | Delivered behavior |
|---|---|
| Mirror | Principal/custom/planar-face planes; editable sources; original inclusion; correct reflected solids; body and supported additive/subtractive feature modes. |
| Linear Pattern | One/two-direction grids, editable counts/spacings, reversed steps, principal/custom/straight-edge/face-normal directions, source inclusion and predictable indices. |
| Circular Pattern | Principal/custom/geometric axes, count, full/partial span, reverse, source inclusion; no duplicated 360° endpoint. |

`cad_mirror`, `cad_linear_pattern`, and `cad_circular_pattern` expose structured
CRUD, reference inspection and isolated preview. FRust hosts and CAD nodes use
the same CadCommandService commands. OCAF remains authoritative; transactions,
Undo/Redo, dependency invalidation, engineering history, native `.djo` and
version 12 `.djc` JSON are preserved. Automatic feature targets follow source
target reassignment. Hole/Cut repetition reuses source cutters, including
through-all across differing thicknesses. The shared FRust prelude's reserved
`type` parameter names were corrected so the actual script path compiles.

Validation passed:

- Debug build and **74/74** focused replication checks: analytic solids and
  material removal/addition, distribution, source edits, target rebinding,
  through-all across steps, references, Undo/Redo, native/JSON roundtrips,
  controlled failures, GUI controls/selection callbacks, actual FRust execution
  and compiled CAD-node execution.
- Existing tools: Hole **27/27**, Chamfer **20/20**, Revolve **38/38**, Lathe
  **13/13**, face attachments **19/19**.
- **22/22** live HTTP/GUI-control checks: creation/editing, previews, exact STEP
  volumes, rebuilding, Undo/Redo, native reopen, invalid-axis rollback and GUI/API
  deletion. Native pointer/visual interaction was **not verified**.

Limits: feature mode accepts one Hole, Cut, Boolean Subtract or Union. Other
solids use bodies or explicit add/remove mode; arbitrary feature-tree replay and
assembly-occurrence patterns are unsupported. Datum definitions use principal
or numeric references; no separate named datum subsystem was introduced.
Bodies mode can contain overlapping independent solids. Split/removed topology
can require reselection. Caps are 256 positions and 512 total body copies;
no-effect or failed material instances are rejected atomically.

Guides: [Mirror](MIRROR.md), [Linear Pattern](LINEAR_PATTERN.md),
[Circular Pattern](CIRCULAR_PATTERN.md), [API and interchange](PATTERN_API_AND_INTERCHANGE.md).
This batch is complete; later modeling tools require a new order.
