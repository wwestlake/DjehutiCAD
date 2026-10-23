# Personal Asset Library

The **Component Library** remains the supplied/standardized catalog (including
legacy custom BRep entries). The separate **Asset Library** stores user designs
as editable engineering documents. Existing catalog data is not migrated or
removed automatically.

Open **View > Asset Library** in the application menu, or **View > Asset Library**
in CAD Workspace. **CAD Workspace > View > Save to Asset Library...** opens the
save editor for the selected solid or occurrence. The window uses CreationDock,
prefers bottom docking, and supports closing, floating, resizing and repositioning.
The existing workspace layout preferences remember its placement and visibility.

Select a save source, enter an asset name/category and optional description or
material note, then **Save new asset**. Assigned engineering material names are
taken from the source rather than inferred. Part assets contain the selected
solid and upstream sketches/features. **Entire document assembly** captures the
document's feature definitions and authored occurrences/assembly structure;
publishing an arbitrary selected subassembly is not offered in this version.
Saving leaves the design document unchanged.

The categorized table starts collapsed on each new application presentation.
Click a category to expand/collapse it, or use **Expand All / Collapse All**.
Search reveals matching asset rows with their categories. Columns resize and
show type, known material information, bounding dimensions and modification time.
Dimensions are X/Y/Z bounding extents, not inferred machining dimensions.
Unavailable metadata is left blank. Category names and organization persist;
expansion is session presentation state.

Use the category editor to create/rename a category or delete an empty one.
Selecting another category in the asset editor and **Update metadata** moves the
asset. **Remove asset** removes only its library entry. Duplicate asset names
within a category are rejected, case-insensitively. **Replace selected** is the
explicit operation for replacing the selected entry's saved design while keeping
its stable asset ID. Existing inserted copies never change implicitly.

In another CAD document, find/select an asset, enter X/Y/Z in millimetres and
choose **Insert copy**. Parts insert an independent parametric feature graph and
a normal CAD occurrence. Repeated insertions get independent feature/sketch IDs.
Use normal CAD properties, sketch editing and instance placement for subsequent
edits; insertion and placement use OCAF Undo/Redo. Assemblies insert their authored
occurrences and an ordinary assembly group. Normal **Instance** commands remain
the explicit way to share a part definition within a receiving document.
There is no live link to the external library or original design file.

**Export editable copy** writes a new `.djc` engineering document that can be
opened using File > Open. It provides access to the saved feature history without
requiring the original design document or modifying the library.

Storage defaults to `%APPDATA%/DjehutiCAD/asset-library.json`. This separate,
versioned metadata collection embeds existing engineering JSON, including the
required document-local component and material definitions. It uses atomic file
replacement and rejects writes after detecting an external change. Reload before
retrying. Invalid/missing asset definitions are rejected before insertion; the
supported library file size is 256 MB. `--asset-library <absolute JSON path>`
selects an isolated collection for testing. Native and engineering JSON document
formats are unchanged.

The `cad_asset` tool shares these operations: `list`, `reload`, `createCategory`,
`renameCategory`, `deleteCategory`, `save`, `get`, `insert`, `update`, `delete`,
and `exportSource`. `save` takes `source` (feature/occurrence ID or `assembly`),
`name`, category ID, optional description/material note and optional replacement
asset ID. `insert` takes asset ID and optional placement in mm/degrees. `ui`
exposes the actual browser's fields/buttons for focused GUI-control testing.

Diagnostics: `--cad-asset-check` checks independent documents, parametric edits,
native/JSON persistence, insertion/placement Undo/Redo, category safeguards,
metadata removal, assemblies, corruption handling and actual browser callbacks.
`--cad-workspace-check` checks registration, float/dock and visibility restoration.
Programmatic component renders and callbacks are distinct from native pointer
interaction. Consult the delivery/checkpoint report for verified results.
