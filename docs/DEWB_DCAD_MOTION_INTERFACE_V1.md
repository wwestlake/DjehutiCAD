# DEWB–DCAD Motion Interface, Version 1

This document is the implementation reference for both sides of the link between the
Djehuti Electronics Workbench system simulator (**DEWB**) and DjehutiCAD (**DCAD**).
Machine-readable schemas are in [`motion_interface_v1/schemas`](motion_interface_v1/schemas),
example messages are in [`motion_interface_v1/fixtures`](motion_interface_v1/fixtures), and an
executable protocol peer is in [`../tools/motion_peer/dewb_motion_peer.py`](../tools/motion_peer/dewb_motion_peer.py).

## 1. Responsibilities

| Owner | Owns |
|---|---|
| DCAD document (OCAF) | Geometry, assembly instances, joints, reference placements, the connection profile (signal mappings) |
| DEWB | Numerical simulation, physical behaviour, simulation time, authoritative joint coordinates |
| DCAD runtime scene | Temporary poses computed from received coordinates (never written to the document) |
| Motion link | Interface exchange, profile validation, synchronization, state delivery |

DEWB decides what the machine does; DCAD decides how that state places the modelled assembly.
DCAD never integrates velocities and never invents coordinates.

## 2. Transport

* **Endpoint**: DCAD is the pipe server. Each DCAD "Start Listening" creates a Windows named pipe
  `\\.\pipe\DjehutiCAD.Motion.<pid>.<token>` (`<token>` is 12 random hex digits, so concurrent
  DCAD processes and documents never collide). DEWB is the client and opens the pipe for read and write.
* **Discovery**: while listening, DCAD writes
  `%LOCALAPPDATA%\DjehutiCAD\MotionEndpoints\<pipeName>.json`
  (`protocol`, `protocolVersion`, `pipeName`, `pipePath`, `processId`, `modelId`, `documentName`, `startedUtc`)
  and deletes it when listening stops. The pipe path is also shown in the Motion Link panel.
* **Framing** (JUCE `InterprocessConnection`): every message is
  `uint32 magic` + `uint32 payloadLength` + `payload`. Both words are **little-endian**;
  `magic = 0xF2B49E2C` (bytes `2C 9E B4 F2`). `payloadLength` counts payload bytes only.
* **Payload**: one UTF-8 JSON object, no BOM, no trailing NUL. Every object has a string `type`.
* **Limits**: payloads up to 1 MiB. DCAD queues at most 64 outgoing messages; a peer that stops
  reading is disconnected. Send at most one `StateFrame` per rendered frame you care about; DCAD keeps
  only the newest unpublished state and never builds a backlog.
* **Liveness**: both sides send at least one message every 1000 ms while connected (a `LinkStatus`
  with `code: "heartbeat"` when idle). DCAD treats 3000 ms of silence as a disconnect, holds the last
  pose, and reports `Disconnected`. A pipe connection alone never means motion is ready.
* **Reconnection**: every `Hello` from DEWB starts a new session. Nothing (binding, timeline,
  synchronization) carries over; DEWB must repeat the full sequence.
* **Scope**: the pipe is created with the default Windows pipe security (owner/administrators have
  write access). JUCE does not expose `PIPE_REJECT_REMOTE_CLIENTS`; the random token and the user-local
  discovery file keep the endpoint private in practice. See §9.

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
  the sim_core superdense-time microstep (integer ≥ 0, default 0). `sequence` is a decimal string
  (unsigned, < 2^63). Revisions and `timelineGeneration` are JSON integers (< 2^31).

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
`acceptedSourceQuantities`. Ids are stable through save/load, JSON interchange and renames; names are display text.

### 4.2 DEWB signal interface — `SignalInterface` (DEWB → DCAD)

`simulationId` (string, identifies the simulation session/model), optional `simulationName`,
`signalInterfaceRevision` (string or int, echoed back), and `signals[]`:
`{id, name, quantity, unit, kind}` where `kind` is `"state"` for `angle`/`position` and `"rate"` for
velocities. Ids must be unique and stable. DCAD never matches signals by name.

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

Validation (both sides): the profile has ≥ 1 mapping; every joint exists and is not fixed; the source
quantity drives that joint type; the signal exists; quantity, unit and kind match; numbers are finite and
`scale ≠ 0`. DCAD refuses to propose an incompatible profile and reports `profileIncompatible` with an
`errors[]` list; DEWB answers a proposal with `BindingAccept` or `BindingReject`.

## 5. Messages

| type | direction | purpose |
|---|---|---|
| `Hello` | both | protocol `"djehuti.motion"`, `protocolVersion: 1`, `role` (`"dewb"`/`"dcad"`), `application`, `applicationVersion`, `capabilities[]` |
| `MechanicalInterface` | DCAD→DEWB | §4.1 |
| `SignalInterface` | DEWB→DCAD | §4.2 |
| `BindingProposal` | DCAD→DEWB | §4.3 plus `modelId`, `interfaceRevision`, `profileRevision`, `simulationId`, `signalInterfaceRevision` |
| `BindingAccept` | DEWB→DCAD | echoes `modelId`, `interfaceRevision`, `profileRevision`, `simulationId` exactly |
| `BindingReject` | DEWB→DCAD | `profileRevision`, `errors[]: {code, joint, signal, message}` |
| `SynchronizeState` | DEWB→DCAD | complete state that starts (or restarts) a timeline |
| `StateApplied` | DCAD→DEWB | `simulationId`, `timelineGeneration`, `profileRevision`, `sequence` of the synchronize state, sent after its pose is published |
| `StateFrame` | DEWB→DCAD | subsequent complete states |
| `LinkStatus` | both | `state`, `severity` (`info`/`warning`/`error`), `code`, `message`; DCAD adds `profileRevision`, `lastSequence`, optionally `errors[]` |

State messages (`SynchronizeState`, `StateFrame`) carry: `simulationId`, `timelineGeneration` (int ≥ 1),
`profileRevision` (accepted), `sequence` (decimal string), `simTimePs` (decimal string), `microstep`
(int ≥ 0), `lifecycle` (`running`/`paused`/`stopped`/`faulted`), and `joints[]: {joint, coordinate,
velocity?}` with **every** joint driven by the accepted profile, coordinates absolute in rad or m,
optional velocities in rad/s or m/s. DCAD displays but never integrates velocities.

DEWB `LinkStatus.state` values: `ready`, `running`, `paused`, `stopped`, `faulted`; `code: "goodbye"`
ends the session cleanly. DCAD `LinkStatus.state` is its display state (§7).

## 6. Sequence

```
DEWB                                   DCAD
 | -- connect pipe ------------------->|  (connection alone is not readiness)
 | -- Hello -------------------------->|  new session
 |<-- Hello, MechanicalInterface ------|
 | -- SignalInterface ---------------->|  DCAD validates the saved profile against it
 |<-- BindingProposal -----------------|  (or LinkStatus error profileIncompatible)
 | -- BindingAccept / BindingReject -->|  accept must echo the exact revisions
 | -- SynchronizeState --------------->|  complete state, timeline g
 |<-- StateApplied --------------------|  pose published to all CAD views
 | -- StateFrame, StateFrame, ... ---->|  stream; heartbeats at least every 1 s
```

* **Ordering**: within a timeline, `sequence` strictly increases and `(simTimePs, microstep)` never
  decreases. Frames are rejected (`staleSequence`, `timeRegression`) otherwise.
* **Synchronization before streaming**: frames before `StateApplied` are rejected (`synchronizeRequired`).
* **Pause / step / speed**: keep sending complete states with the current `lifecycle`; single steps are
  ordinary frames. DCAD follows simulation state and time, not wall-clock time, and does not interpolate in v1.
* **Reset / rewind**: start a new timeline with `SynchronizeState` and a higher `timelineGeneration`;
  wait for `StateApplied`. Frames from an older timeline are rejected (`staleTimeline`); frames from a newer
  one before its synchronization are rejected (`synchronizeRequired`).
* **Revalidation**: when the DCAD interface or profile changes, DCAD sends `LinkStatus`
  (`code: "revalidationRequired"`), a new `MechanicalInterface` and a new `BindingProposal`. Frames with
  the old `profileRevision` are rejected (`staleProfile`); after `BindingAccept`, DEWB sends a new
  `SynchronizeState`. A new `SignalInterface` from DEWB likewise triggers a new proposal.
* **Disconnect**: DCAD holds the last valid pose and shows `Disconnected`. Reconnect = new `Hello`,
  full validation and synchronization.

## 7. Validation and errors

DCAD validates a whole state before publishing any of it; an invalid state changes nothing. Errors are
reported as `LinkStatus` (`severity: "error"`) with these codes (rate-limited to one per code per second):

| code | cause |
|---|---|
| `helloRequired`, `unsupportedProtocol` | Hello missing, wrong protocol/version/role |
| `invalidMessage`, `messageTooLarge`, `unknownMessageType` | Malformed JSON or fields, payload > 1 MiB, unknown `type` |
| `profileIncompatible` (+ `errors[]` with `emptyProfile`, `unknownJoint`, `unknownSignal`, `quantityMismatch`, `unitMismatch`) | Profile does not fit the signal interface |
| `staleProfile`, `bindingTimeout`, `notBound` | Wrong revisions in accept/state, no answer within 10 s, state before acceptance |
| `wrongSimulation`, `staleTimeline`, `synchronizeRequired`, `staleSequence`, `timeRegression` | Superseded or out-of-order state |
| `unknownJoint`, `missingJoint`, `nonFiniteValue` | Joint list does not match the accepted profile, or values are not finite |
| `limitExceeded` (warning) | A coordinate is outside its engineering limits; the value is still applied, never clamped |
| `revalidationRequired` (warning), `heartbeat` (info) | Profile change; keep-alive |

DCAD display states: `Stopped`, `Listening`, `Connected`, `Validating`, `Awaiting synchronization`,
`Synchronized`, `Streaming`, `Paused`, `Disconnected`, `Error`.

## 8. Files

* `motion_interface_v1/schemas/*.schema.json` — JSON Schema (draft 2020-12) for each message type.
* `motion_interface_v1/fixtures/*.json` — one representative message per type for the shaft-and-carriage demo.
* `tools/motion_peer/dewb_motion_peer.py` — Python 3.6+ peer that speaks this protocol over the real pipe
  (discovery, framing, handshake, synchronization, streaming, pause, reset, mismatch and reconnect scenarios).

## 9. Known limitations of version 1

* No interpolation between states; DCAD shows the newest validated state.
* The endpoint uses JUCE named pipes with default security; remote-client rejection
  (`PIPE_REJECT_REMOTE_CLIENTS`) would need a custom Win32 pipe transport.
* JUCE allocates a received payload before DCAD can enforce the 1 MiB limit; oversized messages are
  rejected after receipt.
* Driven relationships only: no assembly constraint solving and no force/feedback co-simulation.
