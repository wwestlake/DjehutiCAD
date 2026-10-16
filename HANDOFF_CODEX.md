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
`box-bottom-x-edge` semantic role on box features. It resolves by geometry
role after a box dimension change and explicitly rejects unsupported or
ambiguous roles. It is not yet a general topology-selection system.

Verification for the OCAF migration is intentionally minimal: use one box,
one cylinder, and one subtraction to check dimension edit, OCAF undo/redo,
reference resolution, native save/reopen, and JSON export. Do not describe
interactive GUI behavior as verified unless that workflow has actually been
observed. OCAF is complete for this document foundation; sketches, assemblies,
and new modeling tools are separate authorized work.

Delivery rule: work on a feature branch, perform the focused Debug build and
minimal check, commit and push the feature branch, fast-forward merge into the
repository's existing `main`, push `main`, report to the architect chat, and
stop. Do not start the next phase automatically.
