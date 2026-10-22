# Parametric Circular Pattern

Choose a body or supported modeling operation and **Modify > Circular Pattern**.
Set the rotation axis, count, angle and Reverse. Axis choices are world X/Y/Z,
a custom origin/vector, a straight-edge axis, a circular-edge axis, or a planar
face's normal through its existing face-frame origin. Custom origin is in mm;
vectors are normalized. There is no separate named datum-axis subsystem.

Counts include the source at index zero. For 360 degrees, angle(i)=i×360/count:
the final position never duplicates zero. Partial patterns include both ends,
angle(i)=i×angle/(count−1). Reverse negates those angles. Excluding the source
skips index zero without redistributing the other positions.

Preview, Apply/Cancel, editing and source/target modes follow [Mirror](MIRROR.md).
**Modeling feature** applies Hole/Cut cutters or Boolean added/removed material
to its intended target. Through-all Hole/Cut extents are recalculated at each
position. Source parameter and target changes rebuild the result. Bodies mode
retains separate solids; add/remove modes use explicit solid tools and a target.

## Example: circular bolt-hole pattern

On a 100 × 100 × 10 plate, create a through Hole of diameter 4 at U=70,V=50.
Choose Modeling feature, custom Z axis at [50,50,0], count=6 and angle=360.
The six hole centres lie on a 20 mm bolt-circle radius. Removed volume is 240π
mm³. Changing the source Hole changes all six positions.

```json
{"action":"create","parameters":{"sources":["hole-id"],"operation":"feature","axis":{"type":"custom","origin":[50,50,0],"vector":[0,0,1]},"count":6,"angleDegrees":360}}
{"action":"update","id":"circularPattern-id","parameters":{"count":4,"angleDegrees":180,"reverse":true}}
```

`cad_circular_pattern` supports CRUD, `preview`, and `references`. Defaults are
axis Z, count 6, angleDegrees 360, reverse false and includeSource true. Reference
edges are discovered with `references {target}`; write `{type:"edge",feature:id,
edge:index}`, then retain the returned persistent reference. Arbitrary curved
edges are unsupported; select a straight line or true circle. Invalid axes,
counts, angles, no-effect material instances and failed geometry return structured
errors and preserve the valid document.

FRust: `djehuti_cad_circular_pattern(args_json: String) -> String`. The **Circular
Pattern** CAD node uses identical JSON and CadCommandService.
See [shared API/interchange conventions](PATTERN_API_AND_INTERCHANGE.md).
