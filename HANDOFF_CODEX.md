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

Delivery rule: work on a feature branch, perform the focused Debug build and
minimal check, commit and push the feature branch, fast-forward merge into the
repository's existing `main`, push `main`, report to the architect chat, and
stop. Do not start the next phase automatically.
