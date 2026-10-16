# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
import unittest
from check_source_paths import collisions


class SourcePathTests(unittest.TestCase):
    def test_case_insensitive_extensionless_resolution(self):
        self.assertEqual(
            collisions(["ui/SelectionProperties.tsx", "ui/selectionProperties.ts"]),
            [["ui/SelectionProperties.tsx", "ui/selectionProperties.ts"]],
        )

    def test_same_stem_with_different_source_extensions(self):
        self.assertEqual(
            collisions(["ui/panel.ts", "ui/panel.tsx"]),
            [["ui/panel.ts", "ui/panel.tsx"]],
        )

    def test_distinct_directories_and_declarations(self):
        self.assertEqual(
            collisions(["ui/a/panel.tsx", "ui/b/panel.tsx", "ui/a/panel.d.ts"]), []
        )

    def test_assets_are_not_module_candidates(self):
        self.assertEqual(
            collisions(["ui/panel.svg", "ui/panel.css", "ui/panel.tsx"]), []
        )


if __name__ == "__main__":
    unittest.main()
