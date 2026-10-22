# Virtual Lathe

Choose **Modify > Virtual Lathe**. Enter the outside and bore profiles as one
`axial position, diameter` pair per line, in millimetres. Positions increase along
the part. Repeat a position with another diameter to create a shoulder or groove.
Both profiles must start and end at the same positions, with positive wall
thickness everywhere. A zero bore diameter produces solid material to the axis.

For example, an outside profile with a groove and an 8 mm bore:

```text
Outside:       Bore:
0, 20          0, 8
10, 20         30, 8
10, 16
12, 16
12, 20
30, 20
```

Apply **Turn part**. New profiles use an XZ sketch with radial X and axial Z;
diameters become sketch radii. Turning uses the existing Parametric Revolve
evaluator. Full/partial angles and reverse rotation remain available. Select the
turned body and choose **Lathe profile** to edit its stations, or **Edit source
sketch** to use the normal 2D editor. Normal Revolve axis/profile controls remain
available in the body inspector.

The source sketch is authoritative. Inspection derives the station table from its
current geometry. After custom curve, topology or constraint edits that cannot be
represented by stations, the tool preserves them and directs further profile edits
to the sketch editor. It does not silently regenerate or discard custom work.

## Agent / automation API

`cad_lathe` supports create/get/update/delete. `create` requires `parameters.outer`;
an omitted bore defaults to zero diameter at both ends. Each station is
`[axial_mm, diameter_mm]`. Updates merge supplied parameters.

```json
{"action":"create","parameters":{"outer":[[0,20],[10,20],[10,16],[12,16],[12,20],[30,20]],"inner":[[0,8],[30,8]]}}
{"action":"get","id":"revolve-2"}
{"action":"update","id":"revolve-2","parameters":{"angleDegrees":180}}
{"action":"update","id":"revolve-2","parameters":{"outer":[[0,24],[10,24],[10,16],[12,16],[12,24],[30,24]]}}
{"action":"delete","id":"revolve-2"}
```

Success returns `ok`, stable `id`, `type: "revolve"`, `workflow: "lathe"`, `sketch`,
`profileEditable` and current `parameters`. Failures return `ok:false`, `code` and
`error`. Unsupported station replacement returns `custom_profile`; sketch editing
and normal `cad_revolve` parameter edits remain available. Station tables contain
2–256 rows per boundary. Outside diameters must be positive; bores must be smaller.

FRust: `djehuti_cad_lathe(args_json: String) -> String`. The **Virtual Lathe** CAD
node uses the identical arguments/result JSON. GUI creation and editing call this
same command service. `cad_ui` command `lathe` opens the tool; fields are
`latheOuter`, `latheInner`, `latheAngle` and `latheReverse`.

Sketch creation/replacement and turning commit together in one OCAF transaction.
Undo/Redo, dependent rebuilds, native `.djo` and engineering `.djc` interchange are
preserved. JSON version 10 adds the optional `lathe` workflow marker; the geometry
and parameters remain an ordinary Revolve feature. Invalid geometry rolls back.

The focused `--cad-lathe-check` diagnostic writes
`build/cad-lathe-check.result.json` and covers geometry, stations, GUI controls,
API operations, Undo/Redo and native/JSON persistence.

Verified October 22, 2026: Debug application build passed; Lathe diagnostic passed
13/13 checks, including GUI creation and station editing. The existing Revolve
diagnostic passed 38/38. GUI control callbacks were exercised; native pointer and
visual inspection were not performed in this phase.

The completed batch also passed 18 live HTTP/GUI-control checks. A grooved hollow
Lathe -> circular-edge Chamfer -> positioned Hole chain rebuilt correctly after
outside-profile edits, matched analytic STEP volumes, and survived native
save/reopen with all three tools editable. See the Chamfer and Hole guides for
their selection workflows and limitations.
