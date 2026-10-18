## Problem and result

Describe the concrete problem and the resulting behaviour. Link the issue or
the roadmap item. State any remaining limitation.

## How it works in real life

For a behaviour change (ADR-030): how the real thing works and how engineers
use it; what anyone can observe from outside and how this change matches it;
every mode and edge case covered, and what is declared as omitted; where the
engine's inside differs and why the outside is unchanged. Write N/A for
changes with no behaviour.

## Validation

List the commands, presets and platforms you ran and their results, including
skipped, failed or unrun checks. For a behaviour change, name the test or
fixture that detects a violation of the contract.

## Review checklist

- [ ] Commits carry the repository owner's identity and no co-author or assistant trailers (CONTRIBUTING).
- [ ] Contract changes update the specification and requirements; architecture decisions have an ADR. Explain N/A in Validation.
- [ ] New behaviour answers ADR-030's five questions and matches the real thing from outside.
- [ ] New behaviour ships with tests or fixtures; extracted EmbCC code passes its golden fixtures.
- [ ] Descriptions carry fidelity declarations and provenance; vendored material carries its licence.
- [ ] User-visible changes have an Unreleased changelog entry, or Validation explains why none is needed.
- [ ] STATUS.md reflects any change in what is implemented or verified.
