"""Print a compact summary of the running CAD's UI state (no token output)."""
from client import Client
import json
import sys

c = Client()
state = c.ui()["state"]
summary = {k: state.get(k) for k in ("documentRevision", "modified", "mode", "task", "message", "selected", "editingSketch", "applyLabel", "canApply")}
summary["fields"] = [{k: f.get(k) for k in ("id", "type", "value", "enabled")} for f in state.get("fields") or []]
summary["features"] = [{k: f.get(k) for k in ("id", "name", "type", "visible")} for f in state["features"]]
summary["camera"] = state.get("camera")
summary["gesture"] = state.get("gesture")
print(json.dumps(summary, ensure_ascii=True, indent=1 if "-v" in sys.argv else None))
