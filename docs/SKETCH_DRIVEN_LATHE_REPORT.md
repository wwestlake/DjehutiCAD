# Sketch-driven Virtual Lathe completion

The Virtual Lathe interface now selects an existing sketch and closed contour,
a document X/Y/Z axis or referenced straight sketch line, angle (default 360°),
and reverse direction. Outside/Bore station-entry fields have been removed.
Preview displays the candidate and axis; changing inputs updates an active
preview. Cancel leaves the live document and history unchanged.

Tree sketch selection, tree entity/contour selection and 3D sketch-outline
picking feed the same task. Unrelated open lines and separate closed contours
are excluded from an explicitly selected contour. The original sketch remains
editable. Each feature/parameter edit uses the existing OCAF transaction,
Undo/Redo, rebuild and native/JSON persistence paths.

The history entry is named Virtual Lathe and retains the existing Revolve feature
representation plus Lathe marker. Contour entity IDs and source sketch/axis-line
references are persistent. OCCT builds the planar profile and native revolved
solid; no cylinders or mesh approximations are used for construction. Referenced
axis sketches participate in dependencies and cache invalidation. Legacy saved
Lathe features and station-based API calls remain supported; the new GUI does
not reconstruct geometry from station tables.

## DUB wheel rim

Successfully generated from the supplied eight-segment XZ contour about document
Z at 360°. No running user document was available at the start, so validation
used a sketch fixture named **Wheel Profile for Lathe** and an independently
created live GUI/API fixture.

OCCT validation confirms **one valid annular solid**. Exact cylindrical-face
checks confirm:

- Tire-seat diameter **100 mm**, seat width **26 mm**.
- Outer flange diameter **110 mm**, overall axial width **32 mm**.
- Inner diameter **94 mm**, continuous inner wall and two raised end flanges.
- Full volume **12462*pi mm³**; 180° and reversed 90° turns match half/quarter
  volumes and produce valid solids.

The live application validation rim is saved at
`build/lathe-live/DUB-Wheel-Rim-live.djo`. The application is left open on that
file for inspection. Generated test fixtures and logs remain under `build/`.

## Validation

Debug application build passed. **422 checks passed, zero failures:**

| Group | Passed |
|---|---:|
| Lathe feature/GUI callbacks, dependencies, validity and persistence | 46 |
| Existing Revolve | 38 |
| Existing Revolve GUI | 7 |
| Existing modeling | 83 |
| Existing surface modeling | 139 |
| Workspace | 20 |
| Sketch numerical editing and popup repair | 65 |
| Live API/GUI workflow | 16 |
| Live GUI X-axis and referenced-line-axis workflow | 8 |

Checks cover XY/XZ/YZ plane interpretation, world and referenced line axes,
full/partial/reverse solids, explicit contour selection, invalid geometry and
axes, atomic failures, preview isolation, source/axis regeneration, Undo/Redo,
and native/JSON save/reopen. Live tests also cover editing the original feature
and source line, restoring the original rim, and reopening editable controls.
The live renderer reported an active OpenGL 4.1 context.

## Limits and verification scope

GUI controls, selection callbacks, native viewport pick calculations and the
live application were exercised programmatically through the existing API.
The control layout was inspected in a JUCE component capture. **Actual native
pointer-driven interaction and native 3D screen capture were not performed.**
The component capture does not establish native OpenGL visual verification.
No desktop input-control session was used during this work.

An axis must be coplanar with the selected profile. Axis references require a
nonzero straight line; curved axes are rejected. Crossing, open, degenerate or
axis-crossing profiles are rejected rather than repaired automatically. Ambiguous
contour choices require explicit selection. Existing construction geometry stays
in the sketch and can be used as an axis without becoming part of the profile.
