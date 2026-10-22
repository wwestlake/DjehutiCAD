# Chamfer

Choose a solid and **Modify > Chamfer**. Click successive edges in Edge mode to
add them; **Clear selected edges** starts again. Enter a positive Distance in millimetres. The same distance
is applied on the two faces adjoining each selected edge.

**Preview** validates and displays the proposed geometry without creating a live
feature, changing the document revision/modified state, or adding an Undo entry.
It uses an independent draft document and the same Chamfer command/evaluator as
Apply. Change an input, Clear preview or Cancel to restore the authored model.
Clear a displayed preview before picking different edges.

Apply commits one OCAF transaction. Select the resulting body to edit its distance
or choose **Edit edges** to display its source body and change the selection. The
source display is temporary; it does not alter saved visibility. If the document
changes while an edge-selection task is open, reopen Chamfer and select its edges
again. Invalid distances or unsuitable geometry return an explanation and preserve
the valid model.

## Agent and automation API

`cad_chamfer` supports `edges`, `create`, `get`, `update`, `preview` and `delete`:

```json
{"action":"edges","target":"box-1"}
{"action":"create","parameters":{"target":"box-1","edges":[0,2],"distance":2}}
{"action":"get","id":"chamfer-2"}
{"action":"update","id":"chamfer-2","parameters":{"distance":3}}
{"action":"preview","id":"chamfer-2","parameters":{"distance":4}}
{"action":"delete","id":"chamfer-2"}
```

`edges` returns the current zero-based indices, persistent references, curve kinds,
lengths and endpoints. Obtain fresh indices after source changes. Creation requires
a target and nonempty edge array; Distance defaults to 1 mm. An update keeps the
stored persistent edge references unless `parameters.edges` is supplied. Changing
the target requires new edge indices. `get` returns current `parameters` and the
stored `edgeReferences`.

Preview accepts creation parameters or an existing Chamfer id plus edits. It returns
`preview:true`, `committed:false`, parameters, edge references and `volume_mm3`.
New-feature previews do not publish a live feature id. API preview validates the
candidate; `cad_ui` → `chamferPreview` displays it in the GUI. Errors return
`ok:false`, `code` and `error`.

FRust: `djehuti_cad_chamfer(args_json: String) -> String`. The **CAD Chamfer** node
uses the same JSON contract. `cad_ui` command `chamfer` opens the visible tool;
its distance field is `chamferDistance`, and its preview buttons are
`chamferPreview` and `chamferClearPreview`. Edge picks use the shared CAD selection.

Chamfers retain their source dependency and persistent selected-edge tokens in
OCAF and engineering JSON. Source parameter edits rebuild them; missing or
ambiguous edges fail rather than silently choosing another edge. Distances,
selections, creation and deletion support Undo/Redo. Native `.djo` and `.djc`
roundtrips preserve the feature. Face history is carried through the operation,
with new bevel faces named by the Chamfer feature and selected-edge position.

This tool implements symmetric distance chamfers. Unequal-distance and
distance/angle chamfers are outside this order. Complex topology changes can require
edge reselection. The focused `--cad-chamfer-check` diagnostic records results in
`build/cad-chamfer-check.result.json`.

Verified October 22, 2026: Debug application build passed and the focused diagnostic
passed 20/20 checks. Coverage includes analytic volume, parameter/source edits,
multi-edge references, native/JSON persistence, invalid-geometry rollback, API
preview isolation from the live document and Undo history, and GUI preview/apply/
edit/cancel controls. Native pointer and visual inspection were not performed.
The final diagnostic also verifies successive ordinary edge picks retain both
edges. The live batch checks verified circular-edge geometry, GUI preview/edit,
downstream Hole rebuilding after a Lathe profile edit, and native save/reopen.
