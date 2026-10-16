# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""tools/verify/compare_reports.py on made-up reports."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("compare_reports", HERE / "verify" / "compare_reports.py")
cr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cr)

BASE = {
    "engine": "0.1.0", "ended": {"reason": "until", "now_ps": "2000"},
    "assertions": [{"id": "banner", "ok": True, "observed": "hello"}, {"id": "led", "ok": True, "observed": "1"}],
    "diagnostics": [{"code": "mcu.trap"}],
    "statistics": {"events": 10, "wall_time_s": 0.5},
}


class CompareReportsTest(unittest.TestCase):
    def test_same_but_wall_time(self):
        b = json.loads(json.dumps(BASE))
        b["statistics"]["wall_time_s"] = 9.0
        b["engine"] = "0.2.0"
        self.assertEqual(cr.compare(BASE, b), [])

    def test_differences(self):
        b = json.loads(json.dumps(BASE))
        b["assertions"][1] = {"id": "led", "ok": False, "observed": "0"}
        b["assertions"][0]["observed"] = "hello!"
        b["diagnostics"].append({"code": "mcu.trap"})
        b["statistics"]["events"] = 11
        b["ended"]["now_ps"] = "2001"
        what = [d["what"] for d in cr.compare(BASE, b)]
        self.assertEqual(what, ["ended.now_ps", "assertion banner observed", "assertion led",
                                "diagnostic mcu.trap", "statistics.events"])

    def test_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp) / "a.json", Path(tmp) / "b.json"
            a.write_text("embsim: info\n" + json.dumps(BASE))
            b.write_text(json.dumps(BASE))
            self.assertEqual(cr.main([str(a), str(b)]), 0)
            c = json.loads(json.dumps(BASE))
            c["assertions"].pop()
            b.write_text(json.dumps(c))
            self.assertEqual(cr.main([str(a), str(b), "--json"]), 1)
            self.assertEqual(cr.main([str(a), str(Path(tmp) / "missing.json")]), 2)


if __name__ == "__main__":
    unittest.main()
