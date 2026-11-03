# Frozen-source backing coverage audit 13

No builds, server/replay runs, allocator migration or production edits.
See docs/BACKING_COVERAGE_13.md for findings and explicit open coverage.

audit.py verifies all frozen source-manifest entries and scans server_engine cpp/hpp
for dynamic-storage spellings, potential growth/new/mapping operations and borrowed
storage references. It strips comments/literals/preprocessor directives and emits
new allocation-sites.json. It is a lexical index, not an AST/call graph or exact
allocation count. Unknown and inactive/conditional branches are not guessed away.

review_paths.py emits reviewed-allocation-paths.json with reviewed field/group,
concrete new/growth/factory/mapping paths, nested allocations, transfers and worker
boundaries. Review entries are not one-to-one runtime allocation events. Both tools
refuse to overwrite final evidence files; run only into a new audit output location
if expanding the source review. No runtime allocator is selected by these tools.

The documented source hashes, anchor/field validation and logical/physical limits
must accompany any citation of these artifacts. A regex hit count is not a backing
coverage percentage or implementation readiness gate.
