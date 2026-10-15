"""Loopback-only development server. Not a public deployment server."""
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from .kernel import ROOT, MAX_TIME, timestamp
from .project import Project, fields


class Session:
    def __init__(self, data):
        self.lock = threading.Lock()
        self.data = data
        self.project = Project(data)
        self.running = False

    def state(self):
        return dict(self.project.snapshot(), running=self.running)

    def control(self, command):
        if not isinstance(command, dict):
            raise ValueError("command must be an object")
        action = command.get("action")
        if action == "set":
            fields(command, ("action", "component", "pressed"))
            self.project.set_button(command["component"], command["pressed"])
            self.project.advance(self.project.kernel.now)
        elif action == "advance":
            fields(command, ("action", "until_ns"))
            self.project.advance(timestamp(command["until_ns"]))
        elif action in ("run", "pause", "reset"):
            fields(command, ("action",))
            if action == "reset":
                new = Project(self.data)
                self.project.close()
                self.project = new
                self.running = False
            else:
                self.running = action == "run"
        else:
            raise ValueError("unknown control action")
        return self.state()


def make_server(data, port=8765):
    session = Session(data)

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass

        def reply(self, status, data, mime="application/json"):
            body = json.dumps(data).encode() if mime == "application/json" else data
            self.send_response(status)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Content-Security-Policy", "default-src 'self'; style-src 'self'; script-src 'self'; frame-ancestors 'none'")
            self.end_headers()
            self.wfile.write(body)

        def permitted(self):
            port = self.server.server_port
            hosts = (f"127.0.0.1:{port}", f"localhost:{port}")
            host = self.headers.get("Host", "")
            origin = self.headers.get("Origin")
            if host not in hosts or (origin and origin != "http://" + host):
                self.reply(403, {"error": "only same-origin loopback requests are accepted"})
                return False
            return True

        def do_GET(self):
            if not self.permitted():
                return
            if self.path == "/api/state":
                with session.lock:
                    self.reply(200, session.state())
                return
            assets = {"/": ("index.html", "text/html; charset=utf-8"),
                      "/app.js": ("app.js", "text/javascript; charset=utf-8"),
                      "/style.css": ("style.css", "text/css; charset=utf-8")}
            asset = assets.get(self.path)
            if asset is None:
                self.reply(404, {"error": "not found"})
                return
            self.reply(200, (ROOT / "web" / asset[0]).read_bytes(), asset[1])

        def do_POST(self):
            if not self.permitted():
                return
            if self.path != "/api/control":
                self.reply(404, {"error": "not found"})
                return
            try:
                if self.headers.get("Content-Type") != "application/json":
                    raise ValueError("Content-Type must be application/json")
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 65536:
                    raise ValueError("body must contain 1–65536 bytes")
                command = json.loads(self.rfile.read(length))
                with session.lock:
                    state = session.control(command)
                self.reply(200, state)
            except (ValueError, TypeError, UnicodeError) as error:
                self.reply(400, {"error": str(error)})

    try:
        server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    except Exception:
        session.project.close()
        raise
    server.session = session
    return server


def serve(data, port):
    server = make_server(data, port)
    stopped = threading.Event()

    def tick():
        while not stopped.wait(0.02):
            with server.session.lock:
                session = server.session
                if session.running:
                    until = min(MAX_TIME, session.project.kernel.now + 20_000_000)
                    session.project.advance(until)
                    if until == MAX_TIME:
                        session.running = False

    ticker = threading.Thread(target=tick, daemon=True)
    ticker.start()
    print(f"EmbSIM development UI listening on loopback port {server.server_port}; Ctrl-C stops it.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stopped.set()
        ticker.join()
        server.server_close()
        server.session.project.close()
