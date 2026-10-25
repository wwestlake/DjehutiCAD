# Construction References, Working Cursor and Reference-Based Transforms

DjehutiCAD has two construction aids. The **working cursor** is a temporary
point you can place anywhere. **Construction references** are persistent,
document-owned points, axes, coordinate systems and planes. Transforms can use
references for exact placement, alignment, rotation, mirroring and scaling.

## Working cursor

A red ring with a cross, labelled **3D cursor**. It is temporary workspace
state: it is not saved with the document and moving it never changes
geometry, earlier operations or undo history.

| Task | How |
|---|---|
| Open the controls | **Create > Working Cursor...** (X, Y, Z in mm, plus U, V on the sketch plane while sketching) |
| Place by clicking | **Place by clicking** (button, or **Create > Place Cursor by Clicking**). One click, then it ends. Esc cancels. |
| Type exact coordinates | Edit the fields and **Set cursor to these coordinates** |
| Reset | **Reset to origin** |
| Move to a selection | **Cursor to selection** (reference, planar face centre, cylinder axis, or a body's bounds centre) |

**Placement in 3D** never invents a depth from a screen click. In order, a click
takes the first of these:

1. A visible reference's origin.
2. A snap point on a body near the pointer (within 14 px). Snap points are
   offered only where they are exact:
   - vertices
   - midpoints of line edges and circular arcs
   - arc and circle centres
   - planar face centres
   - the midpoint of a cylinder's axis
3. The surface point under the pointer.
4. The explicit **XY plane at the cursor's current Z**.

A yellow marker and label preview the target while placing. Turn snapping off in
the cursor panel.

**In a sketch** the cursor lies on the active sketch plane, in its own U/V
coordinates. Placing it snaps to sketch geometry (the sketch snap settings
apply) and to references that lie on the sketch plane. Opening the cursor panel
does not leave the sketch. In the sketch, references off the plane are shown
dimmed and are not snapped.

## References

Each reference has a stable id (`ref-N`), a name, an exact numeric definition,
a visibility flag and a behaviour. They are drawn in screen space over the
model with a fixed size and are never hidden by solids: points as crosses with
a ring (amber), axes as arrows (violet), coordinate systems as red/green/blue
arrows, and planes as translucent cyan squares with a normal. Physical solids
are shaded geometry and are never drawn this way.

| Kind | Meaning |
|---|---|
| `point` | a position |
| `axis` | a line through the origin along `direction` |
| `cs` | origin and orientation (`rotationDegrees`, extrinsic X, Y, Z) |
| `plane` | the plane through the origin with normal `direction` |

**Create:**
- **Create > Construction Reference...**
- **Create reference at cursor** in the cursor panel
- the tool `cad_reference`

**Manage:**
- Select a reference in the model tree under **Construction references**, or
  click it in the view.
- **Eye:** shows or hides it. Visibility is part of the document and undoable.
- **Right-click** gives:
  - Edit
  - Show / Hide
  - Move cursor here
  - Use as Move / Rotate pivot
  - Delete

### Behaviours

| Behaviour | Source | Follows |
|---|---|---|
| Fixed | none | keeps its world coordinates |
| Follow a body or occurrence | `frame`: an occurrence id (or a body id for a body that has no occurrences) | moves and rotates with the source; the offset in the source's frame is stored |
| Axis of a cylindrical face | `cylinderAxis`: source + persistent face token (for example `gear-1/bore`) | the face's axis, midpoint along the face |
| Centre of a face | `faceCentre`: source + face token | the face's centre (planar faces also give the normal, so a plane or coordinate system needs a planar face) |
| Offset from a reference | `reference`: another reference id | that reference, with a stored offset |

Associations use stable ids, never viewport indices. They are resolved on
every query: nothing is cached, so a moved or edited source is followed
immediately. If the source is deleted, or its face is gone or no longer
cylindrical or planar, the reference is reported as **broken** with the reason
and keeps its stored values. It is never re-attached to unrelated geometry.
Broken references stay in the tree and list so they can be repaired (for
example by making them fixed) or deleted. Circular `reference` chains are
refused.

For a body that is placed through occurrences, attach to the occurrence. A
component definition is never changed by a reference.

### Persistence

References are stored in the document's OCAF container (`constructionReferences`
in versioned JSON, .djc). Every create, edit, delete or visibility change is one
undoable command. Documents without references are unchanged, and older readers
ignore the extra field.

## Transform with References

**Modify > Transform with References...** (or `cad_transform`). Pick the part,
choose the operation and fill the reference slots. You can click references in
the view or the tree, and click parts in the tree, to fill the slots.

| Operation | Behaviour |
|---|---|
| Move by displacement | `displacement` in world axes, or in the axes of a chosen reference (`frame`) |
| Move to a reference | the part's origin (or `fromReference`) lands on `toReference` |
| Rotate about a reference axis | exact `angleDegrees` about an axis (or a coordinate system's / plane's Z axis), right-hand rule |
| Align | the `source` reference (on the part) is brought onto the target reference `to`:<br>points coincide<br>axes become concentric (the source slides only perpendicular to the axis)<br>planes become coplanar (moving only along the normal)<br>coordinate systems match exactly<br>`flip` reverses the target direction |
| Mirror | across a plane reference |
| Scale | about a point reference, `factor` 0.001 to 1000, box and cylinder bodies only |

**Create copy** is off by default.
- **Off:** the original is transformed.
- **On:** the original stays and a copy is created. An occurrence is copied as
  another occurrence of the same definition, with its own placement.
  Mirror with copy keeps the source geometry together with its mirror image in
  the mirror feature; the original body stays in the document unchanged.

Rules that protect definitions:

- Occurrences change **placement only**. Mirroring or scaling an occurrence is
  refused, because it would turn it into an independent part or change the
  definition every occurrence shares.
- An occurrence positioned by its joints explains that, instead of moving.
- Rotating about a point reference, and other wrong-kind references, are refused
  with the reason.

Each operation is one undoable command. **Move / Rotate** (T) can also pivot
at a reference: right-click a reference and choose **Use as Move / Rotate
pivot**; its axes are used in local-axes mode.

## Agent and scripting access

| Tool | Purpose |
|---|---|
| `cad_reference` | `list`, `get`, `create`, `update`, `delete`; `snapPoints {body}` lists exact snap points |
| `cad_transform` | the operations above with references |
| `cad_ui` action `cursor` | cursor `get`, `set`, `reset`, `toSelection`, `toReference`, `snap`, `place`, `options` |

FRust host functions `djehuti_cad_reference` and `djehuti_cad_transform` (and
CAD nodes **Construction Reference** and **Reference Transform**) call the same
commands. FRust programs may now also call `djehuti_cad_gear` and
`djehuti_cad_gear_pair`, which were missing from the allowed host list.

```json
{"action":"create","parameters":{"kind":"axis","name":"Slip ring axis",
  "association":{"type":"cylinderAxis","source":"inst-3","face":"component-1/face:0"}}}
{"operation":"rotate","target":"box-4","axis":"ref-7","angleDegrees":90}
{"operation":"align","target":"box-4","source":"ref-5","to":"ref-6","copy":false}
```

## Limitations

- The cursor is temporary and is not saved or restored with the document.
- References are sized in screen space; there is no 3D picking of an axis's
  line, only of its handles (origin and arrow tips).
- Mirror uses the existing Mirror feature (a new feature). Scale supports boxes
  and cylinders; other bodies and occurrences are refused.
- Reference visibility is global (not per sketch or per view).
- Snap points on curved faces and spline edges are limited to the exact ones
  listed above.

## Validation

`bin\Debug\djehuti_cad.exe --cad-construction-reference-check` writes
`build/cad-construction-reference-check.result.json`. Its workflows are:

- cursor placement and snapping through the viewport's own click handler
- references from the cursor
- save and reopen
- a slip-ring axis from its cylindrical housing
- rotating a bracket about it
- moving the slip ring so its references follow
- copies that keep the original
- undo and redo
- deleting referenced geometry
- the same placement work in a sketch
