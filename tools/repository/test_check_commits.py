# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_commits  # noqa: E402


def run(cwd, *args, env=None):
    return subprocess.check_output(["git", *args], cwd=cwd, text=True, env=env).strip()


class CheckCommitsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = self.tmp.name
        run(self.repo, "init", "-q")
        run(self.repo, "config", "user.name", "Owner")
        run(self.repo, "config", "user.email", "motsoujunior17@gmail.com")
        self.old = os.getcwd()
        os.chdir(self.repo)

    def tearDown(self):
        os.chdir(self.old)
        self.tmp.cleanup()

    def commit(self, message, email=None):
        Path(self.repo, "f").write_text(message)
        run(self.repo, "add", "f")
        env = dict(os.environ)
        if email:
            env["GIT_AUTHOR_EMAIL"] = email
        run(self.repo, "commit", "-q", "-m", message, env=env)
        return run(self.repo, "rev-parse", "HEAD")

    def test_owner_commit_passes(self):
        sha = self.commit("core: add scheduler")
        self.assertEqual(check_commits.check_commit(sha), [])

    def test_foreign_author_fails(self):
        sha = self.commit("x", email="someone@example.com")
        self.assertTrue(any("not the repository owner" in p for p in check_commits.check_commit(sha)))

    def test_coauthor_trailer_fails(self):
        sha = self.commit("x\n\nCo-authored-by: Bot <bot@example.com>")
        self.assertTrue(any("forbidden trailer" in p for p in check_commits.check_commit(sha)))

    def test_assistant_text_fails(self):
        sha = self.commit("x\n\nGenerated with [Claude Code](https://claude.ai/code)")
        self.assertTrue(any("attribution" in p for p in check_commits.check_commit(sha)))


if __name__ == "__main__":
    unittest.main()
