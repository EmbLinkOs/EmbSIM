# Repository workflow and required checks

Work branches start from reviewed `main`. Use the issue forms and the pull
request template. Architecture changes need an ADR; contract changes update
the specification, the requirements and the tests together.

## One pull request per milestone

Each roadmap milestone (docs/architecture/07-roadmap.md) is delivered as one
pull request from its working branch to `main`, opened when the milestone's
work is complete and its local checks pass. The description follows the
pull request template, names the milestone's exit criteria from the roadmap
and states which were met, with the evidence, and which remain open.
STATUS.md is updated in the same pull request. Work after a merged milestone
starts a fresh branch from `main`; a merged pull request is never reused.

## Three stable merge checks

| Required check name | Workflow / job | What it checks |
|---|---|---|
| `CI merge gate` | `ci.yml` / `merge_gate` | every core matrix job (Linux GCC, Clang, TinyCC; macOS Clang; Windows MSVC), the workbench typecheck, lint, build and smoke, the tools' tests and the catalog validation succeeded |
| `Documentation build` | `docs.yml` / `build` | documentation integrity (`tools/docs/check_docs.py`) and schema and catalog validation |
| `Repository policy` | `repository.yml` / `policy` | commit identity and trailer rules (`tools/repository/check_commits.py`), banned constructs and SPDX headers |

The merge gate depends on every matrix job and accepts only `success`;
skipped, cancelled or failed dependencies fail it. `ctest` runs with
`--no-tests=error` so an empty test registration cannot pass. Official
actions are pinned to full commit SHAs with version comments; third-party
actions used for toolchains are pinned to tags and listed in `ci.yml`'s
header comment until a reviewed SHA is recorded.

## Commit identity

Every non-merge commit must be authored by the repository owner's Git
identity and carry no `Co-authored-by`, `Signed-off-by` for another person,
or assistant-attribution trailer or text (ADR-014). The checker parses the
trailer block with `git interpret-trailers`. Locally:

```sh
python3 -m unittest discover -s tools/repository -p 'test_*.py' -v
python3 tools/repository/check_commits.py --base "$(git rev-parse main)" --head HEAD
```

When maintainers join, `OWNER_EMAILS` in the checker becomes the list of
maintainers and a DCO-style sign-off may replace it; that change is an ADR.

## Main branch protection

`.github/rulesets/main.json` is the reviewed policy: pull requests required,
review threads resolved, the three checks required with a branch up to
date, deletion and non-fast-forward pushes blocked, no bypass actors, zero
required approvals while CODEOWNERS has one maintainer. An administrator
imports it under Settings → Rules → Rulesets → New ruleset → Import after the
workflows have run once on a pull request, verifies the check names, and
opens a test pull request to confirm the merge box requires them. A checked-in
JSON file activates nothing; record the activation in STATUS.md when it is
observed.

## Before merging

Confirm the three checks ran on the current revision, read any skips, resolve
feedback, update the Unreleased changelog for user-visible changes, and
update STATUS.md when what is implemented or verified changed.
