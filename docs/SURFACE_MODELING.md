# Surface modeling

The **Surfaces** menu provides **Planar Surface**, **Extruded Surface**,
**Revolved Surface**, **Ruled Surface**, **Trim Surface**, **Extend Surface**,
**Split Surface**, **Sew Surfaces** and **Create Solid from Shell**. Matching
buttons sit together in the modeling Modify toolbar. The Modify menu retains
Create Surface and the other surface operations.
These are editable OCAF features.
Select their source geometry in the tree or model views, enter dimensions, and
use Preview before Apply. Preview is isolated from the live document and Undo
history. Select a result and choose **Edit sources and parameters** to reopen it.
Delete is undoable; referenced sources cannot be deleted before their dependents.

## Create Surface

Choose a creation command, select its source, enter parameters, then Preview and
Apply. Creation commands always create a new object. To modify an existing
surface, select it in the **Surfaces** tree group and choose **Edit sources and
parameters**. Apply updates that object's definition and keeps its identifier.

For the four creation commands, choose a whole sketch, toggle individual curves
under an expanded sketch in the tree, or select connected model edges in Edge
mode. **Replace source** clears the current selection. Construction curves are
excluded from profiles; a straight construction line can still define an axis.
Each source must be one connected, unbranched wire (up to 256 segments).

The four Phase-1 tools are:

- **Planar Surface:** one closed, planar, non-self-intersecting boundary. Sketch
  rectangles, circles and arbitrary closed contours or model-edge wires work.
- **Extruded Surface:** one connected open or closed curve, signed distance in mm,
  and a normalized world direction vector. **Use source sketch normal** sets a
  transverse direction for the sketch plane. Ends remain uncapped.
- **Revolved Surface:** one connected open or closed curve, sketch U/V, world,
  custom or selected straight sketch-line axis, and an angle greater than zero
  and at most 360 degrees. For sketch sources the axis must lie in their plane.
  Use the Axis line slot and select an individual line under its sketch.
- **Ruled Surface:** two open curves or two closed wires with matching segment
  counts. Pick the first and second source slots in order. Reverse either source
  to adjust endpoint correspondence. Incompatible, degenerate or intersecting
  results return an error and leave the committed document intact.

Preview starts an isolated viewport draft. Subsequent numeric edits regenerate
the draft after a short pause. Clear preview returns to source picking; Cancel
discards the draft. Apply commits through the same command used by the API.
Independent surfaces display from both sides and support normal viewport/tree
selection and Hide/Show controls. Source and axis edits trigger dependency
rebuilds. OCAF Undo/Redo and native `.djo` / engineering JSON `.djc` persistence
retain definitions, source references and object identities.

The generic Create Surface form also retains these existing methods:

- **Swept surface:** the existing Sweep path, frame, roll, corner and alignment
  controls, with an open or closed profile and without solid caps. Controlled
  nonzero twist requires a straight path in Transport mode.
- **Lofted surface:** ordered open or closed section sketches and the existing
  smooth/ruled and correspondence controls. Sections must consistently be open
  or closed. Move section up/down changes ordering; manual correspondence needs
  equal edge counts. Cyclic starting edges apply only to closed sections.

Surface creation produces faces or shells. It never automatically closes the
ends or creates a solid. Surfaces display from both sides. **Flip surface normals**
reverses the orientation supplied by the profile traversal and OCCT builder;
positive/negative thickness directions follow this orientation.

## Trim, Extend and Split

**Trim Surface:** select a surface/shell or a named face, then a boundary sketch,
individual sketch curves, persistent model edges, or intersecting feature/face. Boundary curves must lie on the surface; intersecting
geometry must divide it. Click **Pick trim region**, click inside the region in
a model view, or enter an exact world-space region point. **Keep selected region**
retains it; clearing that option removes it. Preview shows the chosen result.
The command anchors the point to a persistent source face and its U/V parameters,
so it follows source placement and supported geometry changes. Pick a new point
if that face is removed/split or a new trim boundary changes region membership.
The coordinate fields record the last specified point; use the region picker or
enter the new complete world coordinates when changing selection after movement.
Boundary points, tools that do not divide the surface, and invalid results give
controlled errors. Curves are not silently projected onto a different surface.

**Extend Surface:** select one face and its boundary edge, then enter an extension
distance or select a parallel sketch curve/model edge beyond the boundary as a
target. The earlier U-minimum, U-maximum, V-minimum and V-maximum switches remain
available when no edge is picked. This uses natural continuation
of analytic geometry and C1 continuation of eligible bounded surfaces. Only
rectangular U/V faces are eligible. Arbitrary trimmed contours, holes, unsupported
surface bases and extensions past closed/periodic limits are rejected.

**Split Surface:** select an open surface or named face and the cutting geometry
used by Trim. Preview shows the resulting regions. Apply creates a hidden Split
operation and separately selectable, hideable region features in the model tree.
Edit the parent Split operation to replace its cutter. Source-face U/V anchors
retain region identities across compatible edits and source regeneration; if a
region disappears or its source face is replaced, the edit fails atomically or
the remaining regions are matched to the closest compatible anchors.

All three tools use **Preview**, **Apply** and **Cancel** in the modeling panel.
After an initial preview, parameter changes refresh the draft. Their source
references, definitions and resulting surfaces participate in Undo/Redo and
native/JSON save and reload.

## Sewing shells and creating solids

**Sew Surfaces:** select adjacent surfaces/shells in the tree, or named individual
faces in a model view. Remove unwanted references in the source list. Set an
explicit engineering tolerance between 0.0000001 and 0.1 mm. The output must be
one connected shell; disconnected inputs, faces collapsed by the tolerance, and
nonmanifold joins fail with specific diagnostics. Preview reports whether the
shell is open or closed and its unmatched boundary count and length. The API also
reports each open edge's endpoints and length so remaining gaps can be located.

Sew creates a persistent parametric shell in the **Shells** model-tree group.
Select it to inspect face count, connected regions, open boundary length and
solid-conversion readiness. Shells can be shown, hidden, edited and selected like
other document features. Sew preserves valid open shells. Even a closed result
remains a shell.

**Create Solid from Shell** is a separate command in the Surfaces menu and modeling
toolbar; a ready shell also offers it in its properties. It requires one closed,
connected, consistently orientable shell and produces a genuine positive-volume
OCCT solid. Open, nonmanifold, disconnected or invalid shells fail without a
document change. Both operations retain source references and participate in
Undo/Redo and native/JSON persistence.

## Thicken Surface

Select a face or open shell, enter a positive thickness magnitude, and choose
Positive normal, Negative normal or Symmetric. Symmetric uses half the thickness
on each side. OCCT constructs offsets and boundary walls, and the result must
be a valid solid. Closed shells should use Solidify, or expose a selected face
for thickening. Excessive curvature offsets, self-intersections and incompatible
topology fail; thickness is never silently reduced.

All sources remain dependencies. Sketch dimensions, source placements and upstream
parameters trigger normal downstream rebuilds. Native `.djo` and engineering
`.djc` JSON preserve definitions; JSON version 14 adds the surface feature types
in the established `modeling` object. Stable face names are used for references;
topology changes that split or remove a selected face require reselection.

See [Surface API](SURFACE_MODELING_API.md) for structured commands, FRust hosts
and compiled CAD nodes. Debug and Release validation are reported separately in
the order completion report.
