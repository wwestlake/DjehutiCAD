# Order 4 completion report — October 22, 2026

Sweep, Loft and Virtual Router/Mill are implemented through the shared OCCT/OCAF
command architecture. Implementation commit: `202512a`; GUI/validation follow-up:
`5609b53`. All three appear in Modify, provide isolated Preview and Apply/Cancel,
and reopen from **Edit sources and parameters** in the result inspector. Normal
GUI deletion is undoable.

| Tool | Delivered behavior |
|---|---|
| Sweep | Closed planar profile; straight, curved and connected multi-segment paths; sketch lines/circles/Bezier and persistent model-edge arcs/curves; transport/Frenet/fixed-binormal orientation, roll, straight-path twist, reverse and corner treatment; new/add/subtract. |
| Loft | 2–32 ordered closed section sketches; GUI reorder/removal; smooth/ruled transitions; auto/manual edge correspondence, cyclic offsets, reversal and circular seams; new/add/subtract. |
| Router/Mill | Planar-face/principal/custom entry references; editable cutter diameter and blind/through depth; rounded pockets, open-path slots and closed profile cuts with on/inside/outside compensation; geometric material removal and access validation. |

`cad_sweep`, `cad_loft`, `cad_mill` expose structured CRUD, reference inspection
and isolated previews. FRust hosts and compiled CAD nodes execute the same
CadCommandService methods. OCAF remains authoritative, with transactions,
Undo/Redo, source/target/reference dependency rebuilding and engineering history.
Native `.djo` and version 13 `.djc` JSON preserve all definitions. OCCT's existing
`TKOffset` toolkit was built in Debug and linked; provisioning instructions are
in [native dependencies](../deps/README.md).

Validation passed:

- Debug build and **83/83** new focused checks: analytic and valid geometry,
  arcs/Bezier/multi-segment and closed sweeps, controlled twist and persistence,
  Loft ordering/correspondence, all machining strategies, source/target changes,
  invalid geometry/access rollback, Undo/Redo, native/JSON reopen, GUI controls
  and selection callbacks, real FRust and compiled CAD-node execution.
- **191/191** existing checks: replication 74, Hole 27, Chamfer 20, Revolve 38,
  Lathe 13 and face attachments 19.
- **28/28** live HTTP/GUI-control checks: creation/editing/deletion, previews,
  exact STEP volumes, rebuilding, native reopen, Undo/Redo, stock visibility,
  invalid access/frame failures and fixed-binormal/reversed-path sweeping.
  The twisted sample exported at 79.9999987627 mm³ versus analytic 80 mm³
  (absolute error approximately 0.00000124 mm³).

GUI forms and selection callbacks were exercised programmatically in the running
application. **Native pointer/visual interaction was not verified.** The owned
test application was closed after confirming its generated document was saved.

Limits: one closed contour per profile/section; controlled nonzero Sweep twist
uses straight paths in Transport mode; manual Loft correspondence requires equal
edge counts. Milling uses a flat-ended axial cutter and conservatively requires
an entry plane on/outside all target material on the approach side. Narrow or
disconnected inaccessible regions are rejected. This is geometric machining,
without CAM output or machine dynamics. Complex topology changes may require
reference reselection. No failing checks remain in the exercised scope.

Guides: [Sweep](SWEEP.md), [Loft](LOFT.md), [Router/Mill](VIRTUAL_ROUTER_MILL.md),
[API and interchange](ADVANCED_MODELING_API.md). This three-tool batch is complete;
later work requires a new order.
