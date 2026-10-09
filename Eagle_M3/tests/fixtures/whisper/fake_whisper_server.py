#!/usr/bin/env python3
"""Stand-in for whisper.cpp `whisper-server`, used by tst_whisperserverprocess.

Accepts the same flags Eagle passes (-m, --host, --port, -t, -l) and serves
GET /health and POST /inference. Behaviour switches (environment variables):
  FAKE_LOAD_MS        delay before listening (model load time), default 300
  FAKE_CRASH_AFTER_MS exit with code 3 this long after becoming ready
  FAKE_BAD_ARG        print "error: unknown argument" and exit 0 (like a flag
                      the bundled whisper-server does not know)
"""
import json
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def arg(name, default=None):
    if name in sys.argv:
        i = sys.argv.index(name)
        if i + 1 < len(sys.argv):
            return sys.argv[i + 1]
    return default


class Handler(BaseHTTPRequestHandler):
    def _send(self, status, body):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/health":
            self._send(200, {"status": "ok"})
        else:
            self._send(404, {"error": "not found"})

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        self._send(200, {"text": " fake transcript"})

    def log_message(self, *args):
        pass


def main():
    if os.environ.get("FAKE_BAD_ARG"):
        print("error: unknown argument: --bogus", flush=True)
        return 0
    model = arg("-m")
    if not model or not os.path.exists(model):
        print("error: failed to initialize whisper context", flush=True)
        return 1
    time.sleep(int(os.environ.get("FAKE_LOAD_MS", "300")) / 1000.0)
    host = arg("--host", "127.0.0.1")
    port = int(arg("--port", "8080"))
    try:
        server = ThreadingHTTPServer((host, port), Handler)
    except OSError:
        print(f"couldn't bind to server socket: hostname={host} port={port}", flush=True)
        return 1
    print(f"whisper server listening at http://{host}:{port} lang={arg('-l')} threads={arg('-t')}",
          flush=True)
    crash_after = os.environ.get("FAKE_CRASH_AFTER_MS")
    if crash_after:
        threading.Timer(int(crash_after) / 1000.0, lambda: os._exit(3)).start()
    server.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
