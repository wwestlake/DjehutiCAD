# Modeling toolbars, reference geometry and sketch symmetry

The modeling viewport has a **Modify toolbar** with the same 22 operations as the Modify menu: Extrude, Revolve, Virtual Lathe, Cut sketch, Fillet, Chamfer, Drill Press, Mirror, Linear Pattern, Circular Pattern, Sweep, Loft, Router/Mill, Create/Trim/Extend/Sew/Solidify/Thicken Surface, Subtract, Union and Intersect. Icons have tooltips and active-command indicators. Commands are disabled while editing a sketch, as in the menu.

Drag a toolbar by its grip. Modify supports horizontal/vertical layouts and docking at the corresponding viewport edges. Snap stays horizontal and docks at the top/bottom. Its first-use default is the bottom dock, avoiding existing floating Sketch-toolbox positions. Dock/Float buttons, close buttons and View menu entries control presentation. Layout, visibility, orientation, snap preferences and reference preferences use the existing application settings file; normal application shutdown persists them.

## Reference geometry

While editing a sketch, use the eye button on **Snap**, or View > Reference geometry. Toggle other sketches, solid bodies, individual sources, opacity, snapping to references and read-only reference selection. Active geometry remains brighter than reference sketch lines, native body cross-sections, projected body edges and subdued body fills. A zero opacity excludes reference snaps. Explicitly enabling an individual source can show a source hidden in modeling mode.

Sketch references use the document's resolved sketch frames. Body references include occurrence placements and the currently shown mechanism pose. Reference display is an orthographic projection into the active sketch plane, with native solid/plane section edges as well as projected edges. Solid fill is a display mesh; snapping uses native OCCT geometry and double precision. Changing UI visibility never changes document geometry or body visibility. Leaving Sketch returns to ordinary model rendering.

When reference selection is enabled, tree clicks and reference-canvas picks highlight the reference without entering its sketch or modifying it. Finish the current sketch before deliberately editing a different source. With reference selection off, reference tree clicks do not open editing inspectors; reference context actions show the visibility controls instead.

## Precision snapping

Enable multiple modes on Snap: **Grid, Vertex, Edge, Midpoint, Center, Intersection**, plus **Face** for Drill Press face-point placement. Off temporarily disables snapping while retaining individual preferences. Plane modes are available only in Sketch; Face is available only during supported planar face-point placement.

Tolerance is eight logical screen pixels. Priority is vertex, intersection, center, midpoint, nearest edge, grid; nearest distance breaks ties within a category, then stable source traversal order. Edge targets are bounded native curves; midpoint uses curve arc length; intersection uses native projected curves. Markers and labels identify the chosen target. Commit uses those same local coordinates. Hidden/disabled references do not contribute targets. Snapping adds no coincidence constraints. Use the existing Constrain command to maintain a relationship after placement. Rectangle's own explicit rectangle constraints remain.

To draw a tire profile, open the saved wheel, create a new sketch in the required section plane, enable the rim as a solid reference, enable relevant snaps, and draw using the rim's section or projected edges. Finish Sketch to return to Modify. The rim is never edited by reference display/snapping.

## Symmetry and Mirror Selection

Click **Sym** on the floating Sketch toolbox to expand its compact controls. The collapsed button shows H, V or H+V while live symmetry is enabled. Mode is Off, Horizontal, Vertical or Both, in the active sketch's local horizontal/vertical coordinate system on XY, XZ, YZ or resolved attached planes.

Enter Center X/Y and click **Set centre** (or Enter). **Pick** chooses a centre using the current snap modes; Escape cancels it. **Origin** resets the centre. Subdued dashed guides show the active axes. Edge snapping also targets these axes; Intersection snapping targets their centre in Both mode. Both mirrors a newly drawn quadrant into the other three. Preview includes reflected lines, rectangles, circles and cubic Bezier curves. These are the existing supported Sketch primitives; there is currently no separate arc drawing tool.

New geometry created with live symmetry produces stored linked relationships. Turning symmetry Off affects subsequent drawing; existing links remain. Changing the live centre updates existing live relationships. Source edits regenerate their copies; copies reject direct edits with a source-edit instruction. Source constraints continue to drive the source. Links use the existing sketch/OCAF transaction, dependent-feature rebuild, Undo/Redo, native save and engineering JSON paths.

For existing geometry, check the desired independent sources in the list, choose Horizontal, Vertical, Both or a construction line, choose Linked/Independent, then **Mirror checked entities**. The list supports any number of sources; regular Shift selection preselects entities. **Toggle selected construction line** turns an ordinary independent line into a guide excluded from solid profiles. Construction-line mirrors follow edits to that axis. A linked copy cannot drive another mirror or act as its axis.

The relationship selector provides **Update link** (axis/centre) and **Delete link**. Deleting a link removes its generated copies. Deleting source entities removes their copies; deleting a referenced construction axis fails safely until its links are removed. Independent mirror copies are ordinary, freely editable entities and retain that behavior across save/reopen. Exact coincident copies are suppressed, including circles at the centre and lines on an axis. Partially overlapping reflected lines are rejected; trim the source to the intended half/quadrant first.

For a four-sided T-slot profile, author the upper-right quadrant from one axis to the other, including half of the top slot and half of the right slot. Both produces a closed four-sided outline. Draw the central circle with symmetry Off (a circle exactly at the centre is also deduplicated in Both). Existing Extrude treats separate closed loops additively; use the existing Cut operation for an actual bore. This order does not change that modeling behavior.
