# Parametric Sweep

Select a closed profile sketch and **Modify > Sweep**. Choose a path sketch in
the tree, or click the Path slot and select connected solid edges in Edge mode.
Successive edge picks add segments; Clear path replaces the selection. Profiles
use one closed planar contour. Sketch paths support lines, circles and cubic
Bezier curves; solid-edge paths also support circular arcs and other OCCT curves.
Paths must form one connected, unbranched chain. Closed circular paths are allowed
where OCCT can construct a valid swept solid.

**Align profile to path** maps the sketch origin and plane to the start point and
tangent; profile coordinates remain relative to that origin. Disable it only
when the existing profile passes through the start and is perpendicular to the
tangent. Reverse path changes the traversal direction.

Orientation choices:

- **Transport:** corrected Frenet frame, the default for general paths.
- **Frenet:** follows path curvature where the frame is defined.
- **Fixed binormal:** a constant world binormal; it must remain geometrically
  valid along the path.

Initial roll rotates the starting section. Total twist uses a helical auxiliary
guide on straight paths in Transport mode. Nonzero controlled twist on curved
paths is explicitly unsupported. Roll/twist are in degrees, within ±3600.
Round, Right corner and Transformed choose OCCT's corner-transition behavior.
Tight bends, degenerate tangents and self-intersecting results are rejected.

Choose New body, Add to target or Subtract from target. Material modes require a
target solid. **Preview** evaluates an independent draft; Apply commits one OCAF
transaction. Select the result and **Edit sources and parameters** to revise the
profile, path, orientation or operation. Cancel restores the actual document;
Delete uses the normal feature inspector. Source sketches and persistent edge
references drive rebuilding after edits and native/JSON reopen.

## Structured API / FRust / node

`cad_sweep` supports `create`, `get`, `update`, `delete`, `preview`, `references`.

```json
{"action":"create","parameters":{"profile":"sketch-profile","path":{"sketch":"sketch-path"},"operation":"new","orientation":"transport","alignProfile":true,"rollDegrees":0,"twistDegrees":0}}
{"action":"update","id":"sweep-id","parameters":{"operation":"subtract","target":"box-id","rollDegrees":30}}
{"action":"preview","id":"sweep-id","parameters":{"orientation":"frenet"}}
```

For a model-edge path, use `references {target:body-id}`, then
`path:{feature:body-id,edges:[indices or persistent references],reverse:false}`.
Indices normalize to persistent tokens at creation/update. A sketch path may
optionally specify an `entities` array of stable entity IDs. Get returns complete
parameters; updates merge top-level fields and replace nested path objects.

Defaults: operation new, orientation transport, binormal [0,0,1], alignProfile
true, zero roll/twist, transition round. FRust:
`djehuti_cad_sweep(args_json: String) -> String`; the **Sweep** CAD node uses the
same JSON and CadCommandService.

Example: a radius-2 mm circle swept along a straight 20 mm line creates a solid
of volume 80π mm³. Change the radius or path length and the feature rebuilds.
See [shared API/interchange and GUI controls](ADVANCED_MODELING_API.md).
