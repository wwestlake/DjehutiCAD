# DEWB–DCAD Motion Interface, Version 1

This document is the implementation reference for both sides of the link between the
Djehuti Electronics Workbench system simulator (**DEWB**) and DjehutiCAD (**DCAD**).
Machine-readable schemas are in [`motion_interface_v1/schemas`](motion_interface_v1/schemas),
example messages in [`motion_interface_v1/fixtures`](motion_interface_v1/fixtures), and an
executable protocol peer in [`../tools/motion_peer/dewb_motion_peer.py`](../tools/motion_peer/dewb_motion_peer.py).
Version 1 is unreleased; the lifecycle-control extension (§8) is part of it and is negotiated by capability.

## 1. Responsibilities

| Owner | Owns |
|---|---|
| DCAD document (OCAF) | Geometry, assembly instances, joints, reference placements, the connection profile (signal mappings) |
| DEWB | Numerical simulation, physical behaviour, simulation time, run state, authoritative joint coordinates |
| DCAD runtime scene | Temporary poses computed from received coordinates (never written to the document) |
| Motion link | Interface exchange, profile validation, synchronization, state delivery, lifecycle requests |

DEWB decides what the machine does and whether it runs; DCAD decides how that state places the modelled
assembly and may *request* run-state changes. DCAD never integrates velocities, never invents
coordinates, and never moves the model in response to a request — only in response to state.

## 2. Transport

* **Endpoint**: DCAD is the pipe server. Each DCAD "Start Listening" creates a Windows named pipe
  `\\.\pipe\DjehutiCAD.Motion.<pid>.<token>` (`<token>` is 12 random hex digits, so concurrent
  DCAD processes and documents never collide). DEWB is the client and opens the pipe for read and write.
* **Discovery**: while listening, DCAD writes
  `%LOCALAPPDATA%\DjehutiCAD\MotionEndpoints\<pipeName>.json`
  (`protocol`, `protocolVersion`, `pipeName`, `pipePath`, `processId`, `modelId`, `documentName`, `startedUtc`)
  and deletes it when listening stops. Entries are hints: a crashed DCAD can leave a stale file, so skip a
  pipe that cannot be opened. The pipe path is also shown (and copyable) in the Motion Link panel.
* **Framing** (JUCE `InterprocessConnection`): every message is
  `uint32 magic` + `uint32 payloadLength` + `payload`. Both words are **little-endian**;
  `magic = 0xF2B49E2C` (bytes `2C 9E B4 F2`). `payloadLength` counts payload bytes only.
* **Payload**: one UTF-8 JSON object, no BOM, no trailing NUL. Every object has a string `type`.
* **Limits**: payloads up to 1 MiB. Send at most one `StateFrame` per state you want shown; DCAD keeps
  only the newest unpublished state and never builds a backlog.
* **Liveness**: both sides send at least one message every 1000 ms while connected (a `LinkStatus`
  with `code: "heartbeat"` when nothing else is due, including while paused or stopped). Heartbeats never
  advance simulation time or move the model. DCAD treats 3000 ms of silence as a disconnect.
* **Write failures and stalled peers** (DCAD side): DCAD's reads wait without a deadline, so an idle
  connection stays healthy. Writes run on a dedicated thread. DCAD closes the connection when
  (a) a write fails, (b) a single write has not completed after 3000 ms (the peer stopped reading), or
  (c) 64 outgoing messages are waiting. Closing invalidates the session, discards its queued messages and
  any unconfirmed lifecycle request, holds the last valid pose, shows `Disconnected` with the reason, and
  immediately serves a fresh pipe instance. DEWB sees its pipe handle fail and must reconnect with a full
  session (§6). Stop Listening, Disconnect and application exit cancel pending pipe I/O and join the link
  threads with a bounded wait (4 s); none of them stop the DEWB simulation.
* **DEWB side**: keep pipe I/O on a link thread with the same rules: a bounded outgoing queue, a write
  deadline, and a single latest-state slot so the solver thread never blocks on the pipe.
* **Reconnection**: every `Hello` from DEWB starts a new connection session. Nothing (binding, timeline
  watermarks, synchronization, unconfirmed requests) carries over; DEWB must repeat the full sequence.
* **Scope**: the pipe uses default Windows pipe security (owner/administrators have write access). JUCE
  does not expose `PIPE_REJECT_REMOTE_CLIENTS`; the random token and the user-local discovery file keep the
  endpoint private in practice. See §10.

## 3. Units and conventions

* Interface lengths are **metres**, angles **radians**, right-handed coordinates. (DCAD geometry is in
  millimetres internally; DCAD converts at the interface.)
* Joint coordinates: revolute = angle in rad about the joint frame's **+Z** axis (right-hand rule);
  prismatic = displacement in m along **+Z**. Fixed joints have no coordinate.
* Rotations are unit **Hamilton quaternions** `{ "w", "x", "y", "z" }` (scalar first), *active*: a frame
  `{ translation t, rotation q }` maps a point expressed in the frame to the enclosing frame:
  `p_outer = R(q) · p_inner + t`.
* Pose evaluation, always from reference placements and current coordinates (never accumulated):

  ```
  world(child) = world(parent) × parentToJoint × motion(q − reference) × jointToChildAtReference
  ```

  `parentToJoint` maps joint-frame coordinates into the parent instance frame (into world when
  `parent` is `"world"`). `jointToChildAtReference` maps child-instance-frame coordinates into the joint
  frame at the reference coordinate. `motion(d)` is a rotation by `d` about +Z (revolute) or a translation
  of `d` along +Z (prismatic). Nested chains compose the same way, so children follow moving parents.
* Coordinates are unbounded: 4π rad is two full turns and is distinct from 0.
* Time: `simTimePs` is sim_core simulation time, a signed 64-bit count of **1 ps ticks**
  (`kTicksPerSecond = 10^12`), sent as a **decimal string** (`"2500000000000"` = 2.5 s). `microstep` is
  the sim_core superdense-time microstep (integer ≥ 0, default 0). `sequence` and `stepDurationPs` are
  decimal strings (< 2^63). Revisions and `timelineGeneration` are JSON integers (< 2^31).

### Quantities and units

| quantity | kind | allowed units (factor to SI) | drives |
|---|---|---|---|
| `angle` | state | `rad` (1), `deg` (π/180), `rev` (2π) | revolute |
| `angular_velocity` | rate | `rad/s` (1), `deg/s` (π/180), `rpm` (2π/60), `rev/s` (2π) | revolute |
| `position` | state | `m` (1), `mm` (0.001) | prismatic |
| `linear_velocity` | rate | `m/s` (1), `mm/s` (0.001) | prismatic |

## 4. The three interface sections

### 4.1 DCAD mechanical interface — `MechanicalInterface` (DCAD → DEWB)

| field | type | meaning |
|---|---|---|
| `modelId` | string | Stable UUID of the CAD model; changes only for a new document |
| `documentName` | string | Display only |
| `interfaceRevision` | int | Advances whenever instances, joints, frames, references or limits change (not on renames) |
| `profileRevision` | int | Advances whenever the interface or any mapping changes |
| `lengthUnit`, `angleUnit`, `rotationConvention`, `motionAxis` | string | `"m"`, `"rad"`, `"hamilton-wxyz-active"`, `"+Z"` |
| `instances[]` | `{id, name, featureId}` | Assembly instances (identity independent of modelling history) |
| `joints[]` | object | See below |

Joint: `id`, `name`, `type` (`fixed`/`revolute`/`prismatic`), `parent` (instance id or `"world"`),
`child`, `parentToJoint` and `jointToChildAtReference` (`{translation:[x,y,z] m, rotation:{w,x,y,z}}`),
`coordinate` (`null` for fixed, else `{quantity, unit, reference, limits:{lower,upper}|null}`) and
`acceptedSourceQuantities`. Ids are stable through save/load, JSON interchange and renames.

Revision numbers identify *content*, not a binding instance: undo/redo can restore an earlier number.
DCAD therefore also invalidates bindings on its side whenever the document is replaced (New/Open) and
never lets work queued under an earlier binding publish, even if the numbers match again (§7).

### 4.2 DEWB signal interface — `SignalInterface` (DEWB → DCAD)

`simulationId` (string, identifies the simulation session), optional `simulationName`,
`signalInterfaceRevision` (string or int, echoed back), and `signals[]`:
`{id, name, quantity, unit, kind}` where `kind` is `"state"` for `angle`/`position` and `"rate"` for
velocities. Ids must be unique and stable. DCAD never matches signals by name.

A `SignalInterface` always invalidates the current binding and triggers a new proposal. Keep the same
`simulationId` for the same simulation session (for example after a reset); a different `simulationId`
means the simulation session was replaced and clears DCAD's ordering history (§6.2).

### 4.3 Agreed connection profile — `BindingProposal` (DCAD → DEWB)

DCAD owns the profile editor. Each mapping:

| field | meaning |
|---|---|
| `joint`, `signal` | Joint id and DEWB signal id |
| `sourceQuantity`, `sourceUnit` | Must equal the advertised signal's `quantity` and `unit` |
| `jointQuantity`, `jointUnit` | `angle`/`rad` or `position`/`m` |
| `unitToSI` | Factor converting `sourceUnit` to SI (table above) |
| `scale` | Gear ratio applied after unit conversion |
| `offset` | Added for state sources; `0` for rate sources |
| `initialCoordinate` | Joint coordinate at simulation time 0 for rate sources; `null` for state sources |

DEWB computes every joint coordinate it publishes:

* state source: `coordinate = offset + scale × unitToSI × value`
* rate source: `coordinate(t) = initialCoordinate + ∫₀ᵗ scale × unitToSI × rate dt`, integrated by DEWB
  in **simulation time**, restarting from `initialCoordinate` on every new timeline.

Validation (both sides): ≥ 1 mapping; every joint exists and is not fixed; the source quantity drives that
joint type; the signal exists; quantity, unit and kind match; numbers are finite and `scale ≠ 0`. DCAD
refuses to propose an incompatible profile (`profileIncompatible` with `errors[]`); DEWB answers a proposal
with `BindingAccept` or `BindingReject`.

## 5. Messages

| type | direction | purpose |
|---|---|---|
| `Hello` | both | protocol `"djehuti.motion"`, `protocolVersion: 1`, `role` (`"dewb"`/`"dcad"`), `application`, `applicationVersion`, `capabilities[]` |
| `MechanicalInterface` | DCAD→DEWB | §4.1 |
| `SignalInterface` | DEWB→DCAD | §4.2 |
| `BindingProposal` | DCAD→DEWB | §4.3 plus `modelId`, `interfaceRevision`, `profileRevision`, `simulationId`, `signalInterfaceRevision` |
| `BindingAccept` | DEWB→DCAD | echoes `modelId`, `interfaceRevision`, `profileRevision`, `simulationId` exactly |
| `BindingReject` | DEWB→DCAD | `profileRevision`, `errors[]: {code, joint, signal, message}` |
| `SynchronizeState` | DEWB→DCAD | complete state that starts a timeline or resynchronizes the current one |
| `StateApplied` | DCAD→DEWB | `simulationId`, `timelineGeneration`, `profileRevision`, `sequence` of the synchronization actually published under the current binding; `duplicate: true` when re-acknowledging (§6.3) |
| `StateFrame` | DEWB→DCAD | subsequent complete states |
| `LinkStatus` | both | `state`, `severity` (`info`/`warning`/`error`), `code`, `message`; DCAD adds `profileRevision`, `lastSequence`, optionally `errors[]` |
| `LifecycleCommand` | DCAD→DEWB | run-state request (§8) |
| `LifecycleResult` | DEWB→DCAD | outcome of a request (§8) |

Capabilities: DCAD advertises `mechanicalInterface.v1`, `bindingProposal.v1`, `stateFrame.v1`,
`heartbeat.v1`, `lifecycleControl.v1`. DEWB advertises `signalInterface.v1`, `synchronizeState.v1`,
`stateFrame.v1`, `heartbeat.v1`, and `lifecycleControl.v1` only if it implements §8.

State messages (`SynchronizeState`, `StateFrame`) carry: `simulationId`, `timelineGeneration` (int ≥ 1),
`profileRevision` (accepted), `sequence` (decimal string), `simTimePs` (decimal string), `microstep`
(int ≥ 0), `lifecycle` (`running`/`paused`/`stopped`/`faulted`), and `joints[]: {joint, coordinate,
velocity?}` with **every** joint driven by the accepted profile, coordinates absolute in rad or m,
optional velocities in rad/s or m/s. DCAD displays but never integrates velocities.

DEWB `LinkStatus` codes: `heartbeat` (with `state` = current lifecycle) and `goodbye` (ends the session
cleanly). Lifecycle changes are *published as states* (§8.4), not as `LinkStatus`.

## 6. Sequence and ordering

```
DEWB                                   DCAD
 | -- connect pipe ------------------->|  (connection alone is not readiness)
 | -- Hello -------------------------->|  new connection session
 |<-- Hello, MechanicalInterface ------|
 | -- SignalInterface ---------------->|  DCAD validates the saved profile against it
 |<-- BindingProposal -----------------|  (or LinkStatus error profileIncompatible)
 | -- BindingAccept / BindingReject -->|  accept must echo the exact revisions
 | -- SynchronizeState --------------->|  complete state
 |<-- StateApplied --------------------|  pose published to all CAD views
 | -- StateFrame, StateFrame, ... ---->|  stream; heartbeats at least every 1 s
```

### 6.1 Watermarks

Within one simulation session DCAD keeps ordering watermarks: the current `timelineGeneration`, the last
accepted `sequence`, the last `(simTimePs, microstep)`, and the `sequence` of the last accepted
`SynchronizeState`. Watermarks advance when a state passes validation, even if a newer state supersedes it
before it is drawn (obsolete states are skipped; the newest absolute state carries all accumulated motion).

### 6.2 Rules (both `SynchronizeState` and `StateFrame`)

1. A state whose `timelineGeneration` is **older** than the current one is always rejected
   (`staleTimeline`) — while bound, while awaiting synchronization and after revalidation.
2. A **newer** `timelineGeneration` may only begin with `SynchronizeState` (a `StateFrame` from a newer
   timeline is rejected with `synchronizeRequired`). A new timeline is the only way to rewind: reset,
   rewind and a fresh Start that reinitializes simulation time all use one. Its `sequence` may restart.
3. Within the **current** timeline every state — including a resynchronization — must have a `sequence`
   greater than the last accepted one (`staleSequence`) and must not move `(simTimePs, microstep)`
   backwards (`timeRegression`). A same-timeline `SynchronizeState` therefore can never restore an older pose.
4. `StateFrame`s are accepted only after the latest `SynchronizeState` of the current binding was
   acknowledged with `StateApplied` (`synchronizeRequired` otherwise). Frames never receive `StateApplied`.

### 6.3 Initial synchronization, revalidation, duplicates, reconnect

* **Initial synchronization**: after `BindingAccept`, send `SynchronizeState` (any `timelineGeneration` ≥ 1
  not older than the current one) and wait for `StateApplied` before streaming.
* **Reset / rewind / fresh start**: increment `timelineGeneration`, send `SynchronizeState`, wait for
  `StateApplied`, then stream: **new timeline → complete synchronization → `StateApplied` → streaming.**
* **Same-session revalidation**: when the DCAD interface or profile changes, DCAD sends `LinkStatus`
  `revalidationRequired`, a new `MechanicalInterface` and a new `BindingProposal`. Frames are rejected until
  the new binding is synchronized. After `BindingAccept`, resynchronize either on the current timeline
  with a newer `sequence` (watermarks are preserved through revalidation) or on a new timeline.
* **Duplicates**: a `SynchronizeState` whose `timelineGeneration` and `sequence` equal the last accepted
  synchronization is a duplicate. It is never reapplied. If that synchronization was already published
  under the current binding, DCAD repeats `StateApplied` with `duplicate: true`; otherwise it is ignored.
* **Session replacement**: a `SignalInterface` with a different `simulationId` clears the watermarks.
* **Reconnect**: a new `Hello` clears everything; the session starts from `SignalInterface`.
* **Disconnect**: DCAD holds the last valid pose and shows `Disconnected`.

Pause, single step and speed changes do not change the timeline; they are ordinary states (§8.4).
DCAD follows simulation state and time, not wall-clock time, and does not interpolate in v1.

## 7. Validation, invalidation and errors

DCAD validates a whole state before queuing it; an invalid state changes nothing. Queued work is stamped
with a binding epoch that advances whenever the connection session, simulation session, binding,
mechanical interface, profile or document is superseded (including undo/redo and New/Open). Before a
queued state is drawn, DCAD rechecks — atomically with publication — that its epoch, simulation, profile
and timeline are still current; superseded work is discarded and its `SynchronizeState` is never
acknowledged. `StateApplied` is sent only for a synchronization actually published under the current binding,
which includes solving the CAD mechanism: mapped coordinates are prescribed driver inputs and DCAD computes all
dependent joints itself (see [MECHANISM_CONSTRAINTS.md](MECHANISM_CONSTRAINTS.md)). If that solve fails, DCAD
keeps the last valid pose, reports `mechanismUnsolved`, and expects a new `SynchronizeState` (newer `sequence`
or timeline) rather than a resend of the same one. DCAD's status separates the received state from the last
posed state.

Errors are reported as `LinkStatus` (`severity: "error"`), rate-limited to one per code per second:

| code | cause |
|---|---|
| `helloRequired`, `unsupportedProtocol` | Hello missing, wrong protocol/version/role |
| `invalidMessage`, `messageTooLarge`, `unknownMessageType` | Malformed JSON or fields, payload > 1 MiB, unknown `type` |
| `profileIncompatible` (+ `errors[]`: `emptyProfile`, `unknownJoint`, `unknownSignal`, `quantityMismatch`, `unitMismatch`) | Profile does not fit the signal interface |
| `staleProfile`, `bindingTimeout`, `notBound` | Wrong revisions in accept/state, no answer within 10 s, state before acceptance |
| `wrongSimulation`, `staleTimeline`, `synchronizeRequired`, `staleSequence`, `timeRegression` | Superseded or out-of-order state (§6.2) |
| `unknownJoint`, `missingJoint`, `nonFiniteValue` | Joint list does not match the accepted profile, or values are not finite |
| `limitExceeded` (warning) | A coordinate is outside its engineering limits; the value is still applied, never clamped |
| `unknownRequest` (warning) | A `LifecycleResult` matches no outstanding DCAD request |
| `mechanismUnsolved` | The state is valid on the wire but DCAD's mechanism solver cannot pose it (conflicting inputs, hard bound, no convergence); the last valid pose stays; a `SynchronizeState` is not acknowledged |
| `mappingConflict` (in `profileIncompatible` errors) | A mapping targets a joint whose CAD mechanism role is dependent or locked; only driver joints can be mapped |
| `revalidationRequired` (warning), `heartbeat` (info) | Profile change; keep-alive |

DCAD display states: `Stopped`, `Listening`, `Connected`, `Validating`, `Awaiting synchronization`,
`Synchronized`, `Streaming`, `Paused`, `Disconnected`, `Error`.

## 8. Simulation lifecycle control (`lifecycleControl.v1`)

DCAD sends a `LifecycleCommand` only when **both** sides advertised `lifecycleControl.v1`, the binding is
accepted and its synchronization was acknowledged. Without the capability, motion streaming works
unchanged and DCAD shows why simulation controls are unavailable. Commands are never queued across
sessions: losing the connection or the binding fails the outstanding request on DCAD's side.

### 8.1 `LifecycleCommand` (DCAD → DEWB)

| field | type | meaning |
|---|---|---|
| `requestId` | string | Unique per operation (UUID). A retry of the same operation reuses it. |
| `modelId`, `simulationId`, `profileRevision` | string, string, int | The binding the request was issued under |
| `timelineGeneration` | int | Timeline the request was issued against |
| `command` | `start` \| `pause` \| `resume` \| `stop` \| `reset` \| `step` | Requested transition |
| `stepDurationPs` | decimal string > 0, or `null` | Simulation interval for `step`; `null` otherwise |

DCAD's **Play** control sends `resume` when the last confirmed lifecycle is `paused`, otherwise `start`.

### 8.2 `LifecycleResult` (DEWB → DCAD)

| field | type | meaning |
|---|---|---|
| `requestId` | string | Copied from the command |
| `simulationId`, `profileRevision` | string, int | Current binding identity |
| `command` | string | Copied from the command |
| `outcome` | `accepted` \| `applied` \| `rejected` | `accepted` (optional) = received and in progress, not done; `applied` = DEWB performed the transition; `rejected` = not performed |
| `lifecycle` | `running` \| `paused` \| `stopped` \| `faulted` | DEWB's lifecycle after handling the request |
| `timelineGeneration` | int | Timeline after handling the request (new timeline for reset / fresh start) |
| `simTimePs` | decimal string | Simulation time after handling the request |
| `stateSequence` | decimal string, optional | Sequence of the state that published the outcome |
| `code`, `message` | string | Required when `rejected`: an actionable reason |

Rejection codes: `invalidTransition`, `invalidStep`, `staleBinding`, `staleTimeline`, `busy`,
`unknownCommand`, `faulted`. A result never changes the displayed pose or lifecycle on its own; DCAD shows
the outcome next to the controls, and the confirmed state continues to come from state messages.

### 8.3 Transitions (validated by DEWB)

| command | permitted from | result | timeline |
|---|---|---|---|
| `start` | `stopped` | `running` | Same timeline if the simulation is still at its initial state (t = 0, never run); otherwise a **new timeline** with full synchronization before running |
| `pause` | `running` | `paused` | unchanged |
| `resume` | `paused` | `running` | unchanged |
| `stop` | `running`, `paused` | `stopped`, final state and pose retained | unchanged |
| `reset` | any (including `faulted`) | `stopped` at the initial state (t = 0) | **new timeline**, synchronized before the result |
| `step` | `paused`, or `stopped` at the initial state | `paused`, advanced by exactly `stepDurationPs` | unchanged |

Stop Simulation is distinct from DCAD's Stop Listening and Disconnect, which only affect the transport.

### 8.4 Publication

DEWB publishes every confirmed run-state change immediately, without waiting for a solver step:

* pause, resume, stop, step, fault: a complete `StateFrame` with the new `lifecycle` (the same `simTimePs`
  is allowed; the `sequence` advances);
* reset and a fresh start: `SynchronizeState` on a new timeline, then wait for `StateApplied`;
* then the `LifecycleResult` (`applied`), so the result never precedes the state it describes.

While paused or stopped, heartbeats keep the link alive; they do not advance time or move the model.

### 8.5 Delivery, timeouts and deduplication

* One request is outstanding at a time. DCAD correlates results by `requestId`; results with an unknown
  `requestId` or another `simulationId` are ignored and reported (`unknownRequest`).
* If no result (or `accepted` notice) arrives within 5 s — 30 s after `accepted` — DCAD marks the request
  unconfirmed and offers **Retry**, which resends the identical message with the **same `requestId`**.
* **DEWB must deduplicate by `requestId`**: keep the results of recent requests for the session; a repeated
  `requestId` returns the stored result without executing the command again. A repeated Single Step
  therefore advances simulation time once.

## 9. Files

* `motion_interface_v1/schemas/*.schema.json` — JSON Schema (draft 2020-12) for each message type.
* `motion_interface_v1/fixtures/*.json` — representative messages for the shaft-and-carriage demo.
* `tools/motion_peer/dewb_motion_peer.py` — Python 3.6+ protocol stand-in for DEWB (discovery, framing,
  handshake, synchronization, streaming, stale-state, invalidation, stall/reconnect and lifecycle scenarios).
  Its lifecycle answers demonstrate the message contract only; they are not DEWB's simulator controls.

## 10. Known limitations of version 1

* No interpolation between states; DCAD shows the newest validated state.
* The endpoint uses JUCE named pipes with default security; remote-client rejection
  (`PIPE_REJECT_REMOTE_CLIENTS`) would need a custom Win32 pipe transport.
* JUCE allocates a received payload before DCAD can enforce the 1 MiB limit; oversized messages are
  rejected after receipt.
* A JUCE server pipe silently waits for the next client after a client disconnects, so DCAD detects
  disconnects through heartbeats (3 s) and write failures, not through an immediate pipe event.
* Driven relationships only: no assembly constraint solving and no force/feedback co-simulation.
