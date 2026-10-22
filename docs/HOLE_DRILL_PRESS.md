# Hole / Drill Press

Select a planar face in Face mode and choose **Drill a hole on this face**, or open
**Modify > Drill Press / Hole** and pick the entry face. Set the U/V centre position,
diameter and blind depth, or enable Through all. Choose Plain, Counterbore or
Countersink, then apply **Drill hole**.

- **Plain:** cylindrical, finite flat-bottom cut or through-all hole.
- **Counterbore:** larger cylindrical head recess with its own diameter and depth.
- **Countersink:** conical head recess controlled by entry diameter and included
  angle. Its depth follows the two diameters and the angle.

The head diameter must exceed the shaft diameter, and the head must leave some
shaft depth. All lengths are millimetres; the countersink included angle is in
degrees, greater than 0 and less than 180. Finite cuts can break through a thin wall
if their requested depth exceeds it; Through all remains through after thickness
changes. This is a modeling tool, not a machine/feed/toolpath simulation.

Select the resulting Hole body to edit its position, diameter, depth, type and head
parameters. **Edit support face** temporarily shows the source body so another
planar face can be chosen. Cancel restores the normal display. A new face resets
the omitted/default position to its centroid; adjust U/V as needed.

## Position and references

The support uses the existing planar-face sketch frame: world point = origin +
U × position-U + V × position-V. Drilling goes against its outward normal into the
body. The frame follows the named face when source dimensions or placement change.
For an unplaced box top face, U is world X and V is world Y. Side and bottom faces
can use other directions or negative coordinate values.

The centre must lie inside the trimmed face, away from its boundary and existing
openings. The default is the face centroid, which can lie in an opening on a ring
or concave face; enter a material position in that case. Curved and unnamed faces
are not supported. Removed/split support references produce controlled failures,
rather than attaching the hole to a different face.

## Agent / local API

`cad_hole` supports `face`, `create`, `get`, `update` and `delete`. Get face names
through `cad_list_faces`; inspect the support coordinate system before positioning:

```json
{"action":"face","target":"box-1","face":"box-1/top"}
{"action":"create","parameters":{"target":"box-1","face":"box-1/top","u":10,"v":15,"diameter":6,"depth":8}}
{"action":"update","id":"hole-2","parameters":{"throughAll":true,"style":"counterbore","headDiameter":12,"headDepth":3}}
{"action":"update","id":"hole-2","parameters":{"style":"countersink","countersinkAngleDegrees":90}}
{"action":"get","id":"hole-2"}
{"action":"delete","id":"hole-2"}
```

Face inspection returns world `frame.origin`, unit `frame.u`, `frame.v`, outward
`frame.normal`, and `centreUV`. Creation requires `parameters.target` and `face`.
Defaults are diameter 6 mm, finite depth 10 mm, plain style, head diameter 12 mm,
head depth 3 mm and included angle 90 degrees. Omitted U/V use the face centroid.
Updates merge supplied fields. Changing target/face recentres coordinates that
were not supplied in that update.

CRUD success returns `ok`, stable `id`, `type: "hole"` and complete `parameters`.
Deletion returns `deleted:true`; deletion with dependent features/instances is
refused. Errors return `ok:false`, `code` and `error`, with the live valid document
preserved. Invalid face, position, diameter, head size/depth and cone angles are
reported explicitly.

FRust: `djehuti_cad_hole(args_json: String) -> String`. The **Drill Press / Hole** CAD
node accepts identical JSON. The GUI calls the same command service. `cad_ui`
command `hole` opens the task; controls are `holeU`, `holeV`, `holeDiameter`,
`holeDepth`, `holeThroughAll`, `holeStyle` (Plain/Counterbore/Countersink),
`holeHeadDiameter`, `holeHeadDepth`, and `holeAngle`. Face picks use the shared CAD
selection. Head/depth controls appear only when relevant.

## Document behavior

Each create/update/delete is an OCAF transaction with Undo/Redo. The target and
named entry face are persistent dependencies; source edits rebuild drilling and
downstream operations. The original target stays in the history and is hidden,
as with existing Cut/Chamfer tools. Boolean operations use private geometry copies.
Original face names are carried forward; new hole walls, bottoms and head surfaces
receive role names for later inspection and face-based operations.

Native `.djo` and engineering `.djc` JSON preserve every parameter and reference.
JSON version 11 adds the Hole parameter object; older documents continue to load.
Old application versions cannot evaluate Hole features. The focused
`--cad-hole-check` diagnostic writes `build/cad-hole-check.result.json`.

Verified October 22, 2026: Debug build and 27 focused checks passed, covering
analytic volumes for all hole styles, U/V edits, side/bottom normals, through-all
rebuilding, chained face references, Undo/Redo, native/JSON persistence, invalid
input rollback, actual GUI form/selection callbacks and CAD node generation.
The shared support resolver reads orientation from the face in the final solid,
so drilling follows its inward direction even when a history image is reversed.
GUI checks exercise controls programmatically; native pointer/visual inspection
has not been performed.
