# DEWB Implementation Handoff — Motion Link v1

The DCAD side is complete and checked against a protocol peer. This page lists what DEWB must
implement. The normative reference is [DEWB_DCAD_MOTION_INTERFACE_V1.md](DEWB_DCAD_MOTION_INTERFACE_V1.md);
[`tools/motion_peer/dewb_motion_peer.py`](../tools/motion_peer/dewb_motion_peer.py) is a working client
that you can read line by line.

## Endpoint

* DCAD is the named-pipe server. The user presses **Start Listening** in DCAD's Motion Link panel.
* Discover endpoints in `%LOCALAPPDATA%\DjehutiCAD\MotionEndpoints\*.json` (`pipePath`, `modelId`,
  `documentName`, `processId`), or let the user paste the pipe path (Copy pipe path button).
  Treat entries as hints: a crashed DCAD can leave a stale file, so skip any pipe that fails to open.
* Open `\\.\pipe\DjehutiCAD.Motion.<pid>.<token>` for read and write. JUCE `InterprocessConnection::connectToPipe`
  works, as does any Win32 client.
* Framing: `u32 0xF2B49E2C` + `u32 payloadLength` (both little-endian), then one UTF-8 JSON object.
  If you use JUCE, construct `InterprocessConnection` with the default magic number.
* Do pipe I/O on a link thread. Never block the solver thread: hand the latest complete state to the link
  thread through a single "newest state" slot, as DCAD does on its side.

## Connection sequence

1. Send `Hello` (`protocol: "djehuti.motion"`, `protocolVersion: 1`, `role: "dewb"`). DCAD replies with `Hello` and `MechanicalInterface`.
2. Send `SignalInterface`: a stable `simulationId` and every signal that can drive motion, as
   `{id, name, quantity, unit, kind}`.
3. DCAD validates its saved profile against your signals and sends `BindingProposal`. If it can't,
   it sends `LinkStatus` `profileIncompatible` with per-mapping `errors[]`.
4. Validate each mapping on your side: the signal exists, and `quantity`, `unit` and `unitToSI` match. Then answer:
   * `BindingAccept`, echoing `modelId`, `interfaceRevision`, `profileRevision` and `simulationId` exactly, or
   * `BindingReject` with `errors[]`.
5. Send `SynchronizeState` (a complete state, `timelineGeneration` ≥ 1), then wait for `StateApplied`.
6. Stream `StateFrame`s. Send something at least every second (a `LinkStatus` with `code: "heartbeat"` when idle).
   DCAD declares the link lost after 3 s of silence.

## Signal catalog requirements

* Signal ids must be stable and unique. DCAD never matches signals by name, and nothing is guessed.
  Don't carry over the `fallbackSignal` / name-guessing behaviour from `MechanicalSceneModel`.
* Allowed quantities and units:
  * `angle`: `rad`, `deg`, `rev`
  * `angular_velocity`: `rad/s`, `deg/s`, `rpm`, `rev/s`
  * `position`: `m`, `mm`
  * `linear_velocity`: `m/s`, `mm/s`
* `kind` is `"state"` for `angle` and `position`, and `"rate"` for the two velocity quantities.

## Publishing absolute joint state

* Every state lists **every** joint in the accepted profile as `{joint, coordinate}`. Revolute coordinates are
  absolute radians and prismatic coordinates are absolute metres. `velocity` (rad/s or m/s) is optional.
* State sources: `coordinate = offset + scale × unitToSI × value`.
* Rate sources: integrate `scale × unitToSI × value` from `initialCoordinate` in **simulation time**, and
  restart from `initialCoordinate` on every new timeline. DCAD never integrates.
* Don't wrap angles: 4π must stay 4π. Don't clamp to limits; DCAD reports limit violations as warnings.
* Every state carries:
  * `simulationId`, `timelineGeneration` and `profileRevision`
  * `sequence`: a decimal string that increases strictly within a timeline
  * `simTimePs`: sim_core ticks (1 ps) as a decimal string
  * `microstep` and `lifecycle` (`running` / `paused` / `stopped` / `faulted`)
* `(simTimePs, microstep)` must not go backwards within a timeline.

## Pause, step, speed, reset, reconnect

* **Pause, single step and speed changes:** keep sending complete states with the current `lifecycle`.
  Time is simulation time only. DCAD does not interpolate.
* **Reset or rewind:** increment `timelineGeneration`, send `SynchronizeState`, and wait for `StateApplied`.
  DCAD rejects frames from the old timeline.
* **DCAD revalidation:** if DCAD sends `LinkStatus` `revalidationRequired` followed by a new `BindingProposal`,
  stop streaming and answer the proposal, then synchronize again.
* **Signal changes on your side:** if your signals change, send a new `SignalInterface`; it triggers a new proposal.
* **Disconnect:** send `LinkStatus` `code: "goodbye"` before closing. After any disconnect, a reconnect is a
  fresh session: `Hello`, interfaces, binding and synchronization all over again.

## Known issue in the DEWB reference code

`MechanicalSceneModel.h` `Mat4::transformPoint` computes `y` with `m[9] * p.y` where `m[9] * p.z` is intended.
DCAD does its pose math independently; fix this before reusing that helper.
