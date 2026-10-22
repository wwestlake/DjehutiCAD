# DjehutiCAD local automation API

An authorized local agent can operate the running DjehutiCAD application step by step, for example during a
recorded, narrated demonstration. The design follows the Djehuti Electronics Workbench (DEWB) local agent API:
loopback HTTP on a dynamic port, a discovery file, a bearer token, and direct calls to the application's own
agent tools. Every tool goes through `DjehutiCAD::runToolCommand`, which the BYOK agent also uses. Modeling goes
through `CadCommandService`; the visible task panels go through `CadWorkspacePanel::automation`. The HTTP server
holds no CAD logic.

## Discovery and authentication

- On start, DjehutiCAD binds `127.0.0.1` on a free port and writes
  `%APPDATA%\DjehutiCAD\agent-api.json`:
  `{ schema: "djehuti-cad-agent-api", version: 1, baseUrl, token, transport, executable, instance }`.
- Every request needs `Authorization: Bearer <token>`. Requests that carry an `Origin` header (browsers) are
  refused; there is no CORS.
- Keep the token private: never print it, log it or show it in a recording. `tools/cad_api/client.py` reads it
  and never prints it.
- The file is removed on exit, and only if it still holds this instance's token. The API is not started in
  `--diagnostic` runs.
- Limits: requests up to 1 MiB, no chunked transfer, and at most 16 queued or running requests.

## Endpoints

| Method and path | Result |
|---|---|
| `GET /v1/status` | `{status: "ready", service, version}` |
| `GET /v1/tools` | The live tool catalogue: names, descriptions and JSON parameter schemas (the agent's tools). |
| `GET /v1/state` | Shortcut for `cad_ui` `{action: "state"}`. |
| `POST /v1/tools/call` | `{name, arguments, requestId?}`. Runs one tool. |
| `GET /v1/requests/{id}` | Status and, once finished, the result of an earlier call. |
| `POST /v1/requests/{id}/cancel` | Cancels a call that has not started. |

## Completion semantics

Calls run one at a time, in submission order. A single worker hands each call to the JUCE message thread and
waits for it to finish. Every reply carries `requestId`, `tool`, `status` and `durationMs`, plus `result`
once finished.

| status | HTTP | Meaning |
|---|---|---|
| `queued` / `running` | 202 | Accepted, not finished. Poll `GET /v1/requests/{id}`. |
| `completed` | 200 | Finished; `result.ok` is true. |
| `failed` | 409 | Finished; `result.ok` is false and `result.error` says why. |
| `cancelled` | 200 | Cancelled before it started; nothing changed. |

- Most steps finish within the 1.5 s the server waits before answering, so they return 200 or 409 directly.
- Results report the actual outcome; nothing is reported as done merely because it was scheduled. The BYOK
  agent path no longer answers "Operation scheduled".
- `cad_ui` results and direct modeling results include `state`: the document revision and the actual UI and
  document state after the step.
- A started step cannot be interrupted (cancel answers 409). Most steps are atomic message-thread operations.
  Export plans on the message thread and writes on the worker.
- Reusing a `requestId` with the same arguments returns the original outcome without running the command
  again. Different arguments under the same id are refused (409). After a transport error, read
  `/v1/requests/{id}` instead of resubmitting.

## Tools

All agent tools are available:
- Chamfer: `cad_chamfer` supports edge inspection, CRUD and read-only preview;
  see [Chamfer](CHAMFER.md) for selection, distance and preview semantics.
- Virtual Lathe: `cad_lathe` supports `create`, `get`, `update`, `delete`; see
  [Virtual Lathe](VIRTUAL_LATHE.md) for outside/bore station profiles and GUI fields.
- Revolve: `cad_revolve` supports `create`, `get`, `update` and `delete` using stable
  feature IDs and structured parameters. See [Parametric Revolve](PARAMETRIC_REVOLVE.md)
  for axis choices, degree/mm conventions, examples and error responses.
- modeling: `cad_create_box`, `cad_create_sketch`, `cad_sketch_*`, `cad_extrude`, `cad_cut`, `cad_boolean`,
  `cad_set_dimensions`, `cad_set_placement`, `cad_undo`, `cad_redo`;
- assembly and motion: `cad_create_instance`, `cad_create_joint`, couplings, roles, `cad_motion_*`;
- IK: `cad_ik_*`;
- faces and export: `cad_list_faces`, `cad_create_face_sketch`, `cad_export`;
- inspection: `cad_check_interference` (`pose`: `shown`/`reference`, `clear`), which is the same operation as
  Assembly > Check interference;
- workspace: `cad_workspace`, `open_*`.

The `cad_ui` tool operates the visible workspace like a person does, so a recording shows the same task panels.
`command: "revolve"` opens the Revolve task. Its fields are `revolveProfile` (profile
choice), `axisType` (Sketch U/V, World X/Y/Z, Custom), `angleDegrees`,
`revolveReverse`, and custom-only `axisX/Y/Z` and `axisDX/DY/DZ`. Select a Revolve body
to edit those fields in its inspector, then click `apply`. Feature state includes
the complete Revolve `parameters` object. Combo choices accept their displayed text
or zero-based index.

| action | Arguments | Effect |
|---|---|---|
| `state` | – | Task title and instruction, message, selection, the editing sketch, task `fields` (id, label, type, value, enabled, options), `canApply`/`canCancel`, `commands` (enabled and active), model camera, render status, sketch `gesture`, and every feature with its sketch entities. |
| `command` | `command` | A toolbar or menu command: `sketch`, `box`, `cylinder`, `extrude`, `cut`, `subtract`, `union`, `intersect`, `line`, `rectangle`, `circle`, `sketchSelect`, `constrain`, `fitSketch`, `finishSketch`, `instance`, `joint`, `coupling`, `link`, `ik`, `fit`, `reset`, `views`, `pointer`, `boxSelect`, `circleSelect`, `bodyMode`, `faceMode`, `edgeMode`, `undo`, `redo`. |
| `set` | `field`, `value` | Sets a visible, enabled task field: numbers must be finite; choices must be listed options. |
| `click` | `button` | A task-panel button by id, or `apply` / `cancel`. |
| `select` | `id`, or `entity` [`second`] | Selects a tree item (body, sketch, instance, joint, coupling, IK task), or an entity of the open sketch. |
| `sketch` | `phase`, `x`, `y` (sketch mm) | `begin` starts a gesture with the active Line, Rectangle or Circle tool. `update` moves the preview, which stays on screen without changing the document. `commit` adds the geometry. `cancel` drops it. |
| `camera` | `operation`: `set`/`fit`/`reset`/`state`; `yawDeg`, `pitchDeg`, `distance`, `panX`, `panY`; `view` (-1 = model view, 0.. = 3D view) | Moves only that view's camera, within the same bounds as mouse navigation. The document is not changed. |
| `document` | `operation`: `new`/`open`/`save`; `path`; `discard`; `overwrite` | Same code as the File menu. New and open refuse a modified document unless `discard` is true; save refuses an existing file unless `overwrite` is true. |

## Paced demonstrations

The API never advances on its own. Each call performs one visible step and returns; what it shows (preview,
selection, task panel, camera) stays until the next call. `tools/cad_api/client.py --sequence steps.json`
waits for Enter before every step. Pauses are chosen by the person narrating.

## Client and focused check

```bash
python tools/cad_api/client.py cad_ui --args "{\"action\": \"state\"}"
```

`tools/cad_api/smoke.py` runs in a new, empty document of the running application. It:
- authenticates and rejects a bad token;
- holds a rectangle preview, then commits it;
- extrudes, cuts through all, moves the camera and renames;
- checks request-id replay, a failed modeling call and invalid fields;
- saves, reopens and leaves an empty document.

## Limits

- One controlling client at a time is the intended use. Calls are serialised, and a slow request delays the
  next.
- The 3D view can't be picked by screen coordinates through the API; select by id from `state`.
- Face/edge picking is available via `cad_list_faces` and `cad_create_face_sketch`, not via `cad_ui select`.
- Floating-window layout and dialogs opened by file choosers or alerts aren't driven; use `document` and the
  tools instead.
- Steps that are already running cannot be cancelled.
