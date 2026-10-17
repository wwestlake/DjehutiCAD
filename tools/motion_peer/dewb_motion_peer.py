#!/usr/bin/env python3
"""DEWB-side protocol peer for the DEWB-DCAD motion interface, version 1.

This script stands in for DEWB: it connects to the DCAD named pipe, exchanges interfaces, validates
and accepts DCAD's binding proposal, synchronizes a complete state and streams motion frames computed
exactly as docs/DEWB_DCAD_MOTION_INTERFACE_V1.md specifies. It is an executable example of the wire
protocol, not a simulator: signal values come from simple functions of simulation time.

Usage (Windows, Python 3.6+):
  python dewb_motion_peer.py --signal carriage.position:position:mm:state --signal spindle.speed:angular_velocity:rpm:rate
  python dewb_motion_peer.py --scenario mismatch --signal carriage.position:position:m:state ...

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
        self.file.close()


class Peer:
    def __init__(self, pipe_path, signals, simulation_id):
        self.pipe_path = pipe_path
        self.signals = signals
        self.simulation_id = simulation_id
        self.pipe = None
        self.mappings = []
        self.profile_revision = None
        self.generation = 0
        self.sequence = 0
        self.tick = 0
        self.integrals = {}
        self.last_heartbeat = 0.0
        self.received = []
        self.consumed = 0

    # ---- transport helpers -------------------------------------------------------------
    def pump(self):
        for message in self.pipe.poll():
            self.received.append(message)
            if message.get("type") == "LinkStatus" and message.get("code") == "heartbeat":
                continue
            summary = message.get("type")
            if message.get("type") == "LinkStatus":
                summary += " [%s/%s] %s" % (message.get("severity"), message.get("code"), message.get("message"))
            log("DCAD -> " + summary)
        if time.time() - self.last_heartbeat > 0.5:
            self.last_heartbeat = time.time()
            self.pipe.send({"type": "LinkStatus", "state": "ready", "severity": "info", "code": "heartbeat", "message": ""})

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

    def sleep(self, seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            self.pump()
            time.sleep(0.02)

    # ---- protocol ----------------------------------------------------------------------
    def connect(self):
        self.pipe = Pipe(self.pipe_path)
        self.received = []
        self.consumed = 0
        log("connected to " + self.pipe_path)
        self.pipe.send({"type": "Hello", "protocol": "djehuti.motion", "protocolVersion": 1, "role": "dewb",
                        "application": "dewb_motion_peer", "applicationVersion": "1.0",
                        "capabilities": ["signalInterface.v1", "synchronizeState.v1", "stateFrame.v1", "heartbeat.v1"]})
        self.wait_for(lambda m: m.get("type") == "Hello")
        interface = self.wait_for(lambda m: m.get("type") == "MechanicalInterface")
        log("mechanical interface: model %s, revision %s, %d joints" % (interface["modelId"], interface["interfaceRevision"], len(interface["joints"])))
        self.interface = interface

    def advertise_and_bind(self, expect_accept=True):
        self.pipe.send({"type": "SignalInterface", "simulationId": self.simulation_id, "simulationName": "Protocol peer",
                        "signalInterfaceRevision": 1, "signals": self.signals})
        reply = self.wait_for(lambda m: m.get("type") == "BindingProposal" or (m.get("type") == "LinkStatus" and m.get("severity") == "error"))
        if reply["type"] != "BindingProposal":
            log("DCAD refused to propose: %s" % reply.get("message"))
            for error in reply.get("errors", []):
                log("   %s: %s" % (error.get("code"), error.get("message")))
            return False
        problems = self.validate_proposal(reply)
        if problems:
            self.pipe.send({"type": "BindingReject", "profileRevision": reply["profileRevision"], "errors": problems})
            log("rejected profile: %s" % problems)
            return False
        self.mappings = reply["mappings"]
        self.profile_revision = reply["profileRevision"]
        self.pipe.send({"type": "BindingAccept", "modelId": reply["modelId"], "interfaceRevision": reply["interfaceRevision"],
                        "profileRevision": reply["profileRevision"], "simulationId": self.simulation_id, "signalInterfaceRevision": 1})
        log("accepted profile revision %d with %d mappings" % (self.profile_revision, len(self.mappings)))
        return True

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

    def state(self, kind, lifecycle, joints, generation=None):
        self.sequence += 1
        return {"type": kind, "simulationId": self.simulation_id, "timelineGeneration": generation or self.generation,
                "profileRevision": self.profile_revision, "sequence": str(self.sequence), "simTimePs": str(self.tick),
                "microstep": 0, "lifecycle": lifecycle, "joints": joints}

    def synchronize(self, lifecycle="paused"):
        self.generation += 1
        self.tick = 0
        self.integrals = {}
        message = self.state("SynchronizeState", lifecycle, self.coordinates(0.0, 0.0))
        self.pipe.send(message)
        self.wait_for(lambda m: m.get("type") == "StateApplied" and m.get("sequence") == message["sequence"])
        log("synchronized timeline %d at t=0 (sequence %s)" % (self.generation, message["sequence"]))

    def stream(self, seconds, rate_hz=30.0, speed=1.0, lifecycle="running"):
        frames = int(seconds * rate_hz)
        dt = speed / rate_hz
        for _ in range(frames):
            self.tick += int(round(dt * TICKS_PER_SECOND))
            t = self.tick / TICKS_PER_SECOND
            self.pipe.send(self.state("StateFrame", lifecycle, self.coordinates(t, dt)))
            self.sleep(1.0 / rate_hz)
        log("streamed %d frames to t=%.3f s (%s)" % (frames, self.tick / TICKS_PER_SECOND, lifecycle))

    def disconnect(self, goodbye=True):
        if goodbye:
            self.pipe.send({"type": "LinkStatus", "state": "stopped", "severity": "info", "code": "goodbye", "message": "peer closing"})
            time.sleep(0.2)
        self.pipe.close()
        log("disconnected")


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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pipe", help="pipe path, e.g. \\\\.\\pipe\\DjehutiCAD.Motion.1234.abcdef012345")
    parser.add_argument("--signal", action="append", default=[], help="id:quantity:unit:kind (kind = state|rate)")
    parser.add_argument("--scenario", default="full", choices=["full", "mismatch", "stream", "pause", "drop"],
                        help="full: every step; mismatch: binding refusal; stream: sync and stream; pause: stream then stay paused; drop: stream then vanish without goodbye")
    parser.add_argument("--seconds", type=float, default=4.0)
    args = parser.parse_args()
    if not args.signal:
        raise SystemExit("Advertise at least one --signal.")
    peer = Peer(args.pipe or discover_pipe(), [parse_signal(s) for s in args.signal], "peer-sim-1")
    peer.connect()

    if args.scenario == "mismatch":
        ok = peer.advertise_and_bind()
        log("binding %s" % ("accepted (unexpected)" if ok else "refused, as expected for a mismatched interface"))
        peer.disconnect()
        return

    if not peer.advertise_and_bind():
        peer.disconnect()
        sys.exit(1)
    peer.synchronize()
    peer.stream(args.seconds)
    if args.scenario == "stream":
        peer.disconnect()
        return
    if args.scenario == "pause":
        log("--- paused: time frozen, heartbeats continue")
        peer.pipe.send(peer.state("StateFrame", "paused", peer.coordinates(peer.tick / TICKS_PER_SECOND, 0.0)))
        peer.sleep(args.seconds)
        peer.disconnect()
        return
    if args.scenario == "drop":
        log("--- dropping the connection without goodbye")
        peer.disconnect(goodbye=False)
        return

    log("--- pause: complete states continue with lifecycle=paused, time frozen")
    peer.pipe.send(peer.state("StateFrame", "paused", peer.coordinates(peer.tick / TICKS_PER_SECOND, 0.0)))
    peer.sleep(1.5)
    log("--- single step of 0.25 s")
    peer.stream(0.25, rate_hz=4.0, lifecycle="paused")
    log("--- 4x simulation speed")
    peer.stream(1.0, speed=4.0)

    log("--- invalid frame (missing joint) must be refused and leave the pose unchanged")
    bad = peer.state("StateFrame", "running", peer.coordinates(peer.tick / TICKS_PER_SECOND, 0.0)[1:])
    peer.pipe.send(bad)
    peer.sleep(0.5)

    log("--- reset: new timeline generation starts at t=0")
    old_generation = peer.generation
    peer.synchronize(lifecycle="running")
    log("--- delayed frame from the superseded timeline must be refused")
    peer.pipe.send(peer.state("StateFrame", "running", peer.coordinates(0.0, 0.0), generation=old_generation))
    peer.sleep(0.5)
    peer.stream(2.0)

    log("--- abrupt disconnect (no goodbye): DCAD holds the pose and reports Disconnected after 3 s")
    peer.disconnect(goodbye=False)
    time.sleep(4.0)
    log("--- reconnect: fresh Hello, validation and synchronization")
    peer.connect()
    peer.generation = 0
    peer.sequence = 0
    if peer.advertise_and_bind():
        peer.synchronize(lifecycle="running")
        peer.stream(2.0)
    peer.disconnect()


if __name__ == "__main__":
    main()
