# Card: parametric DIN-rail system

**Purpose.** Generate an adjustable TS35 DIN rail and module bottoms that clip onto it, with a common alignment
reference. You model the rest of each enclosure (walls, bosses, openings, covers) with the ordinary tools on the
bottom's top face. The system is not an enclosure generator.

**Tool.** `cad_din`. Parts are real OCAF features: `din_rail`, `din_bottom`, `din_clip`, `din_screw`. Every edit
regenerates the solid, is undoable and is saved with the document.

| action | use |
|---|---|
| `describe` | standard dimensions with provenance, defaults, ranges |
| `list` | every DIN feature with parameters and position |
| `create` / `get` / `update` / `delete` / `preview` | one part: `part` = `rail`, `bottom`, `clip` or `screw`, `parameters`, optional `placement {x,y,z}` |
| `module` | one aligned set (bottom, clip, friction screw) in one undoable step |
| `updateModule` | change one module (`id` = its bottom id); the others are untouched |
| `deleteModule` | remove the bottom, clip and screw together |

**Frames (read this first).**
- Module frame: origin at the alignment corner; +X along the rail; +Y across it; +Z up from the rail-contact plane.
- The **alignment edge** is the plane `y = 0`. It stays straight (tabs are refused on it). All modules share it, so
  their edges line up on the rail.
- The rail axis is `31 mm` in front of the alignment edge. A module placed at `y = railY - 31` sits on the rail. Pass
  `rail: "<rail id>"` to `module` and this is done for you (`y = rail.y - 31`, `z = rail.z`).
- The rail is modelled with its axis on `y = 0`, flange plane `z = 0`, starting at `x = 0`.

**Parameters.**
- `din_rail`: `lengthMm` 10 to 6000. The cross-section never changes with length.
- `din_bottom`: `lengthMm` along the rail 28 to 1000, `widthMm` across the rail 60 to 1000, `thicknessMm` 3 to 30,
  `tabs` (up to 16). A tab is `{edge: xmin|xmax|ymax, offsetMm, widthMm >= 3, projectionMm >= 3, thicknessMm?, holeDiameterMm?}`.
  `offsetMm` runs along the edge from its start (x = 0 for `ymax`, y = 0 for `xmin`/`xmax`).
- `din_screw`: `advanceMm` 0 to 0.9. About 0.7 touches the rail flange edge. The nylon screw stays in the clip.
- `module` also takes `xMm` (position along the rail), `rail`, `yMm`, `name`, `screwAdvanceMm`.

**What does not change when you resize a bottom.** The hook, the spring-clip arm, the release lug, the friction-screw
block and the clip pocket sit at fixed module-frame coordinates (x 2 to 26, y 8 to 58). Only the plate grows.

**Rules.**
- Edit one module with `updateModule`; other modules never change.
- The clip is a separate replaceable part (`din_clip`) in a pocket under the bottom.
- Faces named for face sketches: `<bottom id>/top`, `bottom`, `xmin`, `xmax`, `ymin`, `ymax` (only when unique).
  Build walls by sketching on `<bottom id>/top` and extruding.
- Dimensions marked provisional in `describe` (rail wall spacing, flange shape) are not confirmed against the EN 60715
  drawing. Width 35, height 7.5 and sheet 1.0 mm are documented.

**Example.** A rail, then two different modules on it:
```json
{"action":"create","part":"rail","parameters":{"lengthMm":300}}
{"action":"module","parameters":{"lengthMm":60,"widthMm":70,"thicknessMm":3,"xMm":10,"rail":"din_rail-1"}}
{"action":"module","parameters":{"lengthMm":45,"widthMm":60,"thicknessMm":4,"xMm":90,"rail":"din_rail-1",
  "tabs":[{"edge":"xmax","offsetMm":10,"widthMm":12,"projectionMm":8,"holeDiameterMm":3.4}]}}
{"action":"updateModule","id":"din_bottom-7","parameters":{"widthMm":90,"lengthMm":80}}
{"action":"update","part":"rail","id":"din_rail-1","parameters":{"lengthMm":450}}
```

**Failure modes.** `lengthMm must be between ...`; `Tabs are not allowed on the alignment edge (ymin)`;
`A xmax tab (...) extends past the ... edge`; `Unknown DIN rail: ...`; `Unknown DIN module bottom: ...`.

**Limits.** Strain and force numbers for the clip arm and snap hooks are hand calculations for PETG. Nothing is
simulated. Print and test before relying on the retention.
