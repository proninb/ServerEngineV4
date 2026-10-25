# SOURCE-LOCALITY-01 — page working sets and target runs

2026-10-23. The user's object/page locality hypothesis is supported by measurement. No production optimization is enabled in this audit. A small dedicated target context is justified as the next isolated experiment; a per-page WHO/type database is not required by these results.

## Evidence levels

**UnitProXL:** actual production Source V2 events with semantic WHO/type refs and successful writes to the existing G. Same isolated fixture used throughout prior experiments. A counter-free production control and two diagnostic PUBLISH calls preserve compiled/runtime goldens; the two diagnostic event streams are identical. This is complete semantic-pipeline observation on this corpus.

**UnitProXL_V2 and ABB:** additional user-requested inputs were copied into build fixtures. Production compatibility was attempted, but neither completes the current PUBLISH. Their complete Source inputs were therefore measured using an explicitly separate structural scanner of ordered object names, namespace scopes and parameter selectors. These measurements do **not** establish Header type correctness, canonical WHO/G parity, or full production compatibility.

The structural scanner was validated against UnitProXL's **entire ordered stream**: 3373 roots, 137,584 declarations and 579,512 assignments, with a bijective mapping of all 137,584 object identities. Reference operand identities and assignment kinds also match. The comparison excludes type IDs, source offsets and expanded character writes. All three structural scans report zero unclassified statements, unresolved target names and unresolved identifier operands; this does not replace type checking.

## Source/page locality

Here a page unit is a configured Source root after ordered book/chapter expansion. Physical token files are also tracked independently in native capture. In the successful UnitProXL fixture they coincide. Empty ABB Source leaves are retained; per-active-unit distributions exclude roots without declarations or assignments.

| Metric | UnitProXL, native | UnitProXL_V2, structural | ABB, structural |
|---|---:|---:|---:|
| Source roots | 3373 | 731 | 5394 |
| Active roots | 3373 | 731 | 4763 |
| Assignment statements | 579,512 | 328,024 | 366,855 |
| Distinct assigned objects per active root, median | **1** | **103** | **14** |
| Working-set objects per active root, median | 13 | 136 | 20 |
| Root type refs / nominal type spellings per active root, median | 12 | 20 | 7 |
| Assignment follows reusable same target context | **90.24645%** | **51.03194%** | **56.95956%** |
| Reuse also allowing context seeded by last declaration | **91.44988%** | **54.70911%** | **71.45085%** |
| Contiguous target run, median | 4 | 2 | 1 |
| Contiguous target run, p95 | 29 | 3 | 12 |
| Contiguous target run, maximum | 135 | 721 | 37 |

Working set is the union of defined/extern objects, assignment targets and reference root operands. Native type counts are actual type_refs; structural counts are nominal declaration spellings and may distinguish aliases that G canonicalizes.

Conservative reuse eligibility requires the same root, physical token file, scope, target name and object identity. Declarations and namespace transitions reset previous-target eligibility. The declaration-seeded simulation can initialize the candidate context from the latest declaration/extern. These are opportunity counts, not implemented parser bypasses or timing predictions. Syntax lookahead, declaration visibility and first-error order still require preservation.

## Why target and source contexts must be separate

Native UnitProXL contains:

- 579,512 left-hand target endpoints;
- 367,144 right-hand reference operands;
- total **946,656** object-type requests.

A simulated single WHO-only last-object type cache shared by both sides hits only **166,384 / 946,656 = 17.58%**. Reference operands commonly replace the target in that cache. In contrast, a dedicated conservative target context can reuse **522,989 / 579,512 = 90.25%** left-hand bindings. These percentages have different denominators; the absolute counts show the benefit of retaining target state separately.

Structural shared-last-object hit rates are 40.77% for UnitProXL_V2 and 42.07% for ABB, again over both operand sides. The corresponding dedicated-target percentages are in the table. The audit does not measure how much wall time these lookups cost: earlier type preparation already showed that high reuse need not imply a significant PUBLISH gain.

## Cross-page relationships

Native UnitProXL has **314,830 / 367,144 = 85.75%** reference operands pointing to an object defined in another Source root. No references precede their definition in this corpus. Nevertheless, **no target WHO is explicitly assigned by more than one root**, and no expanded G parameter key is explicitly written by more than one root.

This explains the observed pattern: each page writes its local objects and links them to objects elsewhere. Cross-page references describe links; they do not by themselves establish a dependency on computed intermediate values.

UnitProXL_V2 structurally has **8241 target object names updated on multiple pages**. Their written textual selector keys are distinct across pages in this scan. Therefore a general design must not assume one owner page per WHO. Structural selector equality cannot detect overlapping whole-array/element writes, inherited aliases or runtime reference aliases, so zero duplicate textual selectors is not a proof of semantic independence.

Structural reference counts:

| Metric | UnitProXL_V2 | ABB |
|---|---:|---:|
| Reference operand statements | 78,280 | 115,373 |
| Operand defined in another Source root | 56,699 | 50,752 |
| Operand appears before its Source definition | 2717 | 22,707 |
| Operand with no definition in the scanned Source domain | 0 | 20 |

Forward cases already have a visible declaration in the structural stream; declaration-before-use is distinct from definition-before-use. ABB's 20 entries count operand occurrences, not necessarily 20 unique objects. Inputs outside the scanned Source domain and actual semantic completeness require separate investigation.

## New-corpus compatibility findings

Original input trees are read-only:

- `C:\Boris\CW\UnitProXL_V2\Project`
- `C:\Boris\CW\ABB\Project\Project`

Fixtures are under `build/source-locality-01/fixtures/`. The established header preparation removes Studio annotations only from copied files and supplies SDK empty SimObject/SimLink and byte alias declarations in a separate fixture header. Original Type hashes were rechecked. All 731 UnitProXL_V2 and 5394 ABB Source leaves match their original bytes. Conversion manifests preserve input order and hashes.

The shared legacy importer gained optional `expand_chapters=True` / `--expand-chapters`; its default remains unchanged. Source `.chap` include containers now expand like `.book` in this experiment, preserving non-include text and page bytes. This fixes the initial ABB preprocessing-directive barrier without changing Source programs.

Remaining production blockers:

1. **UnitProXL_V2:** Header V2 rejects `enum U_TYPE_ENUM` at copied `unity_pro_xl_base.h:34`. No enum-to-integer rewrite was applied. Source semantic observation cannot start in this full pipeline.
2. **ABB:** after include-container expansion, production returns lifecycle status 2 without a detailed diagnostic. Native event capture shows 14 roots begun and 13 completed; the last classified operation is `ABB_9DC8A6A94AA2_N314 = true;` after `const bool ABB_9DC8A6A94AA2_N314;`, in `5050204.page`. That assignment has no successful Graph-write event. This locates the observed barrier; it is not presented as complete root coverage or a fixed parser defect.

No parser grammar, type semantics or original file was altered to force these corpora through PUBLISH.

## Validation and raw artifacts

- UnitProXL control + two diagnostic calls preserve compiled SHA-256 `a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d` and runtime SHA-256 `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`; identical native events SHA-256 `e948d3d2e48554a9f116313ec749ee8bdf62551b6b42bf62e2dcbe198bb81ab8`.
- Native explicit Graph writes: 1,072,381. String assignments count as 15,899 statements but expand to character-element writes. The structural scanner records one syntactic write per string statement; those totals must not be compared as equivalent Graph-write counts.
- Structural statement classification: UnitProXL 717,096 (137,584 declarations + 579,512 assignments); UnitProXL_V2 445,847 (117,823 including externs + 328,024); ABB 474,140 (107,285 including externs + 366,855).
- Three analysis/import tests pass: cross-page writes and forward operands; scope shadowing/string semicolons; optional chapter expansion preserving order, original bytes and old default behavior. Python syntax checks and `git diff --check` pass. Production-control and diagnostic Release+IPO executables build successfully.
- Diagnostic hooks and data structures are absent unless `CW_SOURCE_LOCALITY_AUDIT` is explicitly defined. No diagnostic wall time is used as optimization evidence.

Raw native capture/control/errors: `build/source-locality-01/capture-20261023/`. Expanded ABB control error: `build/source-locality-01/capture-abb-expanded-20261023/`. ABB partial native events: `build/source-locality-01/abb-prefix/`.

Structural data: `build/source-locality-01/structural-{unitproxl,unitproxl-v2,abb}/`, including quality.json, events.csv, files.csv, per-unit CSVs and summary.json. Full ordered-stream validation: `build/source-locality-01/structural-parity.json`. Combined summaries: `build/source-locality-01/comparison.json`.

## Decision

Proceed, if optimizing next, with a single dedicated current-target context, optionally seeded by declarations. Retain current token-origin handling, visibility checks and immediate ordered G writes. RHS resolution must not replace target context. Start with an isolated A/B and test shadowing, type/object spelling collisions, externs, scope/root changes and first diagnostics.

The audit proves locality, not speed. Neither a new per-page type database nor parallel page execution follows automatically from these measurements.
