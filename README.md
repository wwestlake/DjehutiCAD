# DjehutiCAD

DjehutiCAD is an independent FRust-programmable mechanical CAD application built with JUCE, Open CASCADE/OCAF, and modern OpenGL.

The application includes sketch/solid modeling, Boolean operations, an authoritative OCAF document with undo/redo, multiple 3D views, mechanical joints/couplings and constraint solving, local inverse-kinematics targets, a DEWB motion interface, FRust/node integration, and a BYOK agent. Capabilities and recorded verification limits are described in the documentation and handoff.

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
