# Pinned library inputs

A library lock is an opt-in baseline for the **effective loaded catalog** and
all non-hidden regular files under every loaded library root. It makes an
unchanged version label insufficient to hide changes to models, symbols,
register data, licences or fixtures. The native loader checks the baseline before
instantiating boards, components or firmware. Existing unpinned projects retain
their behavior.

## Graphical workflow

Open **Engineering reports → Library reproducibility → Review current libraries**.
Review the library names, versions, file counts and differences from an existing
baseline. Search file paths/hashes in **Review file identities** or export the full
candidate with **Export reviewed lock** before changing project metadata. **Pin reviewed library inputs** or **Replace saved library lock** applies
one validated metadata transaction. **Check saved lock** rereads the inputs;
**Remove library lock** is an explicit edit. Pinning/removal are undoable and do
not restart the circuit, change observations or advance time. A failed pin keeps
the previous project and clock. Save the project normally to persist the edit.

Review never changes the baseline automatically. If a resource changes between
review and Apply, native commit validation refuses the candidate. After a process
restart, an on-disk locked project with drift cannot run; use the CLI review
below to inspect or capture an intentional replacement. A changed loaded
component description or library name/version requires a fresh session before
capture, so an old in-memory description cannot bless new disk metadata.

The demo has no filesystem and cannot create a simulated successful lock.

## Command line and format

```sh
embsim lock capture path/to/project.json > reviewed-library-lock.json
embsim lock check path/to/project.json
embsim run path/to/project.json --until 1ms --json
```

Capture emits a `library_lock` object; it never edits a file. After review, attach
that object as the project's `library_lock` member. Check emits `{ok, changes}`:
exit 0 matches, 1 differs or has no baseline, 2 is usage/read/load failure.
The read-only review commands load a temporary copy without its saved lock so
that drift can be inspected; ordinary run/serve/sidecar/project validation retain
enforcement. Both review commands accept `--library DIR`.

`project.library_lock {op:"capture"}` returns the same baseline over RPC.
`{op:"check", lock?}` checks an explicit candidate or the saved project baseline.
Changes name the scope, path and `added`, `removed`, `changed` or `invalid` state.
`set_library_lock: object|null` is a validated metadata transaction edit.

Version-1 locks contain:

- `libraries`: unique name → version and normalized library-relative file hashes;
- `catalog`: `kind:id` → selected library, version and canonical loaded-description
  SHA-256. This captures first-match selection, not just names available on disk.

File hashes cover exact binary bytes, including line endings. The repository
pins catalog text to LF through `.gitattributes`; third-party libraries should
also preserve their file bytes when distributing across operating systems. Description hashes
cover exact native compact JSON. Host absolute paths and directory enumeration
order are not identities: an unchanged library can move to another machine/root.
Object key order in the lock itself is irrelevant. Added files are drift too.
All loaded libraries are pinned conservatively, including unused entries. Start
a fresh session to avoid carrying libraries from earlier projects into the scope.

Two distinct roots with the same library name are ambiguous and refused. Roots
and traversed entries must be ordinary directories/files: symbolic links,
Windows reparse points, special files and depth over 16 are refused. Capture has
20,000-file/128 MiB total limits. Filenames containing backslashes or colons are
refused. Concurrent filesystem mutation is unsupported; use a quiescent library
checkout during capture/check. The file size is rechecked against bytes read.

`schemas/library-lock.schema.json` validates standalone and embedded locks.
`tools/validate.py` additionally checks normalized non-hidden paths, the pinned
library manifest, total count and catalog-to-library references. Offline schema
validation checks structure; native checks compare actual current inputs.

## Acceptance evidence

`tests/protocol/test_library_lock.py` verifies CLI/RPC agreement, binary hashes,
relocation, native run acceptance/refusal, changed/removed/added file reports,
stale loaded-description rejection, failed pin rollback, time/observation
preservation and rejection of links/special files where the OS supports creating
them. `tests/browser/test_library_locks.py` uses a real native server to review,
pin, check, remove, undo and export a lock while the clock stays at 1 µs. The
exported native project contains the exact reviewed baseline. The gallery shows
that native session; no client-only success is used as evidence.

## Boundaries

This is a content baseline, **not** a remote registry, downloader, dependency
solver, archive of resources, publisher signature or accuracy qualification.
It pins the existing first-match resolver's actual selection. The subsequent
[declared version milestone](library-versions.md) additionally checks SemVer
requests and installed library dependency declarations. It does not add an
alternate-revision or remote dependency solver. It does not pin the
native executable, OS/toolchain, firmware images, inputs outside library roots,
or hidden files (the same exclusion used by library installation). A hidden or
external resource is outside this baseline even if a description references it.
File hashes record bytes now on disk for subsequent loads; they do not prove
which external script bytes an already-running model previously loaded. Project
edits need not invalidate a library baseline because the whole loaded library
scope is pinned. The independent model manifest captures project/configuration
identity. Neither artifact is a full executable simulation checkpoint.
