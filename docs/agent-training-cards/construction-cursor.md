# Card: working cursor

**Purpose.** Place a temporary 3D point to build exact positions from. The cursor
is workspace state: it is not saved, not part of undo, and never changes
geometry.

**Tool.** `cad_ui` with `action: "cursor"`. Every reply includes
`state.cursor` and `state.marker`.

| operation | parameters | effect |
|---|---|---|
| `get` | none | returns the cursor |
| `set` | `x`,`y`,`z` (mm) or, while a sketch is open, `u`,`v` | exact placement; values are stored as given |
| `reset` | none | to (0,0,0) |
| `select` | none | selects the cursor marker and opens its properties |
| `toSelection` | none | to the selected reference, planar face centre, cylinder axis midpoint, or a body's bounds centre |
| `toReference` | `id` | to a reference's origin |
| `snap` | `body` plus `label`, or `type` and `index` | to a listed snap point; list them with `cad_reference` `snapPoints` |
| `place` | `viewX`,`viewY` (pixels in the model view) | the same resolution as a mouse click |
| `options` | `shown`, `snapping` (booleans) | display and snapping |

**Conventions.** World coordinates, millimetres, Z up. In a sketch, `u`,`v` are
the sketch plane's own axes and the cursor lies on the plane (`w` = 0). Typed
values take precedence over mouse placement: place with the mouse, then `set`
the exact numbers.

**Placement order for `place`.** A visible reference; an exact snap point near
the pointer (vertex, line/arc midpoint, arc centre, planar face centre, cylinder
axis midpoint); the surface under the pointer; the XY plane at the cursor's Z. A
screen click never invents a depth.

**Example.**
```json
{"action":"cursor","operation":"set","x":12.5,"y":-3,"z":7.25}
{"action":"cursor","operation":"snap","body":"inst-3","label":"Vertex 4"}
{"action":"cursor","operation":"select"}
```

**Failure modes.** `x must be a finite number within +/-1e9`; `Unknown body or
occurrence`; `No such snap point`; `u/v are sketch-plane coordinates; open a
sketch`.

**Then.** Turn the cursor into a persistent reference with `cad_reference`
`create` using its coordinates (see the references card).
