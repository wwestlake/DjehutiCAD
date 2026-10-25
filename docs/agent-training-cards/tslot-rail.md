# Card: parametric T-slot rail

**Purpose.** Generate a T-slot structural rail with independently configurable slot counts on each face, as one piece or
as two printable halves. Four-piece construction, internal trusses, a central bore, keyed end connections and junction
adapters are added at later checkpoints and are not available yet.

**Tool.** `cad_tslot`. The rail is a real OCAF feature `tslot_rail`: every edit regenerates the solid, is undoable and
is saved with the document.

| action | use |
|---|---|
| `describe` | rules, ranges, defaults and the default section properties |
| `list` | every T-slot feature with parameters and position |
| `create` / `get` / `update` / `delete` / `preview` | one rail: `part` = `rail`, `parameters`, optional `placement {x,y,z}` |
| `printLayout` / `assemble` | `id` = a two-half rail: lay every piece flat on the bed (slotted face down) / put them back |

**Frame.** The rail runs along +X from `x = 0` to `lengthMm`. The cross-section is centred on the axis: width along Y,
height along Z. Faces: top +Z, bottom -Z, left -Y, right +Y.

**Parameters.**
- `widthMm`, `heightMm` 10 to 200; `lengthMm` 10 to 3000.
- `slotsTop`, `slotsBottom`, `slotsLeft`, `slotsRight`: 0 to 8 each. Zero is allowed; a small rail may have one slot on
  one face, a larger rail two or more. Slots on a face are spaced evenly.
- Slot shape: `slotOpeningMm` (mouth), `slotLipMm` (lip thickness), `slotHeadWidthMm`, `slotHeadStraightMm` (straight
  walls, then 45 degree walls) and `slotDepthMm` (to the flat floor). Defaults: 6, 1.8, 10, 1.5, 6.
- `material`: a document material id (`mat-N`). Create or import it first (`cad_material_create`,
  `cad_catalog useMaterial`). Materials are isotropic; FDM layer-direction properties are not represented.
- `construction`: `onePiece` or `twoHalf` (see below). `fourPiece` is refused.

**Rules.** Every cavity keeps at least 1.6 mm of material from every other cavity and from the outside. The tool
refuses counts or sizes that do not fit and names the reason, for example "3 top slot(s) do not fit: the head width ...".

**Reading.** `create`, `get` and `update` return `section` (areaMm2, centroid, secondMomentAboutYMm4 for bending with loads
along Z, secondMomentAboutZMm4 for loads along Y, polarMomentMm4) and `volumeMm3`. The polar moment is not a torsion
constant for this open profile. Faces named for later tools: `<id>/start`, `<id>/end`, `<id>/slot:<face>:<i>/floor`.

**Example.**
```json
{"action":"create","part":"rail","parameters":{"widthMm":20,"heightMm":20,"lengthMm":200,"slotsTop":1,"slotsBottom":1,"slotsLeft":1,"slotsRight":1}}
{"action":"create","part":"rail","parameters":{"widthMm":40,"heightMm":20,"lengthMm":300,"slotsTop":2,"slotsBottom":2,"slotsLeft":1,"slotsRight":1,"material":"mat-1"},"placement":{"y":60}}
{"action":"update","part":"rail","id":"tslot_rail-1","parameters":{"lengthMm":300,"slotsRight":0}}
```

**Failure modes.** `widthMm must be between ...`; `... slot(s) do not fit ...`; `The webs between slot:top:0 and
slot:right:0 would be ... thick`; `Unknown material: ...`; `Four-piece construction is not available yet`.

**Limits.** The profile is an original design, not a manufacturer's. No strength analysis has been run on it; the
section properties are geometry only.

## Two-half construction (`construction: "twoHalf"`)

For a rail with T-slots on two opposite faces only (`slotsTop` and `slotsBottom` at least 1, `slotsLeft` and
`slotsRight` 0). The rail is split on its mid plane into a **top half** (top slots, integral snap arms) and a **bottom
half** (bottom slots, sockets). Each half prints flat with its slotted outer face on the bed and the split face up:
the slot walls are 45 degrees so no supports are needed, and every snap arm stands in the layer planes and bends
sideways, so its layers run along the arm.

Extra parameters: `printableLengthMm` (longest printed piece, default 200), `overlapMm` (stagger of the two halves'
joints, default 40), `snapPinSpacingMm` (30), `snapArmLengthMm` (14), `snapArmWidthMm` (2.0, the bending thickness),
`snapSlitMm` (0.8), `snapHookHeightMm` (2.0), `snapLipMm` (0.7, how far the barb sticks out), `snapHookLengthMm` (4),
`assemblyClearanceMm` (0.25), `snapEndMarginMm` (6).

What the tool does: for a rail longer than `printableLengthMm` it makes the fewest equal pieces whose length plus
`overlapMm` fits the printable length. Top-half joints are at multiples of the piece length; bottom-half joints are
`overlapMm` earlier, so each joint is bridged by the other half. That also gives a shorter filler piece at the start of
the bottom half. Snap arms are placed in every overlap segment, evenly spaced and centred. You never compute the
fillers. Changing any rail parameter (for example `lengthMm`) regenerates the pieces in one undoable step.

Pieces are separate features `tslot_half` (parameters `half`, `x0Mm`, `x1Mm`, `windows`, `rail`, `index`). The
whole-rail solid is kept as a hidden reference with the section properties. Delete the rail to delete its pieces.
`create`/`update` return `pieces`: `sections`, `pieceLengthMm`, `overlapUsedMm` and the piece list.

Refusals (with the reason): slots on the left or right face; side blocks too narrow for the arms (a 20 mm wide rail with
a 10 mm slot head is too narrow: use 30 mm wide, or a narrower head); web under the slot floor too thin for the sockets;
overlap segments shorter than the arm needs (`overlapMm` or `printableLengthMm` too small).

```json
{"action":"create","part":"rail","parameters":{"widthMm":30,"heightMm":20,"lengthMm":500,"slotsTop":1,"slotsBottom":1,"slotsLeft":0,"slotsRight":0,"construction":"twoHalf","printableLengthMm":200,"overlapMm":40}}
{"action":"printLayout","id":"tslot_rail-1"}
{"action":"update","part":"rail","id":"tslot_rail-1","parameters":{"lengthMm":700}}
```

Limits: arm strain (about 0.7 %) and force (about 5 N per arm to deflect) are hand calculations for PETG, not
simulation. Retention, joint strength and stiffness of the assembled rail have not been analysed or tested.
