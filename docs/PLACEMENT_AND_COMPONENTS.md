# Moving Parts, Camera Navigation and Components

## Select, move and rotate

- Click a body or assembly component in the 3D view or the model tree to select
  it. Selected bodies are highlighted.
- **Move / Rotate** places the manipulator at the centre of the selection's
  bounds. Open it with **T**, **Modify > Move / Rotate**, or the four-arrow icon
  on the Modify toolbar.
- The manipulator has three parts:
  - **Arrows (X red, Y green, Z blue):** drag one to move along that axis.
  - **Rings:** drag one to rotate about that axis through the centre point (the
    pivot is the white dot).
  - **Active handle:** turns yellow. A readout next to the pointer shows the
    distance or angle.
- Hold **Ctrl** to snap to 1 mm or 15°. Press **Esc** during a drag to cancel.
  The view shows a preview during the drag, and nothing changes until you
  release.
- Each move or rotation changes only the **placement**. The modeled geometry
  and component definitions are untouched. Each one is a single undoable
  command, and the status line says what moved.
- **Local axes:** `transformLocal` (also available to the agent through
  `cad_ui`) switches between world axes and the selected body's orientation.
- **Numeric entry:** the inspector edits position and rotation directly.
  - Bodies have Position and Rotate X/Y/Z. Rotation is about world axes through
    the placement point, applied X, then Y, then Z.
  - Assembly instances keep their existing Origin and Rotate fields.
- **Jointed occurrences:** an occurrence that is positioned by its joints
  explains why instead of moving. Ground it, or edit its joint, to move it.

## Camera

- **Orbit (left drag):** orbits about the surface point under the pointer. Over
  empty space it orbits about the current screen centre. The picked point stays
  fixed on screen. Panning no longer leaves the orbit pivot at the world origin.
- **Pan:** right drag or Shift drag.
- **Zoom:** mouse wheel.
- **Fit all:** F.
- **Focus selected:** Shift+F frames the selection and orbits about it.
- **Orbit point:** Ctrl+Shift+O picks a point to orbit about.
- **Standard views:** View > Front / Back / Left / Right / Top / Bottom /
  Isometric, or `cad_ui` camera `operation: "standardView"`. Views keep the
  current orbit target.
- **Home:** resets the camera.
- Camera actions never change the document.

## Components

1. **Create > Insert Component...** (or Window > Component Library) opens the
   library.
2. Browse or search, select a record, and choose **Insert**.
3. DjehutiCAD returns to the CAD view with the new occurrence selected and
   Move / Rotate ready.
4. Insert the same record again for another occurrence. Occurrences share one
   document definition and move independently.
5. Save as .djo or .djc. Definitions, occurrence identities and placements are
   restored on reopen.

**Save designed part...** in the library publishes a modeled solid as a
reusable custom component (unchanged; see
[COMPONENT_LIBRARY.md](COMPONENT_LIBRARY.md)).

## Multi-part components (stator and rotor)

A record with `geometry.kind: "assembly"` describes several parts in one
component frame:

```json
{"geometry": {"kind": "assembly"},
 "assembly": {
   "parts": [
     {"role": "stator", "name": "Stator", "geometry": {"kind": "revolved", "profileMm": [[r, z], ...]}},
     {"role": "rotor",  "name": "Rotor",  "geometry": {"kind": "revolved", "profileMm": [[r, z], ...]}}],
   "joint": {"type": "revolute", "parent": "stator", "child": "rotor",
             "origin": [0, 0, 0], "direction": [0, 0, 1], "name": "rotation"}}}
```

Inserting the record does the following in one undoable step:

- Creates one definition and one occurrence per part.
- Grounds the joint's parent part (the stationary housing).
- Places the existing revolute (or prismatic/fixed) joint on the shared axis.

Move the stator with Move / Rotate and the rotor follows its joint. The rotor
turns through the normal joint preview and mechanism tools.

`revolved` geometry is a nominal turned part: a closed polygon of
[radius, z] points in millimetres, revolved about the part's Z axis. Nominal
geometry is labelled as such in each record. It is not a manufacturing-qualified
interface.

## Slip rings

**Electrical / Slip rings** in the library holds seven records:

- Senring M065-06, M065-08, M065-12
- Senring M125-06, M125-12
- MOFLON MMC182, MMC1829

Their values come from the 2026-10-24 slip-ring reference document and the
manufacturers' pages. Each record holds:

- channels, current and voltage ratings, maximum speed
- housing OD and length
- lead wire and length, lead colour order, IP rating
- insulation and dielectric ratings
- flange options
- sources and confidence, plus per-ring electrical channel entries

Each inserts as a grounded **stator** and a **rotor** on a revolute joint about
the housing axis.

- **Stator:** the manufacturer's housing envelope (OD x L).
- **Rotor:** only a small reference marker at the assumed rotating end. Its
  size is not from a drawing.
- **Attachment frames:** the axis, stator and rotor frames are stored in each
  part's `references`. They are marked unverified: the stator is assumed at
  z = 0 and the rotor at z = L.

Not modelled, because the drawings are images that were not captured:

- rotor and stator split
- lead exits
- flange patterns

No manufacturer STEP files were acquired. MOFLON releases them through a
request form, and Senring by technical inquiry. Confirm the exact variant and
drawing before designing mating parts. Do not derive fits, clamp pressure,
bearing loads or tolerances from these envelopes.

## Validation

`bin\Debug\djehuti_cad.exe --cad-placement-workflow-check` writes
`build/cad-placement-workflow-check.result.json`.
