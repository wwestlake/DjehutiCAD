# Card: geometry export (STEP, STL, 3MF, OBJ, PLY, glTF/GLB)

**Purpose.** Write the evaluated solids to files for CAD, Blender, game engines and 3-D printing. Export is a read
operation: it never changes the document and adds no undo entry.

**Tool.** `cad_export` (agent), `djehuti_cad_export(args_json)` (FRust), node "CAD Export". File > Export... and the
model-tree context menu open the dialog.

```json
{"format":"obj","path":"D:/out/bracket.obj","parts":["union-5"],"quality":"fine","units":"mm","up_axis":"y","overwrite":true}
```

| format | use it for | notes |
|---|---|---|
| `step` | other CAD | exact B-rep, assembly and placements, names; `units` mm/cm/m/in |
| `3mf` | printing (Bambu Studio and others) | one file, named objects, placement transforms; Z up; `units` |
| `stl` | printing, simple interchange | one file per part; `units`, `up_axis` |
| `obj` | Blender, polygon modeling | `.obj` + `.mtl`, named objects, normals, material names; `units`, `up_axis` |
| `ply` | mesh tools | all parts merged, normals; `units`, `up_axis` |
| `gltf` / `glb` | game engines | metres and Y up always; node hierarchy, joint pivots, material names |

**Rules.** `parts` omitted exports the visible model; ids are bodies or instances (a body with instances is replaced by
them). `overwrite` must be true to replace files. `quality` low/standard/fine, or `custom` with `chordal_mm` (0.001 to 1)
and `angular_deg` (0.5 to 45). Sizes in results are always mm. DjehutiCAD has no colours, so none are written; the
document material name is exported as the material name. Meshes must be closed and consistently oriented or the export
stops and names the part. Cancelled or failed exports leave existing files untouched.

**Choose axes by destination.** Blender OBJ importer and glTF consumers are Y up; slicers are Z up. glTF forces Y up and
metres, so `units` and `up_axis` are ignored for it.

**Failure modes.** `format must be 3mf, stl, step, obj, ply, gltf or glb.`; `units must be mm, cm, m or in.`;
`up_axis must be z or y.`; `... already exists. Choose another name or allow overwriting.`; `<part> is hidden ...`;
`<part>: the printing mesh is not a closed, consistently oriented surface ...`.

**Limits.** Feature history, sketches, constraints and simulation are not transferred. Blender (2.79 in the test machine) was
used to import OBJ and PLY; glTF files were validated with a strict reader written for the check, not an external
application.
