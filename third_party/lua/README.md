# Lua 5.4.7 (vendored)

The interpreter behind scripted component models (SPEC-013, ADR-009).

- Source: https://github.com/lua/lua, tag `v5.4.7`
  (commit 1ab3208a1fceb12fca8f24ba57d6e13c5bff15e3), the Lua team's own
  repository. Licence: MIT, in `LICENSE`.
- Kept: the core, the auxiliary library and the base, string, table, math
  and utf8 libraries.
- Left out: the stand-alone interpreter and compiler (`lua.c`, `luac.c`),
  the test library (`ltests.*`, `onelua.c`), the io, os, debug, package and
  coroutine libraries and `linit.c` (the host opens the libraries it allows).
- Changed: the "Local configuration" section at the end of
  `src/luaconf.h` fixes the string-hash seed and the `table.sort` pivot, so
  that nothing reads the host clock or depends on heap addresses (a session
  replays identically). Nothing else is modified.

To update: copy the same file set from a new release tag, re-apply the
local configuration block and run the Lua model tests (`ctest -R lua`).
