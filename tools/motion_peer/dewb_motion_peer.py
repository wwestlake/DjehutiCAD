#!/usr/bin/env python3
"""DEWB-side protocol peer for the DEWB-DCAD motion interface, version 1.

This script stands in for DEWB: it connects to the DCAD named pipe, exchanges interfaces, validates
and accepts DCAD's binding proposal, synchronizes a complete state, streams motion frames and answers
lifecycle commands exactly as docs/DEWB_DCAD_MOTION_INTERFACE_V1.md specifies. It is an executable
example of the wire protocol, NOT a simulator: signal values come from simple functions of simulation
time, and its lifecycle handling only demonstrates the message contract DEWB has to implement.

Usage (Windows, Python 3.6+):
  python dewb_motion_peer.py --signal carriage.position:position:mm:state --signal spindle.speed:angular_velocity:rpm:rate
  python dewb_motion_peer.py --scenario lifecycle --seconds 120 --duplicate-step --signal ...

Scenarios:
  full        handshake, streaming, pause/step/speed, invalid and stale states, reset, drop, reconnect
  mismatch    advertise signals that do not fit the profile; DCAD refuses to propose
  stream      synchronize and stream
  pause       stream, then stay paused
  drop        stream, then vanish without goodbye
  lifecycle   serve DCAD's Start/Pause/Resume/Stop/Reset/Single Step requests (use --seconds for how long)
  invalidate  needs DCAD started with DJEHUTI_MOTION_TEST_PUBLISH_DELAY_MS=1500: a queued SynchronizeState
              is superseded by a new simulation session before publication and must never be acknowledged
  stall       stop reading while provoking replies; DCAD must close the connection; then reconnect

The pipe is discovered from %LOCALAPPDATA%\\DjehutiCAD\\MotionEndpoints unless --pipe is given.
"""
import argparse
import ctypes
import glob
import json
import math
import msvcrt
import os
import struct
import sys
import time
from ctypes import wintypes

MAGIC = 0xF2B49E2C
TICKS_PER_SECOND = 10 ** 12
FRAME_RATE = 30.0
LIFECYCLE_CAPABILITY = "lifecycleControl.v1"
UNIT_TO_SI = {
    ("angle", "rad"): 1.0, ("angle", "deg"): math.pi / 180.0, ("angle", "rev"): 2.0 * math.pi,
    ("angular_velocity", "rad/s"): 1.0, ("angular_velocity", "deg/s"): math.pi / 180.0,
    ("angular_velocity", "rpm"): 2.0 * math.pi / 60.0, ("angular_velocity", "rev/s"): 2.0 * math.pi,
    ("position", "m"): 1.0, ("position", "mm"): 1.0e-3,
    ("linear_velocity", "m/s"): 1.0, ("linear_velocity", "mm/s"): 1.0e-3,
}
RATE_QUANTITIES = ("angular_velocity", "linear_velocity")

_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
_kernel32.PeekNamedPipe.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
_kernel32.PeekNamedPipe.restype = wintypes.BOOL


def log(text):
    print(time.strftime("%H:%M:%S"), text, flush=True)


def source_value_si(quantity, t):
    """Demonstration signal values in SI units as a function of simulation time t (s)."""
    if quantity == "position":
        return 0.040 * math.sin(2.0 * math.pi * 0.2 * t)          # +-40 mm carriage stroke
    if quantity == "linear_velocity":
        return 0.040 * 2.0 * math.pi * 0.2 * math.cos(2.0 * math.pi * 0.2 * t)
    if quantity == "angle":
        return 2.0 * math.pi * 0.5 * t                              # half a turn per second
    if quantity == "angular_velocity":
        return 2.0 * math.pi * 0.5                                  # 30 rpm
    raise ValueError(quantity)


class Pipe:
    """JUCE InterprocessConnection framing over a Windows named pipe: <u32 magic><u32 length><UTF-8 JSON>."""

    def __init__(self, path):
        self.file = open(path, "r+b", buffering=0)
        self.handle = msvcrt.get_osfhandle(self.file.fileno())
        self.buffer = b""

    def send(self, message):
        payload = json.dumps(message, separators=(",", ":")).encode("utf-8")
        self.file.write(struct.pack("<II", MAGIC, len(payload)) + payload)

    def _available(self):
        available = wintypes.DWORD(0)
        if not _kernel32.PeekNamedPipe(self.handle, None, 0, None, ctypes.byref(available), None):
            raise ConnectionError("pipe closed (error %d)" % ctypes.get_last_error())
        return available.value

    def poll(self):
        """Return every complete message that has arrived, without blocking."""
        count = self._available()
        if count:
            self.buffer += self.file.read(count)
        messages = []
        while len(self.buffer) >= 8:
            magic, length = struct.unpack("<II", self.buffer[:8])
            if magic != MAGIC:
                raise ValueError("bad frame header %08x" % magic)
            if len(self.buffer) < 8 + length:
                break
            messages.append(json.loads(self.buffer[8:8 + length].decode("utf-8")))
            self.buffer = self.buffer[8 + length:]
        return messages

    def close(self):
        try:
            self.file.close()
        except OSError:
            pass


class Peer:
    def __init__(self, pipe_path, signals, simulation_id, lifecycle_control=True):
        self.pipe_path = pipe_path
        self.signals = signals
        self.simulation_id = simulation_id
        self.lifecycle_control = lifecycle_control
        self.pipe = None
        self.mappings = []
        self.model_id = None
        self.profile_revision = None
        self.generation = 0
        self.sequence = 0
        self.tick = 0
        self.integrals = {}
        self.lifecycle = "stopped"
        self.initial = True              # True while the current timeline is still at its initial state
        self.results = {}                # requestId -> LifecycleResult already produced (deduplication)
        self.last_heartbeat = 0.0
        self.received = []
        self.consumed = 0                # cursor for wait_for
        self.processed = 0               # cursor for the lifecycle command server
        self.dropped_once = set()

    # ---- transport helpers -------------------------------------------------------------
    def pump(self):
        for message in self.pipe.poll():
            self.received.append(message)
            kind = message.get("type")
            if kind == "LinkStatus" and message.get("code") == "heartbeat":
                continue
            summary = kind
            if kind == "LinkStatus":
                summary += " [%s/%s] %s" % (message.get("severity"), message.get("code"), message.get("message"))
            elif kind == "StateApplied":
                summary += " sequence %s%s" % (message.get("sequence"), " (duplicate)" if message.get("duplicate") else "")
            elif kind == "LifecycleCommand":
                summary += " %s (%s)" % (message.get("command"), message.get("requestId"))
            log("DCAD -> " + summary)
        if time.time() - self.last_heartbeat > 0.5:
            self.last_heartbeat = time.time()
            self.pipe.send({"type": "LinkStatus", "state": self.lifecycle, "severity": "info", "code": "heartbeat", "message": ""})

    def wait_for(self, predicate, timeout=10.0):
        """Return the first not-yet-consumed DCAD message matching predicate."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.pump()
            for index in range(self.consumed, len(self.received)):
                if predicate(self.received[index]):
                    self.consumed = index + 1
                    return self.received[index]
            time.sleep(0.01)
        raise TimeoutError("timed out waiting for DCAD")

    def expect_none(self, predicate, seconds):
        """Pump for a while and confirm DCAD sent nothing matching predicate."""
        start = len(self.received)
        self.sleep(seconds)
        return not any(predicate(m) for m in self.received[start:])

    def sleep(self, seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            self.pump()
            time.sleep(0.02)

    # ---- handshake and binding ---------------------------------------------------------
    def connect(self):
        self.pipe = Pipe(self.pipe_path)
        self.received, self.consumed, self.processed = [], 0, 0
        self.generation, self.sequence = 0, 0
        log("connected to " + self.pipe_path)
        capabilities = ["signalInterface.v1", "synchronizeState.v1", "stateFrame.v1", "heartbeat.v1"]
        if self.lifecycle_control:
            capabilities.append(LIFECYCLE_CAPABILITY)
        self.pipe.send({"type": "Hello", "protocol": "djehuti.motion", "protocolVersion": 1, "role": "dewb",
                        "application": "dewb_motion_peer", "applicationVersion": "1.1", "capabilities": capabilities})
        hello = self.wait_for(lambda m: m.get("type") == "Hello")
        self.dcad_lifecycle = LIFECYCLE_CAPABILITY in hello.get("capabilities", [])
        interface = self.wait_for(lambda m: m.get("type") == "MechanicalInterface")
        log("mechanical interface: model %s, revision %s, %d joints; DCAD lifecycle control: %s"
            % (interface["modelId"], interface["interfaceRevision"], len(interface["joints"]), self.dcad_lifecycle))

    def advertise(self, simulation_id=None):
        self.simulation_id = simulation_id or self.simulation_id
        self.pipe.send({"type": "SignalInterface", "simulationId": self.simulation_id, "simulationName": "Protocol peer",
                        "signalInterfaceRevision": 1, "signals": self.signals})

    def bind(self, proposal=None):
        if proposal is None:
            proposal = self.wait_for(lambda m: m.get("type") == "BindingProposal" or (m.get("type") == "LinkStatus" and m.get("severity") == "error"))
        if proposal["type"] != "BindingProposal":
            log("DCAD refused to propose: %s" % proposal.get("message"))
            for error in proposal.get("errors", []):
                log("   %s: %s" % (error.get("code"), error.get("message")))
            return False
        problems = self.validate_proposal(proposal)
        if problems:
            self.pipe.send({"type": "BindingReject", "profileRevision": proposal["profileRevision"], "errors": problems})
            log("rejected profile: %s" % problems)
            return False
        self.mappings = proposal["mappings"]
        self.model_id = proposal["modelId"]
        self.profile_revision = proposal["profileRevision"]
        self.pipe.send({"type": "BindingAccept", "modelId": proposal["modelId"], "interfaceRevision": proposal["interfaceRevision"],
                        "profileRevision": proposal["profileRevision"], "simulationId": self.simulation_id, "signalInterfaceRevision": 1})
        log("accepted profile revision %d with %d mappings" % (self.profile_revision, len(self.mappings)))
        return True

    def advertise_and_bind(self):
        self.advertise()
        return self.bind()

    def validate_proposal(self, proposal):
        """Source-side validation of the proposed profile (spec section 4.3)."""
        catalog = {s["id"]: s for s in self.signals}
        problems = []
        for mapping in proposal["mappings"]:
            signal = catalog.get(mapping["signal"])
            if signal is None:
                problems.append({"code": "unknownSignal", "joint": mapping["joint"], "signal": mapping["signal"], "message": "no such signal"})
            elif signal["quantity"] != mapping["sourceQuantity"] or signal["unit"] != mapping["sourceUnit"]:
                problems.append({"code": "quantityMismatch", "joint": mapping["joint"], "signal": mapping["signal"], "message": "quantity or unit differs"})
            elif abs(UNIT_TO_SI[(signal["quantity"], signal["unit"])] - mapping["unitToSI"]) > 1e-12:
                problems.append({"code": "unitMismatch", "joint": mapping["joint"], "signal": mapping["signal"], "message": "unit factor differs"})
        return problems

    # ---- state publication -------------------------------------------------------------
    def coordinates(self, t, dt):
        """Joint coordinates from the accepted profile: state = offset + scale*unitToSI*value; rate integrated in simulation time."""
        joints = []
        for mapping in self.mappings:
            quantity = mapping["sourceQuantity"]
            unit_value = source_value_si(quantity, t) / UNIT_TO_SI[(quantity, mapping["sourceUnit"])]   # what DEWB's signal reports
            rate_si = mapping["scale"] * mapping["unitToSI"] * unit_value
            if quantity in RATE_QUANTITIES:
                key = mapping["joint"]
                self.integrals[key] = self.integrals.get(key, mapping["initialCoordinate"]) + rate_si * dt
                joints.append({"joint": key, "coordinate": self.integrals[key], "velocity": rate_si})
            else:
                joints.append({"joint": mapping["joint"], "coordinate": mapping["offset"] + rate_si})
        return joints

    def seconds(self):
        return self.tick / TICKS_PER_SECOND

    def state(self, kind, lifecycle, joints, generation=None, sequence=None):
        if sequence is None:
            self.sequence += 1
            sequence = self.sequence
        return {"type": kind, "simulationId": self.simulation_id, "timelineGeneration": generation or self.generation,
                "profileRevision": self.profile_revision, "sequence": str(sequence), "simTimePs": str(self.tick),
                "microstep": 0, "lifecycle": lifecycle, "joints": joints}

    def publish(self, lifecycle=None):
        """Publish a complete state now (lifecycle changes do not wait for the next solver step)."""
        self.lifecycle = lifecycle or self.lifecycle
        message = self.state("StateFrame", self.lifecycle, self.coordinates(self.seconds(), 0.0))
        self.pipe.send(message)
        return message

    def synchronize(self, lifecycle="paused", new_timeline=True):
        if new_timeline:
            self.generation += 1
            self.tick = 0
            self.integrals = {}
            self.initial = True
        self.lifecycle = lifecycle
        message = self.state("SynchronizeState", lifecycle, self.coordinates(self.seconds(), 0.0))
        self.pipe.send(message)
        self.wait_for(lambda m: m.get("type") == "StateApplied" and m.get("sequence") == message["sequence"] and not m.get("duplicate"))
        log("synchronized timeline %d at t=%.3f s (sequence %s, %s)" % (self.generation, self.seconds(), message["sequence"], lifecycle))
        self.last_sync = message
        return message

    def advance(self, seconds):
        """Advance simulation time, integrating in steps no longer than one frame."""
        remaining = seconds
        while remaining > 1e-12:
            dt = min(remaining, 1.0 / FRAME_RATE)
            self.tick += int(round(dt * TICKS_PER_SECOND))
            self.coordinates(self.seconds(), dt)
            remaining -= dt

    def stream(self, seconds, rate_hz=FRAME_RATE, speed=1.0, lifecycle="running"):
        frames = int(seconds * rate_hz)
        dt = speed / rate_hz
        self.lifecycle = lifecycle
        for _ in range(frames):
            self.tick += int(round(dt * TICKS_PER_SECOND))
            self.pipe.send(self.state("StateFrame", lifecycle, self.coordinates(self.seconds(), dt)))
            self.sleep(1.0 / rate_hz)
        log("streamed %d frames to t=%.3f s (%s)" % (frames, self.seconds(), lifecycle))

    def disconnect(self, goodbye=True):
        if goodbye:
            try:
                self.pipe.send({"type": "LinkStatus", "state": "stopped", "severity": "info", "code": "goodbye", "message": "peer closing"})
                time.sleep(0.2)
            except OSError:
                pass
        self.pipe.close()
        log("disconnected")

    # ---- lifecycle commands (DEWB responsibilities, demonstrated) ---------------------
    def result(self, request, outcome, code=None, message=None):
        body = {"type": "LifecycleResult", "requestId": request["requestId"], "simulationId": self.simulation_id,
                "profileRevision": self.profile_revision, "command": request.get("command"), "outcome": outcome,
                "lifecycle": self.lifecycle, "timelineGeneration": self.generation, "simTimePs": str(self.tick),
                "stateSequence": str(self.sequence)}
        if code:
            body["code"] = code
            body["message"] = message or code
        return body

    def handle_command(self, request, drop_first=None):
        request_id = request.get("requestId")
        if request_id in self.results:
            # Deduplication: a repeated delivery of a request returns the original result and is never executed again.
            log("duplicate delivery of %s (%s): returning the stored result, not executing again (t=%.3f s)" % (request.get("command"), request_id, self.seconds()))
            self.pipe.send(self.results[request_id])
            return
        command = request.get("command")
        if request.get("simulationId") != self.simulation_id or request.get("modelId") != self.model_id or request.get("profileRevision") != self.profile_revision:
            body = self.result(request, "rejected", "staleBinding", "The command was issued for a different binding.")
        elif request.get("timelineGeneration") != self.generation:
            body = self.result(request, "rejected", "staleTimeline", "The command refers to timeline %s; the current timeline is %d." % (request.get("timelineGeneration"), self.generation))
        else:
            body = self.execute(request, command)
        self.results[request_id] = body
        if drop_first == command and command not in self.dropped_once:
            self.dropped_once.add(command)
            log("executed %s but dropped its result to simulate a lost reply" % command)
            return
        self.pipe.send(body)
        log("LifecycleResult %s %s -> %s, timeline %d, t=%.3f s" % (command, body["outcome"], self.lifecycle, self.generation, self.seconds()))

    def execute(self, request, command):
        state = self.lifecycle
        if command == "start":
            if state != "stopped":
                return self.result(request, "rejected", "invalidTransition", "Start needs a stopped simulation; it is %s." % state)
            if not self.initial:
                self.synchronize(lifecycle="running")           # fresh start: new timeline, full synchronization first
            self.lifecycle, self.initial = "running", False
            self.publish("running")
        elif command == "pause":
            if state != "running":
                return self.result(request, "rejected", "invalidTransition", "Pause needs a running simulation; it is %s." % state)
            self.publish("paused")
        elif command == "resume":
            if state != "paused":
                return self.result(request, "rejected", "invalidTransition", "Resume needs a paused simulation; it is %s." % state)
            self.publish("running")
        elif command == "stop":
            if state not in ("running", "paused"):
                return self.result(request, "rejected", "invalidTransition", "Stop needs a running or paused simulation; it is %s." % state)
            self.publish("stopped")
        elif command == "reset":
            self.synchronize(lifecycle="stopped")               # initial state on a new timeline, synchronized before the result
        elif command == "step":
            if not (state == "paused" or (state == "stopped" and self.initial)):
                return self.result(request, "rejected", "invalidTransition", "Single Step needs a paused simulation (or the initial state); it is %s." % state)
            try:
                duration = int(request.get("stepDurationPs") or "0")
            except ValueError:
                duration = 0
            if duration <= 0:
                return self.result(request, "rejected", "invalidStep", "stepDurationPs must be a positive decimal string.")
            before = self.seconds()
            self.advance(duration / TICKS_PER_SECOND)
            self.initial = False
            self.publish("paused")
            log("single step advanced simulation time %.6f s -> %.6f s" % (before, self.seconds()))
        else:
            return self.result(request, "rejected", "unknownCommand", "Unsupported command %s." % command)
        return self.result(request, "applied")

    def serve(self, seconds, drop_first=None, duplicate_step=False, stale_after_revalidation=False):
        """Answer DCAD commands and revalidation for a while; stream while running."""
        deadline = time.time() + seconds
        self.processed = len(self.received)        # earlier handshake messages are already handled
        while time.time() < deadline:
            self.pump()
            while self.processed < len(self.received):
                message = self.received[self.processed]
                self.processed += 1
                kind = message.get("type")
                if kind == "LifecycleCommand":
                    self.handle_command(message, drop_first)
                    if duplicate_step and message.get("command") == "step":
                        log("--- simulating a repeated delivery of the same Single Step request")
                        self.handle_command(message)
                elif kind == "BindingProposal":
                    # Same-session revalidation: accept again, then resynchronize the current timeline.
                    self.consumed = max(self.consumed, self.processed)
                    if self.bind(message):
                        if stale_after_revalidation and self.generation > 1:
                            log("--- delayed SynchronizeState from superseded timeline %d (must be refused while awaiting synchronization)" % (self.generation - 1))
                            self.pipe.send(self.state("SynchronizeState", self.lifecycle, self.coordinates(0.0, 0.0), generation=self.generation - 1))
                        self.synchronize(lifecycle=self.lifecycle, new_timeline=False)
            if self.lifecycle == "running":
                self.tick += int(round(TICKS_PER_SECOND / FRAME_RATE))
                self.pipe.send(self.state("StateFrame", "running", self.coordinates(self.seconds(), 1.0 / FRAME_RATE)))
            time.sleep(1.0 / FRAME_RATE)


def discover_pipe():
    folder = os.path.join(os.environ.get("LOCALAPPDATA", ""), "DjehutiCAD", "MotionEndpoints")
    entries = sorted(glob.glob(os.path.join(folder, "*.json")), key=os.path.getmtime, reverse=True)
    if not entries:
        raise SystemExit("No DCAD motion endpoint found; press Start Listening in DjehutiCAD or pass --pipe.")
    with open(entries[0], encoding="utf-8") as handle:
        return json.load(handle)["pipePath"]


def parse_signal(text):
    identifier, quantity, unit, kind = text.split(":")
    if (quantity, unit) not in UNIT_TO_SI:
        raise SystemExit("unsupported quantity/unit: " + text)
    return {"id": identifier, "name": identifier, "quantity": quantity, "unit": unit, "kind": kind}


def scenario_full(peer, args):
    peer.synchronize()
    peer.stream(args.seconds)

    log("--- stale synchronization: replay the initial SynchronizeState of this timeline (duplicate, must not rewind)")
    peer.pipe.send(dict(peer.first_sync))
    peer.sleep(0.5)
    log("--- stale synchronization: same timeline, older sequence (must be refused)")
    peer.pipe.send(peer.state("SynchronizeState", "running", peer.coordinates(0.0, 0.0), sequence=2))
    peer.sleep(0.5)
    log("--- stale synchronization: newer sequence but earlier simulation time (must be refused; rewinds need a new timeline)")
    saved = peer.tick
    peer.tick = 0
    peer.pipe.send(peer.state("SynchronizeState", "running", peer.coordinates(0.0, 0.0)))
    peer.tick = saved
    peer.sleep(0.5)
    peer.stream(1.0)

    log("--- pause: complete states continue with lifecycle=paused, time frozen")
    peer.publish("paused")
    peer.sleep(1.5)
    log("--- invalid frame (missing joint) must be refused and leave the pose unchanged")
    peer.pipe.send(peer.state("StateFrame", "running", peer.coordinates(peer.seconds(), 0.0)[1:]))
    peer.sleep(0.5)

    log("--- reset: new timeline generation starts at t=0")
    old_generation = peer.generation
    peer.synchronize(lifecycle="running")
    log("--- delayed SynchronizeState and StateFrame from the superseded timeline must be refused")
    peer.pipe.send(peer.state("SynchronizeState", "running", peer.coordinates(0.0, 0.0), generation=old_generation))
    peer.pipe.send(peer.state("StateFrame", "running", peer.coordinates(0.0, 0.0), generation=old_generation))
    peer.sleep(0.5)
    peer.stream(2.0)

    log("--- abrupt disconnect (no goodbye): DCAD holds the pose and reports Disconnected after 3 s")
    peer.disconnect(goodbye=False)
    time.sleep(4.0)
    log("--- reconnect: fresh Hello, validation and synchronization")
    peer.connect()
    if peer.advertise_and_bind():
        peer.synchronize(lifecycle="running")
        peer.stream(2.0)
    peer.disconnect()


def scenario_invalidate(peer, args):
    log("--- SynchronizeState queued in DCAD, then the simulation session is replaced before publication")
    peer.generation += 1
    peer.tick, peer.integrals = 0, {}
    queued = peer.state("SynchronizeState", "paused", peer.coordinates(0.0, 0.0))
    peer.pipe.send(queued)
    peer.sleep(0.3)
    peer.advertise(simulation_id="peer-sim-2")
    acknowledged = not peer.expect_none(lambda m: m.get("type") == "StateApplied" and m.get("sequence") == queued["sequence"], 3.0)
    log("queued state from the superseded session %s" % ("WAS acknowledged (failure)" if acknowledged else "was discarded: no StateApplied"))
    log("--- the new session binds and synchronizes normally")
    if peer.bind():
        peer.generation, peer.sequence = 0, 0
        peer.synchronize()
        peer.stream(2.0)
    peer.disconnect()


def scenario_stall(peer, args):
    peer.synchronize()
    peer.stream(1.0)
    log("--- stop reading and send repeated SynchronizeState copies; each asks DCAD for a duplicate StateApplied")
    try:
        for _ in range(400):
            peer.pipe.send(dict(peer.first_sync))
            time.sleep(0.01)
        log("still writing; waiting for DCAD to close the stalled connection")
        deadline = time.time() + 15.0
        while time.time() < deadline:
            peer.pipe.send({"type": "LinkStatus", "state": "running", "severity": "info", "code": "heartbeat", "message": ""})
            time.sleep(0.5)
        log("DCAD did not close the connection (failure)")
    except OSError as error:
        log("DCAD closed the connection: %s" % error)
    peer.pipe.close()
    time.sleep(1.0)
    log("--- reconnect after the failure")
    peer.connect()
    if peer.advertise_and_bind():
        peer.synchronize()
        peer.stream(2.0)
    peer.disconnect()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pipe", help="pipe path, e.g. \\\\.\\pipe\\DjehutiCAD.Motion.1234.abcdef012345")
    parser.add_argument("--signal", action="append", default=[], help="id:quantity:unit:kind (kind = state|rate)")
    parser.add_argument("--scenario", default="full", choices=["full", "mismatch", "stream", "pause", "drop", "lifecycle", "invalidate", "stall"])
    parser.add_argument("--seconds", type=float, default=4.0)
    parser.add_argument("--no-lifecycle", action="store_true", help="do not advertise lifecycleControl.v1")
    parser.add_argument("--drop-first-result", metavar="COMMAND", help="execute COMMAND once but drop its result, so DCAD times out and retries with the same requestId")
    parser.add_argument("--duplicate-step", action="store_true", help="process every Single Step request twice to demonstrate deduplication")
    parser.add_argument("--stale-after-revalidation", action="store_true", help="after re-accepting a revised profile, first send a SynchronizeState from the previous timeline")
    args = parser.parse_args()
    if not args.signal:
        raise SystemExit("Advertise at least one --signal.")
    peer = Peer(args.pipe or discover_pipe(), [parse_signal(s) for s in args.signal], "peer-sim-1", lifecycle_control=not args.no_lifecycle)
    peer.connect()

    if args.scenario == "mismatch":
        ok = peer.advertise_and_bind()
        log("binding %s" % ("accepted (unexpected)" if ok else "refused, as expected for a mismatched interface"))
        peer.disconnect()
        return
    if not peer.advertise_and_bind():
        peer.disconnect()
        sys.exit(1)
    if args.scenario == "invalidate":
        scenario_invalidate(peer, args)
        return

    peer.first_sync = None
    original = peer.synchronize
    def synchronize_and_remember(*a, **k):
        message = original(*a, **k)
        if peer.first_sync is None:
            peer.first_sync = message
        return message
    peer.synchronize = synchronize_and_remember

    if args.scenario == "full":
        scenario_full(peer, args)
    elif args.scenario == "stall":
        scenario_stall(peer, args)
    elif args.scenario == "lifecycle":
        peer.synchronize(lifecycle="stopped")                   # initialized at t = 0, waiting for Start
        log("serving lifecycle commands for %.0f s" % args.seconds)
        peer.serve(args.seconds, drop_first=args.drop_first_result, duplicate_step=args.duplicate_step,
                   stale_after_revalidation=args.stale_after_revalidation)
        peer.disconnect()
    else:
        peer.synchronize()
        peer.stream(args.seconds)
        if args.scenario == "pause":
            log("--- paused: time frozen, heartbeats continue")
            peer.publish("paused")
            peer.sleep(args.seconds)
            peer.disconnect()
        elif args.scenario == "drop":
            log("--- dropping the connection without goodbye")
            peer.disconnect(goodbye=False)
        else:
            peer.disconnect()


if __name__ == "__main__":
    main()
