# Parametric Loft

Select the first closed profile sketch and **Modify > Loft**, then choose further
sketches in the tree in section order. Each section must contain one closed planar
contour. Use **Move section up/down** or **Remove section** to revise the order.
The section list and all correspondence settings remain parametric dependencies.

Enable **Ruled transitions** for straight generators between sections; disable
for OCCT smooth transitions. **Automatic** correspondence lets OCCT reconcile
compatible section edge counts and orientation. **Manual** requires equal edge
counts and exposes explicit per-section controls:

- Starting edge: zero-based cyclic edge offset for a polygon.
- Reverse section: reverse wire traversal.
- Circular seam angle: angular seam on a true circular section, in degrees.

Use Manual when explicit correspondence must be retained. Arrays in the API are
empty for defaults or contain one entry per section. GUI reordering moves each
section's correspondence settings with it. Coincident/degenerate sections,
unsupported multiple contours, invalid seams, incompatible correspondence and
self-intersecting solids return controlled errors.

Choose New body, Add to target or Subtract from target. Preview evaluates an
independent draft; Apply commits one OCAF transaction. Select the result and
**Edit sources and parameters** to edit it. Cancel restores the actual model;
Delete uses the normal inspector. Section geometry/support edits rebuild the
Loft, and Undo/Redo and native/JSON persistence preserve its definition.

## Structured API / FRust / node

`cad_loft`: `create`, `get`, `update`, `delete`, `preview`, `references`.

```json
{"action":"create","parameters":{"sections":["section-a","section-b","section-c"],"ruled":false,"alignment":"auto","operation":"new"}}
{"action":"update","id":"loft-id","parameters":{"sections":["section-a","section-c","section-b"],"offsets":[],"reverseSections":[],"seamAngles":[]}}
{"action":"update","id":"loft-id","parameters":{"alignment":"manual","ruled":true,"offsets":[0,0,0],"reverseSections":[false,false,false],"seamAngles":[45,45,45]}}
```

Select 2–32 distinct sketch IDs. Defaults are smooth (`ruled:false`), automatic
correspondence, new body and empty correspondence arrays. Add/subtract require
`target`. Updates merge supplied top-level fields; when changing section count,
clear or resize correspondence arrays to match. Get returns the full definition.

FRust: `djehuti_cad_loft(args_json: String) -> String`; the **Loft** CAD node uses
the same JSON and shared command service.

Example: ruled circular sections of radii 2, 3 and 4 mm at Z=0,10,20 form two
frusta with total volume 560π/3 mm³. Editing the middle radius rebuilds both
transitions. See [shared API/interchange and GUI controls](ADVANCED_MODELING_API.md).
