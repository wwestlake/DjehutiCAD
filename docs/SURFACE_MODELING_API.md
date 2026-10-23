# Surface modeling API and interchange

All commands use `{action,id?,parameters?}`. Actions are `create`, `get`, `update`,
`delete`, `preview`, and `references`. Updates merge top-level parameter keys;
nested reference objects and arrays replace in full. `references` takes `target`
and lists persistent model-edge references, as for Sweep. Face references come
from `cad_list_faces`. Source IDs remain stable through parameter editing.

| Tool / feature type | Parameters |
|---|---|
| `cad_surface` / `surface` | `method`: planar/extrude/revolve/sweep/loft. `profile`: sketch ID (except Loft). Extrude: signed `distance`, normalized world `direction:[x,y,z]`. Revolve: `axis:{type,origin?,direction?}`, `angleDegrees`, `reverse`. Sweep/Loft share the existing path, orientation and ordered-section parameters. |
| `cad_surface_trim` / `surface_trim` | `target`, optional persistent `face`, `tool:{sketch}` or `{feature,face?}`, world-space `seed:[x,y,z]`, `keep` boolean. |
| `cad_surface_extend` / `surface_extend` | `target`, optional `face`, positive `distance`, `sides:[uMin,uMax,vMin,vMax]` booleans. |
| `cad_surface_sew` / `surface_sew` | `sources`: 2–128 feature IDs or `{feature,face}` references; `tolerance`: 1e-7–0.1 mm (default 1e-5). |
| `cad_surface_solidify` / `surface_solidify` | `target`: one closed-shell feature ID. |
| `cad_surface_thicken` / `surface_thicken` | `target`, optional `face`, positive `thickness`, `direction`: positive/negative/symmetric. |

Revolve axis types are sketchU, sketchV, worldX, worldY, worldZ and custom; the
axis must lie in the profile plane. Sweep and Loft use their existing geometry
services with solid caps disabled. Surface creation also accepts `flipNormals`
(default false), which reverses the builder's profile-oriented surface normals.
See [advanced modeling parameters](ADVANCED_MODELING_API.md)
and the [surface guide](SURFACE_MODELING.md) for controls and geometric limits.
All dimensions are mm and angles are degrees.

```json
{"action":"create","parameters":{"method":"extrude","profile":"sketch-1","distance":20,"direction":[0,0,1]}}
```

Success returns `ok:true`, `id`, `type`, complete `parameters`, and `topology`.
Topology contains `faces`, `shells`, `solids`, `area_mm2`, `volume_mm3`, `closed`,
`unmatchedEdges`, `nonmanifoldEdges`, and `freeBoundaries` with world-space endpoints
and lengths in mm. Sewing does not imply solidification. An open sewn shell is
a successful result with unmatched boundaries, rather than a failed solid.
Trim returns a generated `regionReference:{face,u,v}` anchor in its parameters;
this follows the source face through placement/geometry changes. Changing the
seed, source or selected face regenerates the anchor. Preserve it when replaying
complete definitions through persistence or update commands.
`seed` records the last supplied world point; `regionReference` drives subsequent
rebuilds. After source movement, specify a new complete world point (or use the
GUI region picker) when changing region selection.

Preview executes the same create/update command on an isolated engineering
document; it adds `preview:true`, `committed:false` and `volume_mm3`. Create previews
do not return a live feature ID. Failures return `ok:false`, `code` and `error`;
the live revision, definitions, visibility and Undo history remain intact.

The corresponding FRust hosts are `djehuti_cad_surface`,
`djehuti_cad_surface_trim`, `djehuti_cad_surface_extend`,
`djehuti_cad_surface_sew`, `djehuti_cad_surface_solidify`, and
`djehuti_cad_surface_thicken`. Each accepts an argument JSON `String` and returns
a structured result JSON `String`. The CAD node palette provides one node per
command with `args json` input and `result` output. Hosts, nodes, GUI and LLM tools
all call `CadCommandService::modelingCommand`; there is no separate geometry path.

OCAF feature child 28 stores definitions. JSON engineering interchange is version
14, with these types in the existing `modeling` field. Native/JSON loading rebuilds
through the existing document evaluator. Older applications cannot evaluate the
new types. Transactions, history, Undo/Redo and dependency invalidation use the
same mechanisms as the solid tools.

Final Debug verification: 139 focused surface checks, 274 existing modeling
checks and 30 live application checks passed. Native pointer/visual interaction
and Release builds remain unverified. See the
[Order 5 completion report](PROFESSIONAL_MODELING_ORDER5.md).
