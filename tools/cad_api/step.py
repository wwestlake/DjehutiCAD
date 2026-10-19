"""Perform ONE cad_ui step and print a compact result, e.g.
   python step.py command command=rectangle
   python step.py sketch phase=update x=30 y=20
Values are parsed as JSON when possible (numbers, true/false), otherwise kept as text."""
from client import Client
import json
import sys

args = {}
for item in sys.argv[2:]:
    key, _, value = item.partition("=")
    try:
        args[key] = json.loads(value)
    except ValueError:
        args[key] = value
result = Client().call("cad_ui", {"action": sys.argv[1] if len(sys.argv) > 1 else "state", **args}, allow_failure=True)
state = result.get("state", {})
print(json.dumps({"ok": result.get("ok"), "error": result.get("error"), "revision": state.get("documentRevision"), "mode": state.get("mode"),
                  "task": state.get("task"), "message": state.get("message"), "selected": state.get("selected"), "gesture": state.get("gesture"),
                  "features": [(f["id"], f["type"], len(f.get("entities", []))) for f in state.get("features", [])],
                  "camera": {k: round(v, 2) for k, v in (state.get("camera") or {}).items() if isinstance(v, float)}}, ensure_ascii=True))
