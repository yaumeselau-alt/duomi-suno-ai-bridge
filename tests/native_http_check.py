"""Verify the native JUCE HTTP wire protocol against a loopback-only server.

No real API credentials or external requests. Run from the repository root:
  python tests/native_http_check.py build/src/Debug/DuomiBridgeCheck.exe
"""
import json
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import subprocess
import sys
from threading import Thread
from urllib.parse import parse_qs, urlsplit

MIDI_ID = "11111111-2222-3333-4444-555555555555"
requests = []
failures = []


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_POST(self):
        self.handle_request()

    def do_GET(self):
        self.handle_request()

    def handle_request(self):
        url = urlsplit(self.path)
        query = parse_qs(url.query)
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        requests.append((self.command, url.path))
        try:
            if url.path.endswith("/generate"):
                assert self.command == "POST"
                assert self.headers["Authorization"] == "loopback-test-key"
                assert json.loads(body)["prompt"] == "piano"
                data = [{"task_id": "local-song"}]
            elif url.path.endswith("/midi"):
                assert self.command == "GET"
                assert query == {"key": ["loopback-test-key"]}
                assert self.headers["Content-Type"].startswith("application/json")
                assert json.loads(body) == {"task_id": "local-song"}
                data = MIDI_ID
            elif url.path.endswith("/feed"):
                assert self.command == "GET" and not body
                assert self.headers["Authorization"] == "loopback-test-key"
                if query["task_id"] == ["local-song"]:
                    data = {"state": "succeeded", "audio_url": "https://example.invalid/audio.mp3"}
                else:
                    assert query["task_id"] == [MIDI_ID]
                    data = {"state": "succeeded", "action": "midi", "data": {
                        "state": "complete", "instruments": [{
                            "name": "Piano", "program": 0, "is_drum": False,
                            "notes": [{"pitch": 60, "start": 0.125, "end": 1.5, "velocity": 0.5}]
                        }]
                    }}
            else:
                raise AssertionError("Unexpected endpoint")
            response = {"code": 200, "data": data}
        except Exception as error:
            failures.append(f"{self.command} {url.path}: {type(error).__name__}")
            response = {"code": 400, "msg": "Loopback wire protocol mismatch"}
        encoded = json.dumps(response).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)


def main():
    executable = Path(sys.argv[1]).resolve(strict=True)
    server = HTTPServer(("127.0.0.1", 0), Handler)
    worker = Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        result = subprocess.run(
            [str(executable), "--native-http-check", str(server.server_port)],
            capture_output=True, timeout=45,
        )
        print(result.stdout.decode("utf-8", errors="backslashreplace").encode("ascii", errors="backslashreplace").decode())
        if result.returncode:
            print("Client error:", repr(result.stderr))
            print("Received requests:", requests)
        expected = [("POST", "/api/suno/generate"), ("GET", "/api/suno/feed"),
                    ("GET", "/api/suno/midi"), ("GET", "/api/suno/feed")]
        assert not failures, failures
        assert result.returncode == 0, result.returncode
        assert requests == expected, requests
        print("PASS four loopback requests; no external service or real key used")
    finally:
        server.shutdown()
        server.server_close()
        worker.join()


if __name__ == "__main__":
    main()
