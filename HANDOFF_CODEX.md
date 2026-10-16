# DjehutiCAD Architecture Handoff

The live CAD document is OCAF-backed. Djehuti owns stable public feature IDs,
feature semantics, dimensions, units, visibility, and relationships; OCCT
owns solid evaluation, Boolean operations, naming infrastructure, and
tessellation. `CadCommandService` is the single command boundary used by the
workspace, FRust, node graphs, and the BYOK agent.

Each successful modeling command opens and commits one OCAF transaction.
Undo/redo uses the OCAF document history. Render snapshots and the C++ feature
list are derived projections refreshed after commands, undo/redo, JSON import,
and native load. They are not a second editable document.

Native documents use the `.djo` extension and BinOcaf persistence. Existing
`.djc` files remain versioned engineering JSON; loading imports them into OCAF
and saving a `.djc` exports JSON. JSON export/import preserves public IDs,
relationships, parameters, and stored reference metadata.

The current persistent-reference foundation supports the representative
`box-bottom-x-edge` semantic role on box features. The role is stored as an
OCCT `TNaming_NamedShape` under the OCAF reference label and is updated with
Generated/Modified/Deleted naming evolution as the document rebuilds. It
resolves against the current placed document shape after a box dimension or
placement change and explicitly rejects unsupported, deleted, or ambiguous
roles. It is not yet a general topology-selection system and there is no new
reference-selection UI.

The focused Debug application target was built successfully and produced
`bin/Debug/djehuti_cad.exe`. The existing command service was exercised through
the application's explicit `--cad-foundation-check` diagnostic mode. The check
passed one box plus one cylinder subtraction, representative-reference
creation, dimension edit, reference resolution, OCAF undo/redo, native `.djo`
save/reopen, and JSON export. It writes only a temporary result marker and
does not affect normal application startup. This is service/kernel verification,
not GUI workflow verification. OCAF is complete for this document foundation;
sketches, assemblies, and new modeling tools are separate authorized work.

The dimensioned-sketch part-design slice adds bounded XY/XZ/YZ datum-plane
sketches with line, rectangle, and circle entities. Rectangle creation adds
horizontal/vertical/coincident and driving distance constraints; rectangle
width/height and circle radius/diameter edits update their driving constraint
and regenerate connected geometry. Numeric entity position/center edits,
finite/reverse cuts, through-all cuts, and finish/cancel sketch controls are
available through the compact workspace. Closed planar profiles produce real
OCCT faces, `extrude` produces a real prism solid, and `cut` subtracts a
sketch tool from an explicit solid target. Public feature IDs and sketch
dependencies are stored in both `.djo` OCAF labels and `.djc` JSON version 3.
FRust, node, and BYOK-agent command surfaces expose the same command-service
operations, including sketch position edits.

The updated `--cad-foundation-check` diagnostic creates a 120 x 80 x 8 mm
mounting plate, cuts four 10 mm through-holes, changes the driving width to
140 mm, checks bounds and volume, rejects an invalid edit without changing
the valid model, exercises undo and redo, reopens the native document, and
checks the exported JSON constraint value. It passed with the temporary
marker `CAD_SKETCH_FOUNDATION_CHECK_OK`. This is service/kernel verification,
not GUI verification. Remaining limits are no arbitrary face-attached sketch
support, no general topology-selection UI, and no loft/sweep/revolve or
assembly system.

The final sketch UI correction adds endpoint snapping in an 8-pixel screen
tolerance with visible snap feedback. Snapped line starts and ends create
coincident constraints in the same command transaction as the line, keeping
the gesture undoable as one operation. Shift-click selects a second entity;
the compact constraint menu exposes horizontal, vertical, coincident,
fixed/origin, length, radius, and diameter commands with validation/status
feedback. Escape and Cancel discard only an unfinished gesture, while Finish
returns the canvas to selection mode and requires explicit re-entry.

The affected Debug application target built successfully. Interactive pointer
and keyboard GUI verification remains unperformed; source-level dispatch and
build verification cover the new UI paths. The previously passing mounting
plate service diagnostic was intentionally not rerun for this UI-only slice.

The follow-up workspace correction attaches the constraint-type selector and
the full property/action control set to a dedicated scrollable JUCE viewport.
The modeling toolbar is split across two rows so the primary actions remain
reachable without increasing the existing default or minimum window size.
Focused source inspection confirmed the selector child relationship, viewport
content sizing, and responsive bounds. The launched native application was not
exposed through the available UI automation surface, so visual pointer and
keyboard verification of the repaired layout remains unperformed. The prior
service diagnostic was not rerun because this correction is UI-only.

The viewport framing correction replaces the cube-sized camera limits with
per-view bounds derived from immutable render-snapshot mesh vertices. Initial
nonempty snapshots and explicit Reset/Fit All center the orbit target on the
visible model and compute distance from the viewport aspect ratio, field of
view, and a margin. Zoom is proportional and bounded from scene scale rather
than an arbitrary 12-unit ceiling. Near and far clipping are derived from the
current camera distance and scene radius. Bounds refresh on snapshot revisions
without refitting an already-used camera, preserving ordinary user edits;
resize refits only a view that has not been manually changed. The focused
Debug application target built successfully and produced
`bin/Debug/djehuti_cad.exe`. No native GUI surface or existing camera-math
diagnostic was available for pointer/visual verification, so Fit All, zoom,
off-origin centering, and per-tile aspect behavior remain source-reviewed and
build-verified rather than GUI-observed.

The mesh extraction correction now obtains per-node normals from OCCT surface
properties at triangulation UV coordinates, applies face reversal and surface
location transforms, and checks each emitted triangle's winding against the
oriented normal before publishing the existing position/normal/index layout.
When UV normals are unavailable, nondegenerate triangle normals are used as a
finite fallback; degenerate triangles are omitted. This preserves separate
face boundaries while allowing curved surfaces to vary their normals. The
focused Debug application target built successfully and produced
`bin/Debug/djehuti_cad.exe`. Native GUI access was unavailable, and there is
no existing direct mesh-orientation diagnostic path; therefore runtime
normalization, exterior/hole direction, and visual culling behavior are
source-reviewed and build-verified rather than directly observed. No prior
CAD workflow, camera, solver, or renderer regression diagnostic was rerun.

The application now has a DjehutiCAD-specific Windows icon. The editable vector
master is `Resources/djehuti-cad-icon.svg`, with transparent PNG source sizes
at 16, 32, 48, and 256 pixels. JUCE's installed CMake `ICON_SMALL` and
`ICON_BIG` path generates the Windows icon resource; the Debug build produced
`D:\DjehutiCAD\build\DjehutiCAD_artefacts\JuceLibraryCode\icon.ico` and
`D:\DjehutiCAD\bin\Debug\djehuti_cad.exe`. `Icon.ExtractAssociatedIcon()`
successfully extracted the icon from the executable, confirming embedding.

Delivery rule: work on a feature branch, perform the focused Debug build and
minimal check, commit and push the feature branch, fast-forward merge into the
repository's existing `main`, push `main`, report to the architect chat, and
stop. Do not start the next phase automatically.
