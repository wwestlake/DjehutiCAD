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

[Surface modeling](docs/SURFACE_MODELING.md) provides planar, extruded, revolved,
swept and lofted surfaces, region trimming, eligible U/V boundary extension,
shell sewing, separate solidification, and positive/negative/symmetric thickening.
These share GUI previews, APIs, FRust hosts, CAD nodes and
[version 14 interchange](docs/SURFACE_MODELING_API.md).
The [Order 5 completion report](docs/PROFESSIONAL_MODELING_ORDER5.md) records
verified behavior, test counts and geometric limits.

The 3D viewport uses a procedural XY floor, adaptive model clipping and
selection framing. See [viewport controls](docs/WORKSPACE_TOOLS_AND_GUIDES.md)
and the [Order 6 rendering report](docs/VIEWPORT_RANGE_ORDER6.md).

[Sketch editing](docs/SKETCH_EDITING.md) includes a floating/dockable tool palette,
expandable entity rows, automatic creation selection and undoable entity deletion.

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
