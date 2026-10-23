# Order 6 — Viewport rendering and navigation

Completed October 23, 2026. Scope is viewport rendering/navigation only; OCCT,
OCAF, modeling commands and engineering history are unchanged.

## Cause and changes

The old floor was a finite 49-by-49 line patch (24 grid intervals each side),
centered on the camera target rather than the panned view. Major-line emphasis
also moved with the patch. Model-based near/far planes could clip the floor
before its geometric edge. Thus both finite geometry and clipping contributed.
The old fixed minimum near plane and scene-relative minimum zoom also limited
inspection of small parts within large assemblies.

The floor now uses a three-vertex procedural background pass. It intersects
camera rays with world XY at Z=0, blends world-aligned 1/2/5 spacing levels,
preserves five-interval major lines and origin axes, and fades at grazing angles.
There is no finite patch edge or model far-plane cutoff. Geometry always draws
over the subdued floor; existing guide visibility controls remain available.

Clipping now follows posed model bounds in camera depth. Ordinary views retain
linear depth with tight bounds; ranges above 50,000:1 use logarithmic fragment
depth. Picking compares positive view depth and constructs rays analytically,
avoiding unstable inverse projection at extreme far distances. Navigation keeps
its existing orbit and wheel conventions; pan scales with distance. Fit respects
viewport aspect, and Shift+F/context-menu **Fit selection** frames selected
bodies or solved instances independently of assembly size.

Camera state exposes clipping/grid diagnostics. An asynchronous GPU PNG capture
uses an isolated framebuffer and does not change the document. See the
[camera API](LOCAL_AUTOMATION_API.md) and
[viewport guide](WORKSPACE_TOOLS_AND_GUIDES.md).

## Verified behavior

| Validation | Result |
|---|---|
| Focused Debug build | Passed; output redirected to `build/order6-build-capture.log` |
| Viewport range diagnostic | 59/59 passed |
| Existing workspace diagnostic | 20/20 passed |
| Existing modeling regression | 83/83 passed |
| Live local API / GPU checks | 23/23 passed |

**185 checks passed.** Automated viewport cases cover synthetic component sizes
from 0.0001 to 10,000,000 mm, distant views, portrait fit, posed instance framing,
orbit/pan/wheel callbacks, body/face/region picking and guide-state persistence.

Live OpenGL 4.1 tests rendered the floor at camera distances of 0.0001, 200,
10,000 and 5,000,000 mm, plus a 2,000,000 mm pan at five-degree elevation. GPU
PNGs were checked programmatically and inspected visually: no finite patch edge
was present, and the horizon faded smoothly. A real 0.01 mm box remained visible
and highlighted with a second body positioned 1,000,000 mm away; the selected
view's depth ratio was approximately 19.8 million:1. Fit, reset, native reopen,
invalid-distance rejection, floor toggle and unchanged engineering revision
passed. Camera movement caused no model geometry uploads.

At 1036×764, the floor pass measured **0.134 ms median / 0.271 ms maximum** over
20 warmed samples during orbit. Work is proportional to viewport pixels rather
than viewing distance. This measures the new pass on this machine; no controlled
before/after whole-application benchmark was performed.

Reproduce the automated checks with `--cad-viewport-range-check`,
`--cad-workspace-check` and `--cad-modeling-check`. For live checks, start the
Debug app with an empty, unmodified document and run
`tools/cad_api/viewport_range_check.py` using Python with Pillow. Artifacts and
the saved test scene are under `build/order6-live/`.

## Limits and delivery

Native pointer/visual interaction remains **unverified**: the native window
capture helper timed out twice. GPU readback and programmatic GUI callbacks are
distinct from native interaction. Native control was released, and the owned
test instance was closed after confirming its test document was saved.

Single-precision render meshes still limit tiny detail at very large absolute
world offsets. The floor intentionally fades near the horizon and is invisible
when the viewing ray does not intersect XY ahead of the camera. Major/minor
spacing adapts visually and does not change snapping. Release builds, other GPU
drivers and large production-assembly performance remain unverified.

Implementation commits: `29d18b0` (grid/clipping/navigation) and `57a4f2b`
(GPU capture validation and edge clipping). Both were pushed to the feature
branch before final delivery. This order ends here; no further modeling or
viewport expansion is authorized.
