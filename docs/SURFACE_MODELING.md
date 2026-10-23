# Surface modeling

The Modify menu provides Create Surface, Trim Surface, Extend Surface, Sew
Surfaces, Solidify Shell and Thicken Surface. These are editable OCAF features.
Select their source geometry in the tree or model views, enter dimensions, and
use Preview before Apply. Preview is isolated from the live document and Undo
history. Select a result and choose **Edit sources and parameters** to reopen it.
Delete is undoable; referenced sources cannot be deleted before their dependents.

## Create Surface

Choose a method:

- **Planar boundary:** one closed, planar, non-self-intersecting sketch contour.
- **Extruded surface:** one connected open or closed sketch profile, a signed
  distance in mm, and a world direction vector. The vector is normalized.
- **Revolved surface:** one connected open or closed profile, a coplanar sketch
  U/V, world or custom axis, and a partial or full revolution angle.
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

## Trim and Extend

**Trim Surface:** select a surface/shell or a named face, then a boundary sketch
or intersecting feature. Boundary curves must lie on the surface; intersecting
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

**Extend Surface:** select one face and a distance, then the U-minimum,
U-maximum, V-minimum and/or V-maximum boundaries. This uses natural continuation
of analytic geometry and C1 continuation of eligible bounded surfaces. Only
rectangular U/V faces are eligible. Arbitrary trimmed contours, holes, unsupported
surface bases and extensions past closed/periodic limits are rejected.

## Sew and Solidify

**Sew Surfaces:** select adjacent surfaces/shells in the tree, or named individual
faces in a model view. Remove unwanted references in the source list. Set an
explicit engineering tolerance between 0.0000001 and 0.1 mm. The output must be
one connected shell; incompatible disconnected inputs and collapsed/nonmanifold
geometry fail. Preview reports unmatched boundaries. The API also reports their
endpoints and lengths so remaining openings or gaps can be located.

Sew preserves valid open shells. Even a closed result remains a shell.
**Solidify Shell** is a separate feature: it requires one closed, consistently
orientable shell, checks its topology and creates a positive-volume solid.
Unmatched boundaries and nonmanifold or invalid shells produce explanations.

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
