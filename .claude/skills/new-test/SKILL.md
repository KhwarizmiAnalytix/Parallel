---
name: new-test
description: Add or extend a unit test in Parallel, preserving its framework, test registration, and error-handling contracts. Use when writing tests for changed behavior.
---

# new-test

Read [CLAUDE.md](../../../CLAUDE.md) and the existing test closest to
the changed behavior before choosing a filename, fixture, or macro.

Match adjacent `PARALLELTEST`, Google Test, or fixture-based cases
and `ParallelTest.h`; do not replace the repository's own macros. Both
`Testing/Cxx/CMakeLists.txt` and `Testing/Cxx/BUILD.bazel` filter tests by
backend. Confirm an optional-backend test is enabled in the selected build.
Concurrency tests must avoid timing-only assertions and busy waits.

1. Search for existing coverage with `rg`; extend the relevant test file
   instead of creating a duplicate suite.
2. Match the neighboring includes, namespace, fixtures, naming, and license
   conventions. Do not bring in test helpers from a different repository.
3. Cover the meaningful success, boundary, and failure cases for the change.
   Assert public behavior and the repository's documented error contract.
4. Check source registration, discovery patterns, and backend exclusions so
   the new cases actually execute. Keep parallel build definitions in sync
   where both exist.
5. Run the affected tests using [project-build](../project-build/SKILL.md).
   Report the test command and result, including any unavailable backend.
