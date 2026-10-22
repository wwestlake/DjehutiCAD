# Parametric Linear Pattern

Choose a body or supported modeling feature and **Modify > Linear Pattern**.
Choose one direction or a two-direction grid, counts and per-step spacings in mm.
Counts include position zero even when **Include source position** is disabled.
Directions can be world X/Y/Z, a numeric custom vector, a straight edge, or a
planar face normal. Set a picked reference type, click **Pick face / edge**, then
select the geometry. Reverse flags independently reverse each step direction.

For a one-direction array leave count2 at 1. For a grid use count1/count2 greater
than 1 and nonparallel directions. Preview, Apply/Cancel, subsequent editing and
source/target selection follow [Mirror](MIRROR.md). Ordinary successive body
clicks add sources; Clear sources replaces the selection.

Choose **Modeling feature** for a Hole, Cut, Subtract or Union. The result modifies
the intended target rather than copying the entire already-modified body. A
selected Hole/Cut/Boolean defaults to this mode. Hole and Cut instances reuse
their source cutters, including through-all extent at each repeated position.
Source sketch, diameter/depth and target changes rebuild the pattern. Explicit
add/remove modes use source solids as tools; Bodies mode keeps separate solids.

## Example: six through holes in a plate

Create a 100 × 100 × 10 mm box and a through Hole at U=10, V=10, diameter 4 on its
top face (see [Drill Press](HOLE_DRILL_PRESS.md)). Select that Hole, open Linear
Pattern and use count1=3, spacing1=20, count2=2, spacing2=30, X/Y directions.
Expected removed volume is six cylinders: 240π mm³. Changing the Hole diameter
updates every patterned position.

```json
{"action":"create","parameters":{"sources":["hole-id"],"operation":"feature","direction1":{"type":"X"},"count1":3,"spacing1":20,"direction2":{"type":"Y"},"count2":2,"spacing2":30}}
{"action":"update","id":"linearPattern-id","parameters":{"count2":1,"includeSource":false,"reverse1":true}}
```

## API, FRust and node

`cad_linear_pattern` provides CRUD, `preview` and `references`. Defaults are
count1=2, count2=1, spacing1=spacing2=20 mm, direction1=X, direction2=Y, both
reverse flags false. Other fields and modes follow Mirror. Instance index is
`j * count1 + i`, with i varying fastest. Skipping source excludes only index 0.

Use `references {target}` to list reference-body edges. An edge direction is
`{type:"edge",feature:"box-id",edge:0}` on create/update; the result stores a
persistent `reference` token. Get returns current normalized definitions. Missing
or ambiguous references, nonintegral counts, zero spacing/vectors, parallel
two-direction axes, off-target cuts and invalid geometry fail atomically.

FRust: `djehuti_cad_linear_pattern(args_json: String) -> String`. The **Linear
Pattern** CAD node uses identical JSON and the same command service.
See [shared API/interchange conventions](PATTERN_API_AND_INTERCHANGE.md).
