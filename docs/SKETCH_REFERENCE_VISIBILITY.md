# Sketch Reference Visibility

While a sketch is open, other bodies, assembly instances and sketches can be
shown behind it as read-only reference geometry for display and snapping. Each
sketch keeps its own choice.

## Behaviour

- A new sketch shows no existing model geometry. It displays only its own
  geometry and the sketching aids.
- While a sketch is open, every item in the model tree that can be drawn as a
  reference has an eye control. This covers solid bodies, generated features
  such as gears, other sketches, and assembly instances. Clicking an eye shows
  or hides that item in this sketch only.
- Assembly bodies are offered through their instances (one control per
  displayed body). The open sketch itself, joints, couplings and features built
  from the open sketch have no eye.
- Right-click on an item while sketching gives **Show in this sketch** /
  **Hide in this sketch** and **Reference geometry settings...** (the existing
  panel with the global sketch/solid toggles, snapping and opacity). Items that
  cannot be shown get a disabled entry.
- Sketch-reference visibility is independent of 3D visibility. A gear hidden in
  3D can be shown in a sketch, and showing it does not change its 3D visibility,
  geometry, placement or assembly data. Outside a sketch, the eyes and **Hide**
  keep controlling 3D visibility as before.
- The choice is stored in the sketch's document record (`references`, a list of
  stable feature, sketch or instance ids). It is saved in .djo and .djc files,
  restored when the sketch is reopened, and each change is one undoable command.
  The list is not part of the geometry signature, so changing it rebuilds
  nothing.

## Display preparation

`SketchReferenceScene` prepares only the visible solids. Each solid's prepared
curves, snap points and shaded fill are cached against:

- the solid's exact shape identity (held alive by the cache entry)
- its placement and instance pose
- the sketch plane

Sketch edits, visibility toggles, undo of unrelated changes and idle repaints
reuse the cache. Editing the referenced body produces a new shape, so only that
body is prepared again. Hidden solids keep their prepared data so they can be
shown again quickly. Deleted or regenerated sources drop out of the cache.

The plane section is computed only for faces that cross the sketch plane.
Faces lying on the plane are already shown by their edges. A face swept
straight along the sketch normal is an extrusion side wall: a plane, cylinder
or extrusion surface bounded by two profile levels. Its exact cut is its
profile edge moved onto the plane. All other crossing faces use the OCCT
section.

## Validation

`bin\Debug\djehuti_cad.exe --cad-sketch-reference-check` writes measurements and
checks to `build/cad-sketch-reference-check.result.json`.
