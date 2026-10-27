# DjehutiCAD agent training cards

Short, task-oriented cards for LLM agents that drive DjehutiCAD through its tool
API (`cad_*` tools, the local HTTP API, FRust host functions and CAD nodes).
The tool catalogue descriptions (`DjehutiAgent.cpp`) are the live reference. These
cards add the workflow, coordinate conventions and worked examples.

Order of work for a feature: **Feature → API → Agent card**. A feature is not
finished until its command is reachable by an agent and has a card here.

| Card | Covers |
|---|---|
| [construction-cursor.md](construction-cursor.md) | Working cursor: read, set, snap, select |
| [construction-references.md](construction-references.md) | Persistent points, axes, coordinate systems and planes |
| [reference-transforms.md](reference-transforms.md) | Move, rotate, align, mirror and scale using references |
| [din-rail-system.md](din-rail-system.md) | Parametric DIN rail, module bottoms, clip and friction screw |
| [tslot-rail.md](tslot-rail.md) | Parametric T-slot rail with per-face slot counts |
| [geometry-export.md](geometry-export.md) | Export to STEP, STL, 3MF, OBJ, PLY, glTF/GLB with units and axes |

Card format: purpose, when to use, commands and parameters, coordinate and unit
conventions, a worked example, and failure modes with their messages.

Note: no earlier card location or template was found in this repository or the
Creation Suite docs when these were written. If the suite keeps cards elsewhere
or in another format, move these there.
