# Modeling workflow UI and symmetry — completion report

Implemented the toolbar/reference/snapping work order and its Sketch symmetry addendum. OCAF and the existing command service remain authoritative; Virtual Lathe geometry and the constraint solver's existing solving rules are unchanged.

## Delivered

- **Modify:** all 22 existing Modify operations, shared menu dispatch, vector icons/tooltips, active/disabled states, dragging, horizontal/vertical orientation, docking and visibility controls.
- **Snap:** horizontal toolbar with Grid, Vertex, Edge, Midpoint, Center, Intersection, contextual planar Face and Off. Native curves/vertices/solid sections supply targets. Eight-pixel tolerance and deterministic priorities drive the displayed and committed coordinates. Snapping creates no implicit constraints.
- **References:** global/per-source sketch and body controls, opacity, read-only selection/highlighting, resolved sketch frames, body placements/occurrences, and current mechanism pose. Ordinary model display returns after Sketch.
- **Symmetry:** Off/Horizontal/Vertical/Both; movable numeric/snapped centre; construction guides; reflected drawing previews; linked source-driven geometry; independent mirrors; construction-line axes; editable/deletable relationships. Exact duplicates are suppressed and partial line overlaps fail atomically. Supported primitives are lines, rectangles, circles and cubic Bezier curves.
- **Persistence/API:** toolbar and snap/reference preferences use existing application settings. Symmetry/centres/axis references use the existing sketch/OCAF payload in native and JSON documents, with Undo/Redo and dependent feature rebuilds. `cad_ui` and the existing nested workspace UI bridge expose the same operations; tool descriptions and guides are updated.

## Validation

| Validation | Result |
|---|---|
| Debug build | Passed |
| New focused UI/document assertions | 134 passed |
| Existing sketch line, usability and editing assertions | 121 passed |
| Existing Lathe, Revolve, modeling and replication assertions | 241 passed |
| Existing workspace assertions | 20 passed |
| Live API/application assertions, including actual restart | 14 passed |

**530 assertions passed** across these checks. The T-slot case authors ten quadrant lines, generates forty connected outline segments, edits the source, rebuilds an extrusion, and saves/reloads native and JSON documents. Additional checks cover primitive reflection, arbitrary construction axes, independent edits, duplicate/overlap handling, read-only reference selection, all planar snap categories, exact planar face refinement, toolbar dispatch and drag/dock callbacks.

The live running application created an XZ tire-reference sketch on a temporary rim copy, committed the indicated rim snap, edited a source dimension in the symmetrical T-slot through numerical properties, reopened the saved result and verified toolbar/snap preferences after restart. Original rim SHA-256 remained `8e2d9429c19ec02c698363c766a24eb21260aa499b3d6023b2617b406bd50120`. Test instances were closed normally and prior application settings restored byte-for-byte. No native machine-control session was acquired.

## Limits and unverified behavior

- GUI checks used real JUCE callbacks, running-application APIs and inspected component captures. Native pointer/visual interaction was not performed.
- Face snapping is contextual to planar Drill Press point placement. Sketch references are orthographic projections/sections in the existing 2D sketch canvas.
- The current Sketch editor has no separate arc drawing primitive. Linked copies cannot drive other mirrors; edit the independent source instead. Source/axis changes that would break dependent features roll back.
- Existing Extrude adds separate nested closed loops. Use the existing Cut operation for an actual central bore; this order does not change that modeling behavior.
- The shared workspace bridge was exercised. New actions were not separately executed as compiled FRust programs or compiled CAD-node graphs. Very large assembly reference performance was not benchmarked.

See [workflow guide](MODELING_WORKFLOW_UI.md) and [API actions](LOCAL_AUTOMATION_API.md#modeling-workflow-ui-and-symmetry).
