"""Focused completion-semantics check in a running, EMPTY CAD document.

A deliberately slow export keeps the single worker busy, so a second request stays queued and can be cancelled.
Checks 202 (accepted, not finished), cancel of queued work, refusal to cancel finished work, 200 completion with
the real result, and that the document is unchanged by export. Leaves a new empty document.
"""
from client import Client
import json
from pathlib import Path
import subprocess
import tempfile
import time
import uuid


def main():
    c = Client()
    checks = []

    def check(condition, label, detail=""):
        if not condition:
            raise AssertionError(label + (": " + str(detail) if detail else ""))
        checks.append(label)

    state = c.ui()["state"]
    if state["features"] or state["modified"]:
        raise RuntimeError("Use a new empty document; this check preserves existing work.")
    for i in range(2):   # large radius at 0.001 mm chordal: several seconds of meshing in Debug
        body = c.call("cad_create_cylinder", {"radius": 400, "height": 50})["id"]
        c.call("cad_set_placement", {"id": body, "x": i * 900, "y": 0, "z": 0})
    revision = c.ui()["state"]["documentRevision"]
    out = Path(tempfile.mkdtemp(prefix="dcad-api-semantics-")) / "slow.3mf"

    slow, queued = str(uuid.uuid4()), str(uuid.uuid4())
    code, reply = c.request("/v1/tools/call", {"name": "cad_export", "requestId": slow, "arguments": {
        "format": "3mf", "path": str(out), "quality": "custom", "chordal_mm": 0.001, "angular_deg": 0.5}})
    check(code == 202 and reply["status"] in ("queued", "running"), "slow export answers 202 before it finishes", (code, reply.get("status")))
    code, reply = c.request("/v1/tools/call", {"name": "cad_ui", "requestId": queued, "arguments": {"action": "state"}})
    check(code == 202 and reply["status"] == "queued", "next request waits in order behind it", (code, reply.get("status")))
    code, reply = c.request("/v1/requests/" + queued + "/cancel", {})
    check(code == 200 and reply["status"] == "cancelled" and reply["result"]["cancelled"], "queued request can be cancelled", (code, reply))
    code, reply = c.request("/v1/requests/" + slow)
    check(code == 202 and reply["status"] == "running", "status query reports the export still running", (code, reply.get("status")))
    responding = subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process djehuti_cad | Select-Object -First 1).Responding"],
                                stdout=subprocess.PIPE, universal_newlines=True).stdout.strip()
    check(responding == "True", "UI thread keeps responding while the export runs on the worker", responding)
    code, reply = c.request("/v1/requests/" + slow + "/cancel", {})
    check(code == 409 and reply["status"] == "running", "running request is not falsely cancelled", (code, reply))
    started = time.monotonic()
    while True:
        code, reply = c.request("/v1/requests/" + slow)
        if code != 202:
            break
        time.sleep(0.25)
    result = reply["result"]
    check(code == 200 and reply["status"] == "completed" and result["ok"] and out.exists(), "completion reports the real export result", (code, result.get("error")))
    check(result["parts"][0]["triangles"] > 1000, "result carries the actual mesh", result["parts"][0]["triangles"])
    code, reply = c.request("/v1/requests/" + queued)
    check(reply["status"] == "cancelled", "cancelled request stays cancelled (never ran)")
    check(c.ui()["state"]["documentRevision"] == revision, "export left the document revision unchanged")
    c.ui("document", operation="new", discard=True)
    print(json.dumps({"passed": len(checks), "checks": checks, "exportWaitSeconds": round(time.monotonic() - started, 2),
                      "triangles": result["parts"][0]["triangles"]}, indent=2))


if __name__ == "__main__":
    main()
