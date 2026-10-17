# DEWB Implementation Handoff — Motion Link v1

The DCAD side is implemented and checked against a protocol peer. This page lists what DEWB must
implement. The normative reference is [DEWB_DCAD_MOTION_INTERFACE_V1.md](DEWB_DCAD_MOTION_INTERFACE_V1.md);
[`tools/motion_peer/dewb_motion_peer.py`](../tools/motion_peer/dewb_motion_peer.py) is a working client you
can read line by line. The peer's lifecycle answers only demonstrate the message contract — the real
simulator behaviour is DEWB's to implement.

## Endpoint and transport

* DCAD is the named-pipe server (Motion Link panel → **Start Listening**). Discover endpoints in
  `%LOCALAPPDATA%\DjehutiCAD\MotionEndpoints\*.json` (`pipePath`, `modelId`, `documentName`, `processId`)
  or let the user paste the pipe path. Entries are hints; skip pipes that fail to open.
* Open `\\.\pipe\DjehutiCAD.Motion.<pid>.<token>` for read and write (JUCE `InterprocessConnection::connectToPipe`
  or any Win32 client). Framing: `u32 0xF2B49E2C` + `u32 payloadLength` (little-endian) + UTF-8 JSON.
* Do pipe I/O on a link thread. Give the solver a single "newest state" slot; never block it on the pipe.
  Use a bounded outgoing queue and a write deadline; treat a failed or stalled write as a lost connection.
* Send something at least every second (`LinkStatus` `code: "heartbeat"` with `state` = your lifecycle).
  DCAD declares the link lost after 3 s of silence. DCAD closes the pipe itself when a write to you fails,
  stalls for 3 s or backs up 64 messages — you will see your pipe handle fail; reconnect with a new session.
  Transport loss must not stop or reset your simulation.

## Connection sequence

1. `Hello` (`protocol: "djehuti.motion"`, `protocolVersion: 1`, `role: "dewb"`, `capabilities`). Include
   `lifecycleControl.v1` only once the lifecycle commands below are implemented. DCAD answers with
   `Hello` (its capabilities include `lifecycleControl.v1`) and `MechanicalInterface`.
2. `SignalInterface` with a stable `simulationId` and every signal that can drive motion.
3. DCAD sends `BindingProposal` (or `LinkStatus` `profileIncompatible` with `errors[]`).
4. Validate each mapping (signal exists; `quantity`, `unit`, `unitToSI` match) and answer `BindingAccept`
   (echo `modelId`, `interfaceRevision`, `profileRevision`, `simulationId` exactly) or `BindingReject`.
5. `SynchronizeState` (complete state), wait for `StateApplied`, then stream `StateFrame`s.

## Signal catalog

* Stable, unique ids; DCAD never matches by name or guesses. Do not carry over `fallbackSignal` /
  name-guessing behaviour from `MechanicalSceneModel`.
* Quantities and units: `angle` (`rad`, `deg`, `rev`), `angular_velocity` (`rad/s`, `deg/s`, `rpm`,
  `rev/s`), `position` (`m`, `mm`), `linear_velocity` (`m/s`, `mm/s`); `kind` `"state"` for angle/position,
  `"rate"` for velocities.
* Keep the same `simulationId` for the same simulation session (also after a reset). A different
  `simulationId` tells DCAD the session was replaced.

## Absolute state publication and ordering

* Every state lists **every** joint of the accepted profile: `{joint, coordinate}` in absolute rad or m,
  optional `velocity`. State sources: `offset + scale × unitToSI × value`. Rate sources: integrate
  `scale × unitToSI × value` from `initialCoordinate` in simulation time, restarting on every new timeline.
  Never wrap angles; never clamp to limits.
* Each state: `simulationId`, `timelineGeneration`, `profileRevision`, `sequence` (decimal string),
  `simTimePs` (sim_core 1 ps ticks, decimal string), `microstep`, `lifecycle`.
* Ordering (spec §6.2): never send an older timeline; a newer timeline starts with `SynchronizeState`;
  within a timeline every state, including a resynchronization, needs a larger `sequence` and must not
  move `(simTimePs, microstep)` backwards. Rewinding time always means a new timeline.
* After DCAD's `revalidationRequired` + new `BindingProposal`: stop streaming, accept, then resynchronize
  (same timeline with the next `sequence`, or a new timeline). Do not resend an old `SynchronizeState`;
  a repeated one is never reapplied (DCAD re-acknowledges it with `duplicate: true` if it was published).

## Lifecycle commands (`lifecycleControl.v1`)

DCAD sends `LifecycleCommand {requestId, modelId, simulationId, profileRevision, timelineGeneration,
command, stepDurationPs}`; answer with `LifecycleResult {requestId, simulationId, profileRevision,
command, outcome, lifecycle, timelineGeneration, simTimePs, stateSequence?, code?, message?}`.

| command | permitted from | DEWB does | publish |
|---|---|---|---|
| `start` | `stopped` | run; if not at the initial state, reinitialize time first | new timeline `SynchronizeState` (if reinitialized) → wait `StateApplied` → `StateFrame running` → result |
| `pause` | `running` | suspend, keep state and time | `StateFrame paused` → result |
| `resume` | `paused` | continue the same timeline | `StateFrame running` → result |
| `stop` | `running`, `paused` | end the run, keep the final state | `StateFrame stopped` → result |
| `reset` | any | restore the initial state at t = 0, lifecycle `stopped` | new timeline `SynchronizeState` → wait `StateApplied` → result |
| `step` | `paused`, or `stopped` at the initial state | advance exactly `stepDurationPs`, end `paused` | `StateFrame paused` → result |

* **Pause vs Stop vs Reset**: Pause keeps the run resumable; Stop ends the run but keeps its final state on
  screen (Start then begins a fresh run on a new timeline); Reset returns to the initial state on a new
  timeline without running. None of them may wait for the next solver step to be published.
* `outcome`: `applied` once performed; `rejected` with `code` (`invalidTransition`, `invalidStep`,
  `staleBinding`, `staleTimeline`, `busy`, `unknownCommand`, `faulted`) and an actionable `message`;
  optionally `accepted` first for long operations (then `applied`/`rejected` within 30 s).
* Reject requests whose `modelId`/`simulationId`/`profileRevision` is not the current binding
  (`staleBinding`) or whose `timelineGeneration` is not current (`staleTimeline`).
* **Deduplicate by `requestId`**: keep results of recent requests for the session; a repeated
  `requestId` returns the stored result and does not execute again. DCAD's Retry reuses the id, so a
  repeated Single Step must advance time only once.
* Answer within 5 s (or send `accepted`), otherwise DCAD shows the request as unconfirmed.
* A fault (`faulted`) is published as a state immediately; `reset` must be permitted from it.

## Known issue in the DEWB reference code

`MechanicalSceneModel.h` `Mat4::transformPoint` computes `y` with `m[9] * p.y` where `m[9] * p.z` is intended.
DCAD does its pose math independently; fix this before reusing that helper.
