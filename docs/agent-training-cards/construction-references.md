# Card: construction references

**Purpose.** Document-owned markers for exact placement: point, axis,
coordinate system (`cs`) and plane. They are saved, undoable and visible in the
tree; solids are never affected by them.

**Tool.** `cad_reference` with `action`: `list`, `get`, `create`, `update`,
`delete`, `snapPoints`.

`parameters` (all optional on update; send only what changes):

| field | meaning |
|---|---|
| `kind` | `point`, `axis`, `cs`, `plane` |
| `name`, `visible` | label and display |
| `origin` | `[x,y,z]` mm, world |
| `direction` | `[x,y,z]` for `axis` (line direction) and `plane` (normal); any non-zero vector, normalised; zero is refused |
| `rotationDegrees` | `[rx,ry,rz]` for `cs`, extrinsic X then Y then Z |
| `association` | `{type, source, face}` |

**Behaviours (`association.type`).**
- `fixed` (default): keeps its world values.
- `frame`: follows an occurrence (or a body that has no occurrences). Omit the world values to coincide with the source frame; give them to keep an offset.
- `cylinderAxis`: axis of a cylindrical face (`source` + persistent face token from `cad_list_faces`, for example `gear-1/bore`); position cannot be typed.
- `faceCentre`: centre of a face; planar faces also give the orientation.
- `reference`: offset from another reference; cycles are refused.

**Reading.** `get`/`list` return `status` (`ok` or `broken` with `problem`),
`world` {origin, direction, rotationDegrees} (mm, degrees), `association`, and
for followers `relative` (the stored offset). `cad_ui` `state.marker` describes
the reference selected in the UI.

**Editing rules.**
- Values are stored exactly as sent; nothing is rounded.
- Editing the world values of a `frame` or `reference` follower keeps it
  associative and recomputes its offset. To detach it, send
  `association:{"type":"fixed"}`.
- A broken reference keeps its stored values and stays listed. Repair it with a
  new association or make it fixed.
- Deleting a source leaves followers broken; nothing is silently re-attached.

**Example.**
```json
{"action":"create","parameters":{"kind":"axis","name":"Shaft axis",
  "association":{"type":"cylinderAxis","source":"inst-3","face":"component-1/face:0"}}}
{"action":"create","parameters":{"kind":"cs","name":"Mount","association":{"type":"frame","source":"inst-3"}}}
{"action":"update","id":"ref-7","parameters":{"origin":[30,0,12.5],"name":"Datum A"}}
{"action":"get","id":"ref-7"}
```

**Failure modes.** `direction must not be the zero vector`; `Unknown source
reference`; `This association would create a circular reference dependency`;
`attach to the occurrence ...` (a body placed through occurrences); `Face ... no
longer exists`.
