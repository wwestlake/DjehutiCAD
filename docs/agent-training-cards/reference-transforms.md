# Card: reference-based transforms

**Purpose.** Move, rotate, align, mirror or scale a body or assembly occurrence
using construction references, with an explicit original-versus-copy choice.

**Tool.** `cad_transform`. One undoable command per call.

| operation | parameters |
|---|---|
| `move` | `displacement` `[dx,dy,dz]` mm (optional `frame` = reference whose axes it uses), or `toReference` (optional `fromReference`, default the part's origin) |
| `rotate` | `axis` (axis reference, or a cs/plane whose Z is used), `angleDegrees` |
| `align` | `source` (reference on the part), `to` (target reference), optional `flip` |
| `mirror` | `plane` (plane or cs reference) |
| `scale` | `center` (reference), `factor` 0.001 to 1000; box and cylinder bodies only |

Common: `target` (occurrence id, or a body id for a body with no occurrences) and
`copy` (default `false`).

**Rules.**
- `copy:false` transforms the original; `copy:true` leaves it and makes a copy.
  An occurrence is copied as another occurrence of the same definition.
- Occurrences change placement only. Mirroring or scaling one is refused
  (it would change or detach the shared definition).
- An occurrence positioned by its joints is not moved; ground it or edit the joints.
- Align brings points together; makes axes concentric (the part slides only
  perpendicular to the axis); makes planes coplanar (moves only along the
  normal); matches coordinate systems exactly.
- Associated references follow a moved part; fixed references stay.

**Example: swing a bracket 90 degrees about a slip-ring axis.**
```json
{"operation":"rotate","target":"box-4","axis":"ref-7","angleDegrees":90}
{"operation":"align","target":"box-4","source":"ref-5","to":"ref-6"}
{"operation":"move","target":"inst-3","displacement":[0,0,15],"copy":true}
```

**Reply.** `ok`, `id` (and `copyId`), `translationMm`, `rotationAngleDegrees`.

**Failure modes.** `a point has no direction` style kind errors that name the
reference; `... is broken: <reason>`; `Scaling would change the definition shared
by every occurrence`; `... is positioned by its joints`.
