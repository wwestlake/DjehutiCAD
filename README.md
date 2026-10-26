# DjehutiCAD

DjehutiCAD is an independent FRust-programmable mechanical CAD application built with JUCE, Open CASCADE/OCAF, and modern OpenGL.

The application includes sketch/solid modeling, Boolean operations, an authoritative OCAF document with undo/redo, multiple 3D views, mechanical joints/couplings and constraint solving, local inverse-kinematics targets, a DEWB motion interface, FRust/node integration, and a BYOK agent. Capabilities and recorded verification limits are described in the documentation and handoff.

Modeling tools include Box, Cylinder, Extrude, Cut, Union/Subtract/Intersect,
constant-radius Fillet, and [Parametric Revolve](docs/PARAMETRIC_REVOLVE.md).
Revolve supports full/partial angles, selectable/custom axes, editable parameters,
sketch-driven rebuilding, and shared GUI/agent/API commands. Editable NURBS
surfaces remain future work.

[Virtual Lathe](docs/VIRTUAL_LATHE.md) turns selected closed sketch contours into
parts, shoulders and grooves using the same parametric Revolve implementation.

[Chamfer](docs/CHAMFER.md) adds editable equal-distance, multi-edge bevels and
read-only previews through the GUI and shared command/API system.

[Hole / Drill Press](docs/HOLE_DRILL_PRESS.md) provides face-based blind/through-all
holes, counterbores, countersinks and editable numeric positioning.

[Mirror](docs/MIRROR.md), [Linear Pattern](docs/LINEAR_PATTERN.md) and
[Circular Pattern](docs/CIRCULAR_PATTERN.md) provide editable body repetition and
supported additive/subtractive feature repetition, with shared previews, GUI,
structured APIs, FRust hosts and CAD nodes. See the
[version 12 interchange conventions](docs/PATTERN_API_AND_INTERCHANGE.md). The
[Order 3 completion report](docs/PROFESSIONAL_MODELING_ORDER3.md) records validation
and current limitations.

[Sweep](docs/SWEEP.md) moves a closed profile along sketch/model-edge paths;
[Loft](docs/LOFT.md) joins ordered sections; [Virtual Router / Mill](docs/VIRTUAL_ROUTER_MILL.md)
provides editable pockets, slots and profile cuts. All share GUI previews,
structured APIs, FRust hosts, CAD nodes and [version 13 interchange](docs/ADVANCED_MODELING_API.md).
The [Order 4 completion report](docs/PROFESSIONAL_MODELING_ORDER4.md) records
verification and current limitations.

[Gears](docs/GEARS.md) generates editable involute spur gears as real solids and
compatible spur pairs (centre distance, phase, joints and the existing gear
coupling) through Create > Gear / Gear Pair, `cad_gear` and `cad_gear_pair`.

[Moving parts, camera and components](docs/PLACEMENT_AND_COMPONENTS.md) covers the Move / Rotate
manipulator (T), orbit about the point under the pointer, standard views, inserting and placing
library components, and multi-part components such as a stator and rotor on a revolute axis.

[Construction references](docs/CONSTRUCTION_REFERENCES.md) adds a movable working cursor (3D and sketch), persistent
point/axis/coordinate-system/plane references that can follow bodies, occurrences and cylindrical faces, and
reference-based move, rotate, align, mirror and scale with an explicit Create copy option (`cad_reference`, `cad_transform`).

[Surface modeling](docs/SURFACE_MODELING.md) provides direct Planar, Extruded,
Revolved and Ruled Surface workspace commands with selected curve/wire sources,
live drafts and normal property editing. It also retains swept and lofted
surfaces, region trimming, selected-edge/toward-curve extension, independently
selectable split regions,
shell sewing, separate solidification, and positive/negative/symmetric thickening.
These share GUI previews, APIs, FRust hosts, CAD nodes and
[version 14 interchange](docs/SURFACE_MODELING_API.md).
The [Order 5 completion report](docs/PROFESSIONAL_MODELING_ORDER5.md) records
verified behavior, test counts and geometric limits.
The [surface creation Phase 1 report](docs/SURFACE_CREATION_PHASE1.md) covers
the four direct creation tools and their current verification limits.

The 3D viewport uses a procedural XY floor, adaptive model clipping and
selection framing. See [viewport controls](docs/WORKSPACE_TOOLS_AND_GUIDES.md)
and the [Order 6 rendering report](docs/VIEWPORT_RANGE_ORDER6.md).

[Sketch editing](docs/SKETCH_EDITING.md) includes a floating/dockable tool palette,
expandable entity rows, automatic creation selection and undoable entity deletion.
[Modeling workflow controls](docs/MODELING_WORKFLOW_UI.md) add persistent Modify/Snap
toolbars, ghosted read-only sketch/body references, native geometric snapping, and
linked live symmetry or independent/linked mirroring about movable sketch axes
and construction lines.

The [Materials and Sheet Stock Manager](docs/MATERIALS_AND_SHEET_STOCK.md) provides reusable engineering catalogs,
document material assignments, and portable stock snapshots. Open it from CAD Workspace → Edit → Materials and Sheet Stock.

Independent protected-source engineering setups are available from **File → New Test Fixture** and **Window → Test Fixtures**.
See [Mechanical test fixtures](docs/MECHANICAL_TEST_FIXTURES.md) for setup, persistence, available methods and solver limitations.

## Build

Configure with CMake from a Visual Studio developer shell. The project expects the existing local JUCE and FrustLang checkouts used by the Djehuti suite; override `JUCE_PATH` and `FRUSTLANG_ROOT` when those locations differ.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --target DjehutiCAD --parallel 1
```

## Scope boundary

See [mechanism constraints](docs/MECHANISM_CONSTRAINTS.md), [inverse kinematics](docs/INVERSE_KINEMATICS.md), and [the implementation handoff](HANDOFF_CLAUDE.md). The mechanism examples are in `examples/mechanisms`; open a document through File → Open CAD Document, then use the Assembly tree and IK panel.

This repository contains no SPICE/Xyce integration, electrical solver, schematic editor, PCB editor, machine simulator, or electronics instrument workflow. Those remain in the Electronics Lab application.

## License

DjehutiCAD is released under the MIT License. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.
