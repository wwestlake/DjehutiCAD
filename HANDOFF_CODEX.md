# DjehutiCAD Architecture Handoff

Latest sketch update (October 23, 2026): **Sketch Editor usability and entity
management are complete**. The workspace now has a compact floating/dockable
sketch toolbox with persistent View-menu visibility. Completed geometry selects
its shared properties; expandable sketch rows include all stored entities and
synchronize primary/secondary canvas selection. Delete key, tree/canvas context
menus, properties and toolbox use one undoable OCAF entity-delete operation,
including constraint cleanup and dependent-rebuild rollback. Escape cancels a
gesture, returns to Select, then clears selection. New circles require finite
centres and radius >= 0.000001 mm; legacy degenerate entities remain deletable.

Commit `856e7e5` was pushed before live validation. The follow-up fixes a stale
Float/Dock label and presentation error recovery. Debug delivery passed: 36 new
checks, 20 sketch-edit, 20 workspace, 83 modeling and 22 live API/restart checks
(181 total). Native/JSON, legacy deletion, actual preference restart and saved
scene reopen passed. Component captures were visually inspected; mouse/key
callbacks were invoked programmatically. Native pointer/menu/tooltip interaction
and Release remain unverified. Native control was not acquired in this batch;
the owned app was closed with its scene saved. See `docs/SKETCH_USABILITY_REPORT.md`
and `docs/SKETCH_EDITING.md`. Stop after this sketch assignment.

Previous viewport update (October 23, 2026): **Order 6 is complete**. The finite
floor patch and model clipping both caused the short grid range. A procedural
world-XY floor now blends 1/2/5 spacing across the visible plane; model clipping
follows posed bounds and uses logarithmic depth only for wide depth ranges.
Orbit conventions are preserved, pan scales with distance, and Shift+F/context
Fit selection frames selected bodies/instances. Picking uses analytic rays and
positive depth. OCCT/OCAF, modeling and engineering history are unchanged.

Commits `29d18b0` and `57a4f2b` implement and validate the viewport changes.
Debug passed: 59 viewport checks, 20 workspace checks, 83 modeling regressions
and 23 live API/GPU checks (185 total). Floor rendering was verified out to
5,000,000 mm distance and through large pans; a 0.01 mm part remained visible
in a widely separated assembly. Floor GPU cost at 1036x764 was 0.134 ms median,
0.271 ms maximum over 20 warmed samples. No controlled baseline benchmark was
performed. See `docs/VIEWPORT_RANGE_ORDER6.md` for details and reproduction.

Native pointer/visual interaction remains unverified after two capture-helper
timeouts. Native control is released; the owned app was closed with its test
document saved. Float mesh precision at extreme absolute offsets, Release,
other drivers and production-scale assembly performance remain limitations or
unverified behavior. Stop after Order 6; no later batch is authorized.

Previous expansion update (October 23, 2026): **Order 5 is complete**. Surface
Creation (planar/extrude/revolve/sweep/loft), Trim/Extend, Sew/Solidify and Thicken
use the existing modeling command/OCAF architecture. Commits `f855c2e` and
`5b8eb9b` implement and validate the tools; `a5eb63a` rejects periodic-extension
clamping. See `docs/PROFESSIONAL_MODELING_ORDER5.md`,
`docs/SURFACE_MODELING.md` and `docs/SURFACE_MODELING_API.md`.
Six GUI/API/FRust/CAD-node commands expose structured CRUD and isolated previews;
OCAF child 28 stores definitions and engineering JSON is now version 14.
Trim region points have persistent face/UV anchors. Surfaces render from both
sides and expose distinct selectable face references for downstream solid tools.

Final Debug build passed: 139/139 surface checks, 274/274 existing modeling
regressions and 30/30 live HTTP/GUI-control checks (443 total). Native/JSON,
dependency movement, Undo/Redo, actual FRust and compiled nodes, curved extension
and downstream Hole machining passed. Native pointer/visual interaction and
Release remain unverified. The owned test instance was closed with its document
saved. Limits include single-chain profiles, consistent open/closed Loft sections,
individual trim regions, rectangular U/V extension, one connected sewn shell,
straight-path controlled Sweep twist and eligible open-shell/face thickening.
Stop after Order 5; no later batch is authorized.

Previous expansion update (October 22, 2026): **Order 4 is complete**. Sweep, Loft
and Virtual Router/Mill use the shared GUI/API/FRust/CAD-node command architecture.
Commits `202512a` and `5609b53` implement and validate these features. See
`docs/PROFESSIONAL_MODELING_ORDER4.md`, `docs/SWEEP.md`, `docs/LOFT.md`,
`docs/VIRTUAL_ROUTER_MILL.md` and `docs/ADVANCED_MODELING_API.md`.
OCAF feature child 28 stores definitions; engineering JSON is version 13.
The existing OCCT `TKOffset` toolkit is now linked and its Debug library was built;
see `deps/README.md` for provisioning (Release needs its own toolkit build).

Debug build passed; 83/83 new checks, 191/191 existing modeling checks and 28/28
live HTTP/GUI-control checks passed. Real FRust and compiled CAD-node execution,
native/JSON, dependency rebuilding, Undo/Redo and controlled invalid geometry
were verified. Native pointer/visual interaction remains unverified. The owned
test instance was closed with its generated document saved. Limits include one
contour per section, straight-path controlled twist, equal-count manual Loft
correspondence and conservative exterior-plane milling access. Stop after these
three tools; this order does not authorize another batch.

Previous expansion update (October 22, 2026): **Order 3 is complete**. Commit
`2f7d1aa` implements parametric Mirror, Linear Pattern and Circular Pattern through
the shared GUI/structured API/FRust/CAD-node commands. See
`docs/PROFESSIONAL_MODELING_ORDER3.md` for the consolidated report and limits.
OCAF stores definitions in feature child 27; engineering JSON is now version 12.
Automatic feature targets follow source target reassignment. Hole/Cut patterns
reuse the actual source cutter, including through-all at each repeated position;
Boolean Subtract/Union patterns use their removed/added material.

Debug delivery build passed; new focused checks 74/74, existing Hole 27/27,
Chamfer 20/20, Revolve 38/38, Lathe 13/13 and face attachments 19/19 passed.
All 22 live HTTP/GUI-control checks passed. FRust hosts and compiled CAD nodes
were actually executed; the existing FRust prelude's reserved `type` parameter
names were corrected. Native pointer/visual interaction remains unverified.
The owned test application was closed after confirming its generated document
was saved. No further CAD group is authorized by this order.

Previous expansion update (October 22, 2026): Order 2 implements Virtual Lathe
(`95d1cd8`), editable multi-edge Chamfer with isolated previews (`66ae5ce`), and
Hole / Drill Press (`249188d`). Lathe edits outside/bore station profiles using
the existing parametric Revolve. Hole supports planar-face U/V positioning,
blind/through-all cuts, counterbores and countersinks. All use CadCommandService,
OCAF transactions/dependencies/history, native `.djo` and engineering `.djc` JSON
(version 11). The GUI, agent/local API (`cad_lathe`, `cad_chamfer`, `cad_hole`),
FRust hosts and CAD nodes share these commands.

Debug builds passed. Focused checks: Lathe 13/13, Chamfer 20/20, Hole 27/27,
existing Revolve 38/38 and face attachments 19/19. Eighteen live HTTP/GUI-control
checks passed, including analytic STEP volumes, a Lathe -> Chamfer -> Hole chain
rebuilt by station edits, previews, parameter edits, Undo/Redo, native save/reopen,
structured rollback and deletion. Native/JSON roundtrips also passed for each
tool in the focused diagnostics. GUI controls and shared selection callbacks
were exercised programmatically; native pointer/visual inspection remains
unperformed (the prior capture helper timed out twice).

See `docs/VIRTUAL_LATHE.md`, `docs/CHAMFER.md`, `docs/HOLE_DRILL_PRESS.md` and
`docs/PARAMETRIC_REVOLVE.md` for behavior and limits. Lathe station replacement
requires its generated straight profile structure; custom sketches remain
editable through Sketch/Revolve. Chamfer is equal-distance only. Hole needs a
named planar support and uses flat blind bottoms. Topology changes that remove
or split references require reselection and fail atomically. Stop after this
batch; Sweep, Loft and later tools require the next approved order. Older scope
statements below describe earlier milestones.

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

The application now uses the approved pictorial DjehutiCAD icon: a machined
metal gear housing and wireframe emblem in the suite's cyan-violet family.
The canonical raster master is `Resources/djehuti-cad-icon-master.png`; the
transparent derived PNG sizes are 16, 32, 48, and 256 pixels. The earlier D
monogram SVG is removed from the active resource set. JUCE's installed CMake
`ICON_SMALL` and `ICON_BIG` path generates the Windows icon resource; the
Debug build produced `D:\DjehutiCAD\build\DjehutiCAD_artefacts\JuceLibraryCode\icon.ico`
and `D:\DjehutiCAD\bin\Debug\djehuti_cad.exe`. The approved icon was
confirmed embedded by extracting the associated icon from the executable.

The orbit view-matrix correction replaces the previous hand-built basis with
an explicit model-centered look-at basis. Forward is normalized from eye to
target, right is derived from a stable reference-up cross product, and up is
derived from right and forward; translation is computed once from the eye,
with pan applied in view coordinates. The explicit `--cad-camera-check` path
passed the default orientation, the former level quarter-turn, and a nonzero
target case for unit axes, pairwise orthogonality, determinant +1, target
center/depth mapping, and distance preservation. The focused Debug target
built successfully and produced `bin/Debug/djehuti_cad.exe`. No native GUI
surface was available, so the user's visual orbit check remains unperformed.
The recent scale-aware framing and corrected mesh normals were preserved.

Delivery rule: work on a feature branch, perform the focused Debug build and
minimal check, commit and push the feature branch, fast-forward merge into the
repository's existing `main`, push `main`, report to the architect chat, and
stop. Do not start the next phase automatically.
