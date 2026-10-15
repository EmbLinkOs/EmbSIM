# Project instructions

- Develop EmbSIM against `docs/SPECIFICATION.md` and `docs/ARCHITECTURE.md`.
  Keep implemented features distinct from roadmap items.
- Linux, macOS and Windows are required targets. Do not introduce unconditional
  POSIX dependencies into the kernel or shared clients.
- GUI and CLI share simulation behavior. UI quality, responsiveness, keyboard
  access and readable diagnostics are product requirements.
- Run `python scripts/build.py --test` for kernel/runtime changes. Use the optional
  browser smoke check for interaction changes when a browser is available.
- Document each model's fidelity and limitations; register metadata alone is not
  a behavioral model. Preserve source licenses/provenance during legacy migration.
- Commits and PRs use the owner's configured identity. No co-author trailers or
  assistant attribution. Do not change Git identity to an invented author.
- Use the existing isolated checkout. Do not create worktrees unless requested.
