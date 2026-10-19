"""Focused API smoke check in a running, EMPTY CAD document. No regression suite.
Creates a small sketch/extrude/cut through visible task controls, saves a temporary
native document, checks reopen and undo, then leaves a new empty document.
Aborts without modifying a populated or unsaved document.
"""
from client import Client
import json
from pathlib import Path
import tempfile
import time
import uuid


def main():
    started = time.monotonic()
    c = Client()
    checks = []

    def check(condition, label):
        if not condition:
            raise AssertionError(label)
        checks.append(label)

    state = c.ui()["state"]
    if state["features"] or state["modified"]:
        raise RuntimeError("Use a new empty document; this check preserves existing work.")
    check(c.request("/v1/status")[1]["status"] == "ready", "authenticated discovery/status")
    check(c.request("/v1/status", token="invalid")[0] == 401, "reject wrong token")
    catalogue = c.request("/v1/tools")[1]
    check(any(t["name"] == "cad_ui" for t in catalogue), "live tool catalogue includes UI controls")
    check(c.request("/v1/tools/call", {"name": "not_a_tool"})[0] == 400, "reject unknown tools")
    c.call("cad_workspace", {"action": "activate", "workspace": "cad"})
    c.ui("command", command="sketch")
    c.ui("click", button="planeXY")
    state = c.ui("click", button="apply")["state"]
    sketch = state["editingSketch"]
    check(state["mode"] == "sketch" and bool(sketch) and state["showing"], "Create Sketch enters visible sketch editing")
    c.ui("command", command="rectangle")
    c.ui("sketch", phase="begin", x=-30, y=-20)
    state = c.ui("sketch", phase="update", x=30, y=20)["state"]
    check(state["gesture"]["active"] and not state["features"][0]["entities"], "rectangle preview held without committing geometry")
    state = c.ui("sketch", phase="commit", x=30, y=20)["state"]
    check(len(state["features"][0]["entities"]) == 4, "gesture commits four rectangle edges")
    c.ui("command", command="finishSketch")
    c.ui("command", command="extrude")
    c.ui("set", field="distance", value=8)
    state = c.ui("click", button="apply")["state"]
    body = state["selected"]
    check(any(f["id"] == body and f["type"] == "extrude" for f in state["features"]), "task Apply extrudes the drawn sketch")
    revision = state["documentRevision"]
    c.ui("camera", operation="fit")
    state = c.ui("camera", yawDeg=60, pitchDeg=30)["state"]
    check(state["documentRevision"] == revision and abs(state["camera"]["yawDeg"] - 60) < 0.001, "camera changes without document edits")
    c.ui("set", field="name", value="API plate – Δ")
    state = c.ui("click", button="apply")["state"]
    check(any(f["name"] == "API plate – Δ" for f in state["features"]), "UTF-8 byte framing and visible field edit")
    c.ui("command", command="sketch")
    c.ui("click", button="planeXY")
    c.ui("click", button="apply")
    c.ui("command", command="circle")
    c.ui("sketch", phase="begin", x=0, y=0)
    c.ui("sketch", phase="commit", x=4, y=0)
    c.ui("command", command="finishSketch")
    c.ui("command", command="cut")
    c.ui("select", id=body)
    c.ui("set", field="throughAll", value=True)
    state = c.ui("click", button="apply")["state"]
    check(any(f["type"] == "cut" and f["visible"] for f in state["features"]), "task Cut applies circle profile to selected target")
    before = len(state["features"])
    rid = str(uuid.uuid4())
    first = c.call("cad_undo", request_id=rid)
    second = c.call("cad_undo", request_id=rid)
    check(first == second and len(c.ui()["state"]["features"]) == before - 1, "same request id does not repeat undo")
    c.call("cad_redo")
    result = c.call("cad_create_box", {"width": -1, "depth": 10, "height": 10}, allow_failure=True)
    check(not result["ok"] and result.get("error"), "direct tool returns actual modeling failure")
    result = c.call("cad_ui", {"action": "set", "field": "doesNotExist", "value": 1}, allow_failure=True)
    check(not result["ok"], "reject invalid UI field")
    result = c.call("cad_ui", {"action": "document", "operation": "new"}, allow_failure=True)
    check(not result["ok"], "unsaved document protected against accidental New")
    path = Path(tempfile.mkdtemp(prefix="dcad-api-check-")) / "plate.djo"
    c.ui("document", operation="save", path=str(path))
    c.ui("document", operation="new")
    state = c.ui("document", operation="open", path=str(path))["state"]
    check(any(f["type"] == "cut" and f["visible"] for f in state["features"]), "native save and reopen")
    c.ui("document", operation="new")
    c.ui("camera", operation="reset")
    check(not c.ui()["state"]["features"], "leaves clean empty workspace for Bill's demonstration")
    print(json.dumps({"passed": len(checks), "checks": checks, "seconds": round(time.monotonic() - started, 2), "savedCheckDocument": str(path)}, indent=2))


if __name__ == "__main__":
    main()
