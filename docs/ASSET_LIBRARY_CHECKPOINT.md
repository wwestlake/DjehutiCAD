# Asset Library implementation checkpoint

October 23, 2026. Branch: `codex/asset-library`; base `main` is `ef725a5`.
This is **not a completion report** and has not been merged into main.

Existing functionality: Component Library publishes exact BRep snapshots of
individual designed solids and inserts frozen document-local definitions and
occurrences. It does not publish assemblies or preserve editable feature history.

New implementation is written in `CadAssets`, `CadAssetDocument` and
`CadAssetBrowser`: separate personal JSON collection, category/asset CRUD,
editable feature/sketch snapshots, independent ID-remapped document insertion,
material definitions, document-assembly copies, categorized resizable table,
search, collapsed initial categories, GUI editor, source export and CreationDock
bottom panel. CAD View menus and `cad_asset` use these services. The supplied
Component Library remains intact. See `ASSET_LIBRARY.md` for intended behavior.

Validation is pending. Initial Debug build log: `build/asset-library-build.log`;
two ambiguous var initializers were corrected after their compilation errors.
The initial build may still be compiling later translation units. Finish that
build before starting an incremental build; redirect its output to a new log.
Do not promote this checkpoint to main without successful functional checks.

Next steps:

1. Finish Debug build and repair any remaining compile errors.
2. Run `--cad-asset-check`; inspect backend and nested `ui` results in
   `build/cad-asset-check.result.json`, including the component capture path.
3. Run `--cad-workspace-check` (now includes Asset Library docking/visibility),
   existing component checks and focused modeling regressions.
4. Run the app with `--asset-library <isolated absolute JSON path>` and use its
   local API to exercise GUI save, browse, insert into another document,
   restart, insertion/placement Undo/Redo, and native/JSON reopen. Never print
   the discovery token. `cad_asset` action `ui` exposes real browser callbacks.
5. Inspect captures; correct GUI issues. Verify category/metadata deletion cannot
   change already inserted copies, and malformed assets fail atomically.
6. Update verified results/limitations and commit; merge/push main only when done.

Known scope: assembly saving captures the entire current document assembly,
not a selected nested subgroup. Inserts are independent parametric copies;
ordinary in-document Instance commands remain the linked-definition path.
Analysis cases/project-kit metadata are excluded from asset snapshots. External
controller/kinematic binding behavior needs verification before any claims.

No native input control has been acquired for this assignment. No live test app
has been launched. User files `examples/mechanisms/coil_winder.djo` and
`motor_test_out/` are pre-existing untracked work and must remain untouched.
The original work order is attached at
`C:/Users/wwestlake/.codex/attachments/1fbb0a35-7e0a-49b4-881d-374fc761fc50/Pasted text.txt`.
