# Parametric Gears

DjehutiCAD generates real involute spur gears as editable OCCT solids and builds
geometrically compatible spur pairs on parallel shafts. Gears are ordinary
document features (type `gear`). Their definitions are stored through the same
OCAF modeling command as Sweep and Loft, so creation, edits, Undo/Redo, preview
and native/JSON files all work the same way.

This stage implements external involute **spur** gears only. The `family`
parameter is reserved for later stages. `bevel` and `worm` are refused with a
clear message instead of being forced through the spur algorithm.

## Single gear

GUI: **Create > Gear...**. Select a gear and use **Edit gear parameters...** to
change it. Agent and local API: `cad_gear`. FRust: `djehuti_cad_gear`. Node:
**Gear**.

| Parameter | Default | Meaning |
|---|---:|---|
| `teeth` | 20 | Whole number, 5 to 400 |
| `module` | 2 mm | Pitch diameter / teeth |
| `pressureAngleDegrees` | 20 | 10 to 35 |
| `faceWidth` | 10 mm | Axial length, from z = 0 to z = faceWidth |
| `boreDiameter` | 8 mm | 0 makes a solid gear |
| `backlash` | 0 mm | Circular thinning of this gear's teeth at the pitch circle |
| `profileShift` | 0 | Shift coefficient x, from -1.5 to 1.5 |
| `addendumCoefficient` | 1 | Tip height in modules |
| `dedendumCoefficient` | 1.25 | Root depth in modules (at least the addendum) |
| `rootFilletCoefficient` | 0.2 | Root fillet radius in modules, at most dedendum - addendum |

Derived dimensions: pitch radius `r = m z / 2`, base radius `rb = r cos(a)`,
tip `r + m (ha + x)`, root `r - m (hf - x)`, and tooth thickness at the pitch
circle `m (pi/2 + 2 x tan a) - backlash`.

Geometry: each flank is an involute from `max(base, root)` to the tip circle.
It is a B-spline interpolated through 25 exact involute points, and the checks
measure its deviation from the analytic involute at below 0.0001 mm. Below the
base circle the flank is radial. Tip and root are exact circle arcs. The root
fillet is a 2D fillet between flank and root circle; it is reduced (with a
warning) when the radial flank or the tooth space is too short for it. The
profile is extruded along +Z. Tooth 0 is centred on +X.

Faces carry persistent names: `<id>/start`, `<id>/end`, `<id>/bore`,
`<id>/tip:i`, `<id>/root:i`, `<id>/flank:i:left|right`, `<id>/radial:...` and
`<id>/fillet:...`. The end faces are planar and can carry face sketches. Place
a gear with the normal placement controls (`cad_set_placement`). It can be used
in Booleans, assemblies and export like any solid.

The following are refused with an explanatory error, leaving the document
unchanged:

- root diameter of zero or less
- backlash that consumes the tooth
- tip circle inside the base or root circle
- pointed teeth
- tooth spaces that close at the root
- a bore that leaves less than 0.25 module of rim
- unknown parameters

Warnings (in `derived.warnings`, shown in the task panel):

- undercut risk below `2 (ha - x) / sin²(a)` teeth
- narrow tip land
- thin rim
- a reduced fillet

```json
{"action":"describe","parameters":{"teeth":20,"module":2}}
{"action":"create","parameters":{"teeth":24,"module":1.5,"faceWidth":8,"boreDiameter":6}}
{"action":"update","id":"gear-3","parameters":{"teeth":30}}
{"action":"get","id":"gear-3"}
{"action":"check","id":"gear-3"}
```

`describe` computes the derived dimensions without changing the document.
`create`, `get` and `update` replies include `derived`. When the gear is paired,
they also include `pairing`. If an edit breaks the pair, the reply has a
`warning`. The partner gear and coupling are never changed silently.

## Matched spur pair

GUI: **Create > Gear Pair...**. A paired gear's inspector shows the pair status
and **Edit gear pair...**. Agent and local API: `cad_gear_pair`. FRust:
`djehuti_cad_gear_pair`. Node: **Gear Pair**.

The pair has shared parameters:

- `module`, `pressureAngleDegrees` and `faceWidth`
- `backlash`: the total circular backlash; half is applied to each gear
- `addendumCoefficient`, `dedendumCoefficient` and `rootFilletCoefficient`

It also has per-gear fields, the placement and the mechanism option:

- `gearA` and `gearB`, each `{teeth, profileShift, boreDiameter, name}`
- `origin`: gear A's axis position, in mm
- `lineOfCentersDegrees`: the direction from A to B in the XY plane
- `mechanism`: `none`, `create`, or `keep` for updates

The plan:

- Working pressure angle: `inv(aw) = inv(a) + 2 tan(a) (x1 + x2) / (z1 + z2)`.
- Centre distance: `m (z1 + z2) / 2 cos(a) / cos(aw)`. This is exactly
  `m (z1 + z2) / 2` without profile shift.
- Gear A has a tooth centred on the line of centres. Gear B is rotated to
  `line + 180 - 180 / zB` degrees, so a tooth space faces it.
- Ratio `dB/dA = -zA / zB` (external gears turn in opposite directions).
- Contact ratio and both tip-to-root clearances are reported.

A pair is refused when:

- gear B's tips would contact gear A below its base circle, or A's tips would
  contact B below its base circle (involute interference)
- either gear's tips reach the other's root circle
- the contact ratio is below 1

`mechanism: "create"` adds one assembly instance and one world revolute joint
per gear: A is the driver and B is dependent. It also adds one existing `gear`
coupling with the tooth-count ratio. No new solver or kinematic relationship
type is introduced.

Actions:

- `plan`: a dry run.
- `get {id}`: the current pair definition and status.
- `preview`: evaluates on an isolated draft document.
- `create`: a single transaction.
- `update {id, parameters}`: a single transaction. The parameters merge with
  the current pair. It regenerates both gears, moves gear B, shifts their
  instances, re-derives the shaft joints from the gear axes, and resets
  existing couplings to the new ratio with zero phase offset.
- `check {id}`
- `couple {id, jointA, jointB}`: creates or corrects the gear coupling between
  existing revolute joints carrying the two gears. It is refused if a joint axis
  misses its gear axis.
- `unpair {id}`

`check` verifies that the pair still meshes:

- module and pressure angle
- parallel axes and centre distance
- axial face overlap
- tooth phase on the line of centres
- shaft joint axes
- coupling ratio
- coupling phase offset (allowed only in whole tooth pitches)

Problems make `compatible` false and explain what is wrong.

```json
{"action":"create","parameters":{"module":2,"backlash":0.1,"gearA":{"teeth":20,"boreDiameter":8},"gearB":{"teeth":40,"boreDiameter":10},"mechanism":"create"}}
{"action":"update","id":"gear-2","parameters":{"gearA":{"teeth":25}}}
{"action":"check","id":"gear-1"}
```

A coupling only relates joint coordinates. It is not proof of physical
meshing. Use **Check interference** (`cad_check_interference`) at
representative positions; the gear checks do this at four engagement positions on three pairs.

## Limitations of this stage

- External spur gears only: no internal (ring) gears, racks, helical, bevel or
  worm geometry.
- The flank below the base circle is radial with a circular root fillet, not
  the trochoid a generating cutter would leave. Undercut is reported, not
  modelled. Pairs whose contact would reach below a base circle are refused.
- No tip shortening for profile-shifted pairs. Clearance is reported, and pairs
  whose tips reach the mating root are refused.
- Pair update makes both gears share gear A's face width and the shared tooth
  system. Pairs are built on parallel Z shafts in the XY plane; a pair rotated
  about X or Y is reset to Z shafts by an update.
- No tooth contact dynamics, torque, efficiency, wear, strength or self-locking.

## Validation

Run `bin\Debug\djehuti_cad.exe --cad-gear-check`. Results are written to
`build/cad-gear-check.result.json`.
