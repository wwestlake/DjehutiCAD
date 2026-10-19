"""One-step CAD automation client. Uses only Python's standard library.

Import Client to call tools; or run client.py cad_ui --args '{...}'.
The client never prints the discovery token and never retries a mutation blindly.
"""
import argparse
import json
import os
from pathlib import Path
import time
import urllib.error
import urllib.request
import uuid


class Client:
    def __init__(self, discovery=None, timeout=120):
        self.discovery = Path(discovery or Path(os.environ["APPDATA"]) / "DjehutiCAD" / "agent-api.json")
        config = json.loads(self.discovery.read_text(encoding="utf-8-sig"))
        self.base = config["baseUrl"]
        if not self.base.startswith("http://127.0.0.1:"):
            raise ValueError("Expected CAD's loopback endpoint")
        self.token = config["token"]
        self.timeout = timeout
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def request(self, path, data=None, token=None):
        body = None if data is None else json.dumps(data, ensure_ascii=False).encode("utf-8")
        req = urllib.request.Request(self.base + path, data=body, headers={
            "Authorization": "Bearer " + (self.token if token is None else token),
            "Content-Type": "application/json; charset=utf-8",
        })
        try:
            with self.opener.open(req, timeout=5) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as response:
            return response.code, json.load(response)

    def call(self, name, arguments=None, request_id=None, allow_failure=False):
        request_id = request_id or str(uuid.uuid4())
        try:
            code, reply = self.request("/v1/tools/call", {"name": name, "arguments": arguments or {}, "requestId": request_id})
        except (OSError, TimeoutError) as error:
            raise RuntimeError(f"Transport interrupted; inspect /v1/requests/{request_id} before resubmitting") from error
        deadline = time.monotonic() + self.timeout
        while code == 202:
            if time.monotonic() >= deadline:
                raise TimeoutError(f"Command may still be running. Inspect /v1/requests/{request_id}; do not replay it.")
            time.sleep(0.15)
            code, reply = self.request("/v1/requests/" + request_id)
        result = reply.get("result", reply)
        if not result.get("ok", False) and not allow_failure:
            raise RuntimeError(result.get("error", f"Command failed with HTTP {code}"))
        return result

    def ui(self, action="state", **kwargs):
        return self.call("cad_ui", {"action": action, **kwargs})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tool", nargs="?", default="cad_ui")
    parser.add_argument("--args", default="{}", help="JSON arguments")
    parser.add_argument("--args-file", type=Path, help="Read arguments from a UTF-8 JSON file")
    parser.add_argument("--discovery", type=Path)
    parser.add_argument("--sequence", type=Path, help="JSON array of {name, arguments, caption} steps; waits for Enter before EVERY step")
    args = parser.parse_args()
    client = Client(args.discovery)
    if args.sequence:
        for step in json.loads(args.sequence.read_text(encoding="utf-8-sig")):
            input(step.get("caption", step["name"]) + " — press Enter to perform this step: ")
            print(json.dumps(client.call(step["name"], step.get("arguments", {})), ensure_ascii=False, indent=2))
    else:
        arguments = json.loads(args.args_file.read_text(encoding="utf-8-sig") if args.args_file else args.args)
        print(json.dumps(client.call(args.tool, arguments), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
