# Parametric Revolve

Revolve creates exact OCCT solids from closed sketches. It supports full 360-degree
and partial revolutions, reverse rotation, nested profiles with holes, editable
parameters, and dependent rebuilding through the existing OCAF document history.

## Create and edit in the GUI

1. Finish a closed sketch. Lines, rectangles, circles and cubic Bézier edges can form
   the profile. Keep its material on one side of the intended axis; an edge may lie
   on the axis.
2. Select the sketch and choose **Modify > Revolve**, **Revolve** in its task panel,
   or **Revolve** in its context menu.
3. Choose the Profile and Axis, enter an Angle greater than 0 and no greater than
   360 degrees, optionally enable Reverse rotation, and apply.
4. Select the resulting body in the model tree to edit its profile, axis, angle or
   reverse setting. Apply changes. Edit the source sketch to change the section.

Axis choices:

| Choice | Meaning |
|---|---|
| Sketch U / Sketch V | Axis through the sketch origin, along its local U/V direction. Follows offsets and attached-face changes. |
| World X / Y / Z | Axis through the world origin along the selected world direction. |
| Custom | World-coordinate origin in millimetres and a nonzero world direction vector. The vector is normalized. |

The axis must lie in the sketch plane. Rotation follows the right-hand rule about
its direction; Reverse rotation changes the direction. A full turn has no angular
end caps. Partial turns have planar start/end caps. Planar, persistently named faces
can support subsequent face sketches through the normal face-selection workflow.

Undo/Redo includes creation, parameter edits, sketch edits and deletion. Deletion
is refused when another feature or assembly instance still depends on the body.
The source sketch remains part of the document and is hidden when Revolve uses it.

## Agent and local HTTP API

The discoverable tool is **`cad_revolve`**. The local HTTP API and the BYOK agent use
the same tool catalogue and `CadCommandService` as the GUI. There is no separate
geometry evaluator for agents.

Create (returns the stable `revolve-*` identifier):

```json
{"action":"create","parameters":{"sketch":"sketch-1","axis":{"type":"sketchV"},"angleDegrees":360,"reverse":false}}
```

Inspect, partially update, or delete:

```json
{"action":"get","id":"revolve-17"}
{"action":"update","id":"revolve-17","parameters":{"angleDegrees":120,"reverse":true}}
{"action":"update","id":"revolve-17","parameters":{"axis":{"type":"custom","origin":[5,0,0],"direction":[0,1,0]}}}
{"action":"delete","id":"revolve-17"}
```

`create` requires `parameters.sketch`. Defaults are `axis.type: "sketchV"`,
`angleDegrees: 360` and `reverse: false`. Axis type values are `sketchU`, `sketchV`,
`worldX`, `worldY`, `worldZ` and `custom`. Origin/direction arrays have exactly three
finite numbers. `update` merges supplied fields with the existing parameters;
`parameters.sketch` can replace the source profile, subject to dependency validation.

Successful creation, inspection and modification return
`{ok:true,id,type:"revolve",parameters:{sketch,axis,angleDegrees,reverse}}`.
Deletion returns `{ok:true,id,deleted:true}`. Failures return
`{ok:false,code,error}`. Codes distinguish invalid arguments, missing features,
modeling failures and refused dependent deletion. HTTP/agent results also include
the current workspace `state`, including the parameters of each Revolve feature.
Inspect failures before resubmitting a mutation.

FRust exposes `djehuti_cad_revolve(args_json: String) -> String`. The **CAD Revolve**
node accepts `args json` and returns `result` JSON, using the same action contract.

## Persistence and validation

Native `.djo` stores the feature and parameters in OCAF. Engineering `.djc` JSON
version 9 includes the optional `revolve` object on each Revolve feature; older
documents continue to load. Old application versions cannot evaluate Revolve.

Parameter changes and source/support changes invalidate the existing geometry
cache and rebuild the feature. Invalid changes abort the OCAF transaction and keep
the prior valid document. Validation checks the angle, axis, coplanarity, profile
closure, profile intersections, nested boundaries, OCCT shape validity and positive
solid volume. Touching/overlapping loops and profiles on both sides of the axis are
rejected with an explanation. This feature creates solids, not open revolved surfaces.

Focused diagnostics: `--cad-revolve-check` exercises geometry, API operations,
dependencies, rollback, Undo/Redo and native/JSON persistence;
`--cad-revolve-ui-check` exercises the actual task-form commands and controls.
Their results are written to `build/cad-revolve[-ui]-check.result.json`.

Verified on October 22, 2026: the focused Debug application build passed;
`--cad-revolve-check` passed 38/38 checks and `--cad-revolve-ui-check` passed 7/7.
The first diagnostic covers analytic volumes, reverse direction, all datum planes,
custom axes, holes, face-supported sketches, persistent cap references, source and
parameter edits, rollback, native/JSON roundtrips, API CRUD, the FRust host call,
and node-to-host source generation. The UI diagnostic drives the real task-form
callbacks for creation, editing, axis controls, Undo/Redo and invalid-angle feedback.

Sweep, Loft, general surface modeling and other expansion phases are outside this change.
