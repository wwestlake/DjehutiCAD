# Virtual Router / Mill

Choose a solid and **Modify > Virtual Router / Mill**. A named upward planar face
is selected automatically when available. Use **Pick entry face** to choose an
accessible planar face, or select a principal/custom entry plane. The reference
normal points outward toward the approaching flat-ended cutter; machining goes
inward. Reverse approach reverses the normal and local V direction.

Choose an operation, a tool diameter and blind depth or Through cut:

- **Pocket:** one closed region sketch. The reachable region is computed using
  cutter-radius erosion and expansion. Internal corners retain the cutter radius;
  narrow or disconnected inaccessible regions fail instead of being gouged.
- **Slot:** one open connected planar path. Removal has the tool diameter and
  rounded end caps. Lines, supported curves and selected circular arcs are allowed.
- **Profile cut:** one closed path. The tool-width band can lie On path, Inside
  or Outside the intended boundary. A through profile cut may split a body into
  several valid retained solids.

**Align sketch to reference** maps sketch-local U/V coordinates into the entry
frame, like the existing face-sketch frame. Disable it to retain world placement;
the geometry must then be coplanar with the entry plane. Selected model edges
retain their world placement and must already lie in that plane. Face U/V axes
can differ on side/bottom faces; inspect the existing face-sketch frame when
positioning the region/path.

The approach plane must be outside/on the outer side of **all** target material.
Internal or obstructed entry planes are rejected conservatively. This tool does
not calculate arbitrary cutter/holder access through an existing cavity. The
cutter must fit the pocket/inside offset, and all contours and resulting solids
are checked for degeneracy and self-intersection. Blind depth is measured from
the entry plane and can break through thin material; Through cut follows later
target thickness changes. Empty/no-effect cuts fail atomically.

Preview displays an isolated draft. Apply commits one OCAF transaction. Select
the result and **Edit sources and parameters** to revise target, reference,
strategy, diameter, depth or source sketch/path. Cancel restores the model;
normal feature deletion is undoable. Sources remain in history, and dependency
rebuilds, native save and engineering JSON preserve the machining definition.

This is geometric machining. Machine simulation, feeds/speeds, G-code and
physical cutting dynamics are outside this order.

## Structured API / FRust / node

`cad_mill` supports `create`, `get`, `update`, `delete`, `preview`, `references`.

```json
{"action":"create","parameters":{"target":"box-id","reference":{"type":"face","feature":"box-id","face":"box-id/top"},"strategy":"pocket","region":"region-sketch","toolDiameter":4,"depth":3}}
{"action":"update","id":"mill-id","parameters":{"strategy":"slot","path":{"sketch":"open-path-sketch"},"throughAll":true}}
{"action":"update","id":"mill-id","parameters":{"strategy":"profileCut","path":{"sketch":"closed-path-sketch"},"compensation":"outside"}}
```

A reference is `{type:"XY"|"XZ"|"YZ"}`, `{type:"custom",origin:[x,y,z],
vector:[nx,ny,nz]}`, or `{type:"face",feature:id,face:token}`; optional `reverse`
changes its approach direction. Discover face tokens with `cad_list_faces`.
Path references follow [Sweep](SWEEP.md), including persistent model-edge tokens.
Defaults: pocket, diameter 6 mm, blind depth 3 mm, reference XY at the origin,
alignToReference true and compensation on. API callers should supply an entry
reference appropriate for their target. Inactive region/path parameters remain
stored; clear unused source IDs when removing those references.

FRust: `djehuti_cad_mill(args_json: String) -> String`; the **Virtual Router / Mill**
CAD node calls the same command service.

Examples: a radius-10 pocket of depth 3 removes 300π mm³; a straight 20 mm slot
with a 4 mm cutter removes `(80+4π) × depth` mm³; a 20×10 pocket with a 4 mm cutter
has reachable area `184+4π` mm². See [shared API/interchange](ADVANCED_MODELING_API.md).
