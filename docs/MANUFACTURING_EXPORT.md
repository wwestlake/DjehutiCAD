# Geometry export (STEP, STL, 3MF, OBJ, PLY, glTF/GLB)

Export writes files from the evaluated OCCT solids. It is a read operation: it never changes the document,
marks it modified or adds undo entries.

## Where

- File > Export... (3MF, STL, STEP, GLB, glTF, OBJ, PLY): the selected body or instance is ticked, otherwise the whole visible model.
- Right-click a visible solid body or an assembly instance in the model tree > Export...
- Agent tool `cad_export`, FRust `djehuti_cad_export(args_json)`, node "CAD Export" (input `args json`).

All of them go through `CadCommandService::planExport` / `runExport` (`Source/CadExport.*`).

## Arguments (agent, FRust, nodes)

```json
{ "format": "3mf" | "stl" | "step" | "obj" | "ply" | "gltf" | "glb",
  "path": "D:/out/bracket.obj",
  "parts": ["union-5", "inst-8"],
  "quality": "low" | "standard" | "fine" | "custom",
  "chordal_mm": 0.02, "angular_deg": 10,
  "units": "mm" | "cm" | "m" | "in",
  "up_axis": "z" | "y",
  "overwrite": false }
```

- `parts`: omit for the visible modeled assembly.
- `chordal_mm` and `angular_deg` apply only when `quality` is `custom`.
- `units` and `up_axis` are described under "Units and orientation"; both default to mm and Z up.
- The result JSON lists `ok`, `files`, `size_mm`, per-part `size_mm`, `volume_mm3`, `triangles`,
  `mesh_volume_mm3`, `warnings`, or `error`.

## What is exported

- **Parts.** Bodies and instances must be visible solids. With no explicit parts, every visible solid body is
  exported. A body that has assembly instances is replaced by its instances, so the source body is not
  duplicated. Sketches, hidden Boolean operands, grids, joint guides and highlights are never exported.
- **Placement.** The authored reference configuration: body placements and instance placements. A simulation,
  DEWB or IK pose is never exported. Each placement is applied once, using the same transform as the viewport.
- **Units.** Millimetres unless `units` says otherwise (see below). 3MF declares its unit, STEP writes the unit
  it was given, STL/OBJ/PLY have no unit field (the files carry a comment, the numbers are in the chosen unit).
  Reported sizes in results are always millimetres.

## Formats

| Format | Library | Content |
|---|---|---|
| 3MF | lib3mf 2.5.0 | One mesh object per part, named, with the part id as part number, placed by its build-item transform. Title and Application metadata. |
| Binary STL | OCCT `StlAPI_Writer` | One file per part. Several parts give `<name>-<part>.stl` beside the chosen path. Shells are not joined; joining bodies is a modeling operation (Union). |
| STEP | OCCT `STEPCAFControl_Writer` | AP214 IS, millimetres. One product per source body and one named component per part with its placement, written from an export-only XCAF document. |

| Wavefront OBJ | Own writer | `.obj` plus a companion `.mtl` (both written atomically). One `o` object per part with its placement applied to the vertices, `usemtl` with the document material name, per-vertex `vn` normals, `f v//vn` triangles. A header comment states units and up axis. No hierarchy. |
| Binary PLY | Own writer | Binary little-endian, `x y z nx ny nz` floats, triangle faces. PLY has no objects, names or hierarchy, so all parts are merged into one mesh; header `comment part "<name>" vertices a..b faces c..d` lines record the ranges, and a warning says so. |
| glTF 2.0 (`.gltf`) | Own writer | One JSON file with the mesh buffer embedded as a base64 data URI. Same content as GLB. |
| glTF 2.0 Binary (`.glb`) | Own writer | GLB container (JSON + BIN chunks). Metres, +Y up, indexed triangles (16 or 32 bit), POSITION and NORMAL accessors with min/max, one node and mesh per part, assembly hierarchy and joint pivot frames as the node tree, one material per document material name. |

STEP carries exact B-rep solids. It does not transfer Djehuti feature history, sketches, scripts, mechanism
constraints, joints or simulation behaviour.

## Units and orientation

| Format | Units | Up axis |
|---|---|---|
| STEP, 3MF | `units` (mm, cm, m, in): 3MF declares it and scales the mesh, STEP declares it and writes scaled coordinates | Z (the formats are defined Z up; `up_axis` is ignored) |
| STL, OBJ, PLY | `units` | `up_axis` z (CAD) or y. Y up maps (x, y, z) to (x, z, -y), a right-handed rotation, never a mirror |
| glTF, GLB | Always metres (the glTF rule); `units` is ignored | Always +Y up (the glTF rule); `up_axis` is ignored |

- Winding stays counter-clockwise seen from outside after any rotation or scaling, and normals are rotated with the geometry.
- Blender's legacy OBJ importer defaults to Y up and glTF is Y up, so choose Y up for those; slicers expect Z up.
- The result JSON reports the effective `units` and `up_axis`.

## Colours and materials

DjehutiCAD stores no display colour, so no format carries one. The document material assigned to a body (for
example "PETG") is exported by name where the format has materials: OBJ/MTL `newmtl` and `usemtl`, glTF material
`name` (and node `extras.material`). Unassigned parts use `Default`. Every material is neutral grey so that
importers do not receive invented colours. 3MF carries no materials from DjehutiCAD on purpose: slicers assign
filaments and would treat an invented base material as one. STL, PLY and STEP carry no material information
from the document.

## Mesh quality (all mesh formats)

Presets: Low (0.2 mm, 28 deg, draft), Standard/Medium (0.05 mm, 15 deg), Fine/High (0.01 mm, 5 deg) and Custom
(chordal 0.001 - 1 mm, angular 0.5 - 45 deg). OBJ, PLY and glTF use per-face vertices so the normals of curved
faces are smooth while hard edges stay hard; the closed-surface checks run on the welded mesh of the same
triangulation, exactly as for 3MF and STL.

## Printing mesh quality (3MF and STL)

| Preset | Chordal tolerance | Angular tolerance |
|---|---|---|
| Standard | 0.05 mm | 15 deg |
| Fine | 0.01 mm | 5 deg |
| Custom | 0.001 – 1 mm | 0.5 – 45 deg |

- Chordal tolerance is the largest distance between a facet and the true surface.
- Angular tolerance is the largest angle between neighbouring facets along a curve.
- The mesh is generated by `BRepMesh_IncrementalMesh` on a private copy of each solid. The viewport's display
  mesh is not used and is not changed.

## Checks before a file is written

1. The part contains at least one solid, passes `BRepCheck_Analyzer`, and has positive volume.
2. The mesh has finite coordinates. Coincident boundary nodes are welded.
3. Collapsed triangles (two equal corners) are omitted and reported. Near-zero-area triangles are reported.
4. Every edge is used exactly twice, in opposite directions. Open, non-manifold and inconsistently oriented
   edges stop the export and name the part.
5. Signed volume must be positive. A deviation of more than 2 % from the solid volume is a warning.
6. 3MF only: lib3mf `IsManifoldAndOriented()` must also pass.

Geometry is never repaired or reshaped: holes and dimensions are kept as modeled.

## Files and cancellation

- Each file is written to a sibling `*.partial-xxxxxxxx.*` file and moved over the destination only after a
  complete write, using `MoveFileEx` with `REPLACE_EXISTING`.
- Cancel and failure delete the temporary files and leave existing destinations untouched.
- Automation refuses to replace an existing file unless `overwrite` is true. The dialog asks through the
  file chooser.
- The dialog runs meshing and writing on a worker thread, with progress and Cancel. Planning (part
  resolution and geometry copies) runs on the message thread; the worker never touches the live document.

## Focused check

`djehuti_cad.exe --cad-export-check` (working directory `D:\DjehutiCAD`) builds a 60 x 40 x 10 mm plate with
a 12 mm through-hole and an 8 mm boss, and exports it in 3MF, STL and STEP. It reads them back with lib3mf,
OCCT `RWStl` and `STEPCAFControl_Reader`, then exports a two-instance assembly with a second body. It also
checks cancellation, a missing folder, overwrite protection and an invalid tolerance.

A second section builds a box, a cylinder, a hollow cylinder, a Boolean-subtracted plate (with a PETG material),
a filleted block, and a box instance with a hinged cylinder instance, exports the complete model to OBJ (mm Z up
and inch Y up), PLY, glTF and GLB, and reads every file back with independent strict readers in the check
(OBJ text, PLY binary layout, glTF JSON with accessor ranges, embedded buffer or GLB chunks, node tree). For every part
it compares name, bounds, volume and normal direction with the B-rep. It also checks STL in inches/Y up, 3MF in
centimetres and STEP in inches (declared unit and real size), material names, hierarchy depth, and that the
document revision is unchanged.

Results go to `build/cad-export-check.result.json`; files stay in `build/export-check/`.
