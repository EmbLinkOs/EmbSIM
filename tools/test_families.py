# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""tools/catalog/families.py: placeholders, numbers as datasheets print them, and the families' output."""
import json
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "catalog"))
import families  # noqa: E402


class Placeholders(unittest.TestCase):
    def test_whole_string_keeps_the_type(self):
        self.assertEqual(families.subst({"a": "${v}", "b": ["${v}"]}, {"v": 5.1}, "t"), {"a": 5.1, "b": [5.1]})

    def test_inside_a_string_is_text(self):
        self.assertEqual(families.subst("BZX55 ${v} V", {"v": 5.0}, "t"), "BZX55 5 V")
        self.assertEqual(families.subst("${a}/${b}", {"a": "x", "b": 0.005}, "t"), "x/0.005")

    def test_unknown_variable_fails(self):
        with self.assertRaises(SystemExit):
            families.subst("${nope}", {}, "t")


class Evidence(unittest.TestCase):
    def match(self, value, text):
        return re.search("^" + families.pattern_of(value) + "$", text) is not None

    def test_numbers_match_their_printed_forms(self):
        self.assertTrue(self.match(3, "3"))
        self.assertTrue(self.match(3, "3.0"))
        self.assertTrue(self.match(3.0, "3.00"))
        self.assertTrue(self.match(0.45, "0.450"))
        self.assertTrue(self.match(4, "4"))

    def test_numbers_do_not_match_others(self):
        self.assertFalse(self.match(3, "33"))
        self.assertFalse(self.match(3, "3.1"))
        self.assertFalse(self.match(0.45, "0.451"))
        self.assertIsNone(re.search(families.pattern_of(3), "3.5"))

    def test_text_is_literal(self):
        self.assertTrue(self.match("1N4148", "1N4148"))
        self.assertFalse(self.match("a.b", "axb"))


class Output(unittest.TestCase):
    def test_every_family_renders_ids_its_rows_name(self):
        for name, fam in families.families():
            files = families.render(name, fam)
            ids = {json.loads(t)["id"] for p, t in files.items() if p.endswith("component.json")}
            self.assertEqual(ids, {"emblink/" + r["short"] for r in fam["rows"]}, name)
            for p, t in files.items():
                self.assertNotIn("${", t, f"{p}: a placeholder left")

    def test_french_fingerprint_is_current(self):
        for name, fam in families.families():
            for p, t in families.render(name, fam).items():
                if p.endswith("component.json"):
                    d = json.loads(t)
                    if "i18n" in d:
                        en = families.catalog_text.english({k: v for k, v in d.items() if k != "i18n"})
                        self.assertEqual(d["i18n"]["fr"]["source"], families.catalog_text.fingerprint(en), p)


if __name__ == "__main__":
    unittest.main()
