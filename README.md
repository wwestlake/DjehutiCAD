# DjehutiCAD

DjehutiCAD is the independent JUCE desktop foundation for a future FRust-programmable 3D CAD and modeling application.

The current foundation provides the desktop shell, dockable-style workspace tabs, LagDaemon authentication controls, FRust editor/runtime integration, and the reusable FRust node editor. The CAD viewport, geometry engine, OpenGL renderer, and modeling nodes are intentionally deferred to later work.

## Build

Configure with CMake from a Visual Studio developer shell. The project expects the existing local JUCE and FrustLang checkouts used by the Djehuti suite; override `JUCE_PATH` and `FRUSTLANG_ROOT` when those locations differ.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --target DjehutiCAD
```

## Scope boundary

This repository contains no SPICE/Xyce integration, electrical solver, schematic editor, PCB editor, machine simulator, or electronics instrument workflow. Those remain in the Electronics Lab application.

## License

DjehutiCAD is released under the MIT License. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.
