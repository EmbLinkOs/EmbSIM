import http.client
import json
import threading
import unittest

from embsim.kernel import ROOT
from embsim.project import load
from embsim.server import make_server


class ServerTests(unittest.TestCase):
    def setUp(self):
        self.server = make_server(load(ROOT / "examples/button-led.json"), 0)
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.addCleanup(self.cleanup)

    def cleanup(self):
        self.server.shutdown()
        self.thread.join()
        self.server.server_close()
        self.server.session.project.close()

    def request(self, method, path, body=None, headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)
        try:
            connection.request(method, path, json.dumps(body) if body is not None else None,
                               headers or {"Content-Type": "application/json"})
            response = connection.getresponse()
            return response.status, response.read()
        finally:
            connection.close()

    def control(self, command):
        status, body = self.request("POST", "/api/control", command)
        self.assertEqual(status, 200, body)
        return json.loads(body)

    def test_button_controls_run_pause_and_reset(self):
        state = self.control({"action": "set", "component": "SW1", "pressed": True})
        self.assertTrue(state["components"]["LED1"]["lit"])
        self.assertTrue(self.control({"action": "run"})["running"])
        self.assertFalse(self.control({"action": "pause"})["running"])
        state = self.control({"action": "advance", "until_ns": 30_000_000})
        self.assertFalse(state["components"]["LED1"]["lit"])
        state = self.control({"action": "reset"})
        self.assertEqual(state["time_ns"], 0)
        self.assertFalse(state["components"]["LED1"]["lit"])
        self.assertEqual(len(state["inputs"]), 2)

    def test_assets_and_rejected_controls(self):
        for path in ("/", "/app.js", "/style.css", "/api/state"):
            self.assertEqual(self.request("GET", path)[0], 200)
        self.assertEqual(self.request("GET", "/../LICENSE")[0], 404)
        for command in ({"action": "unknown"}, {"action": "advance", "until_ns": -1},
                        {"action": "set", "component": "missing", "pressed": True},
                        {"action": "run", "ignored": True}):
            self.assertEqual(self.request("POST", "/api/control", command)[0], 400)
        self.assertEqual(self.request("GET", "/api/state", headers={"Host": "attacker.example"})[0], 403)
        self.assertEqual(self.request("POST", "/api/control", {"action": "run"},
                                     {"Content-Type": "application/json", "Origin": "https://attacker.example"})[0], 403)
