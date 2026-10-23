# Virtual Lathe — sketch-driven turning

Draw a closed cross-section in an XY, XZ or YZ sketch, then **Finish Sketch**.
Choose **Modify > Virtual Lathe**, select the source **Sketch** and **Closed
contour**, select **X-axis**, **Y-axis**, **Z-axis** or a **Sketch line**, and set
an angle greater than 0 through 360 degrees. The default is 360 degrees. Use
**Preview**, then **Turn part**. Reverse rotation changes the right-hand direction.
No outside/bore station table is required or shown.

Select the sketch in the model tree or click its outline in the 3D viewport.
Clicking an entity beneath a sketch resolves its closed contour. When several
contours are available, choose the intended one explicitly. Only that contour's
stable entity identifiers are used; unrelated lines, other contours and axis
geometry remain in the source sketch. Circle and connected line/Bezier contours
use the existing sketch representation. Intersecting or ambiguous geometry must
be corrected in Sketch before turning.

World axes pass through the document origin. They are independent of sketch
plane labels: an XZ rim around Z uses **Z-axis**. A referenced straight sketch
line defines an infinite axis in document coordinates and follows subsequent
line edits. It must be nonzero and lie in the profile plane. Any existing line
can act as a construction axis, including a line in another coplanar sketch.
The selected axis is displayed in the viewport before committing. An active
preview updates when its axis, contour, angle or direction changes. Cancel
restores the document view without altering geometry, feature history or Undo.

The result is a **Virtual Lathe** history feature backed by the existing native
OCCT Revolve operation. Full and partial turns must produce one valid solid;
partial turns include end faces. Offset profiles produce hollow rotational parts.
Open, crossing, degenerate, axis-crossing and invalid results are rejected with
an explanation and the document transaction rolls back.

Select a turned part and choose **Lathe profile...** to edit its source contour,
axis and angle. **Edit source sketch** opens the original sketch, which remains
authoritative and available in the tree. Sketch and axis-line changes regenerate
the feature through the existing dependency cache and OCAF transaction system.
One committed feature edit is one Undo step. Native `.djo` and engineering `.djc`
files retain contour/axis references, angle, direction and the Lathe history marker.

## API, FRust and CAD nodes

`cad_lathe` uses the same command as the GUI:

```json
{"action":"profiles","sketch":"sketch-1"}
{"action":"preview","parameters":{"sketch":"sketch-1","entities":["line-2","line-3","line-4","line-5"],"axis":{"type":"worldZ"},"angleDegrees":360,"reverse":false}}
{"action":"create","parameters":{"sketch":"sketch-1","entities":["line-2","line-3","line-4","line-5"],"axis":{"type":"worldZ"},"angleDegrees":360,"reverse":false}}
{"action":"get","id":"revolve-6"}
{"action":"update","id":"revolve-6","parameters":{"angleDegrees":180}}
{"action":"update","id":"revolve-6","parameters":{"axis":{"type":"sketchLine","sketch":"sketch-7","entity":"line-8"}}}
{"action":"delete","id":"revolve-6"}
```

`profiles` returns labeled contour choices with their `entities` arrays. Creation
may omit `entities` only when exactly one closed contour is discovered. Explicit
entity references remain stable when the sketch is edited; deleting required
geometry is refused if its dependent Lathe can no longer rebuild. Choose the new
contour after changing topology or selecting a different source sketch.

Create/get/update return `ok`, stable `id`, `type:"revolve"`, `workflow:"lathe"`,
source `sketch` and complete `parameters`. Updates merge supplied parameters.
Preview returns `preview:true`, `committed:false` and `volumeMm3`; a draft creation
does not return a committed feature identifier. Failures return `ok:false`, `code`
and `error`. Preview runs an isolated copy through the same geometry command.
Legacy `outer`/`inner` station API requests and saved Lathe features remain
supported for compatibility; they are not the new GUI workflow.

FRust: `djehuti_cad_lathe(args_json: String) -> String`. The **Virtual Lathe** CAD
node accepts the identical structured request and result. `cad_ui` command
`lathe` opens the tool. Fields: `latheProfile`, `latheContour`, `latheAxis`,
`latheAxisLine` (visible for a line axis), `latheAngle` and `latheReverse`.
Buttons: `lathePreview`, `latheClearPreview`, `apply`, `cancel`. Sketch/line choices
use the display labels returned in `state.fields[].options`.

## DUB wheel rim acceptance fixture

An XZ contour with seat radius 50 mm over Z=-13..13, flange radius 55 mm over
Z=-16..-13 and 13..16, and inner radius 47 mm over Z=-16..16, revolved about
world Z, produces one annular rim solid. Expected cylindrical surfaces are:

- Tire-seat diameter **100 mm**, width **26 mm**.
- Outer flange diameter **110 mm**, overall width **32 mm**.
- Inner diameter **94 mm**, with a continuous inner cylindrical wall.

Its analytic volume is **12462*pi mm³**. The focused `--cad-lathe-check`
diagnostic verifies those surfaces, full/partial/reverse geometry, selected
contours, preview isolation, GUI selection/control callbacks, invalid geometry,
axis/source dependencies, Undo/Redo, legacy compatibility and native/JSON reload.
It writes `build/cad-lathe-check.result.json` and rim fixtures under
`build/lathe-check/`. Programmatic callbacks and viewport picking are distinguished
from pointer-driven interaction in the completion report.
