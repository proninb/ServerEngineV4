# SOURCE-V2-ENDPOINT-COST-01 — saved ETL reanalysis

2026-10-26. **Diagnostic complete; no implementation candidate justified.**
Read-only analysis of the existing SOURCE-V2-CRITICAL-01 stacks and current
source. No new benchmark runs, ETW capture, timers, counters, caches or production
edits. The narrow goal is to identify avoidable work before another A/B.

## Data and method

Same four saved traces: 32 sampled launches, 14249 recognizable Source samples,
recorded 1 ms sample interval, zero ETW loss. App EXE/PDB GUID/age are matched.
The original report's stack/inline/ICF limitations still apply. Nine selected
current source/build files exactly match the frozen profiled source hashes.
Saved stack-file hashes are unchanged before/after this analysis.

For read_endpoint, take the first **observed child** below that frame and assign
each sample once. The table partitions observed stacks, not an exact semantic
decomposition of invisible inline instructions. In particular the own-leaf row
can contain inlined work without a distinct frame. Numbers are estimated CPU
weight per sampled PUBLISH, not call counts or native wall milliseconds.

## 1. What is inside read_endpoint

| Disjoint observed context | Samples | CPU ms/PUBLISH | Share of read_endpoint |
|---|---:|---:|---:|
| Lexical/token processing, including intern | 1612 | 50.375 | 40.54% |
| Typed member/index traversal | 1134 | 35.438 | 28.52% |
| Own leaf / inlined work without a distinct child frame | 945 | 29.531 | 23.77% |
| Name/ID lookup and validation | 264 | 8.250 | 6.64% |
| Other observed child / OS / remainder | 15 | 0.469 | 0.38% |
| Dependency registration | 6 | 0.188 | 0.15% |
| Endpoint-path interning | 0 | 0 | 0% observed |
| Total | 3976 | 124.250 | 100% |

Token context: advance 1395 samples, semantic_input::next 213, next_source 4.
Member/index context: append_member 1081, prepare_member 44, append_index 9.
Name/ID context: identity_space::find_key 253, identity_space::contains 2,
string_table::contains 9. This is stable qualitatively across all four blocks;
the per-block distribution is retained.

**Canonical path interning is outside this function.** Code inspection confirms
read_endpoint returns a local typed path. canonicalize_endpoint creates the
global handle later, during ordered commitment; assign_string also interns its
element paths separately. All 2211 Source samples under
graph::intern_resolved_endpoint_path occur outside read_endpoint:
2018 with canonicalize_endpoint as immediate caller, 193 with assign_string.
That is 69.094 CPU ms/PUBLISH in a separate context, not part of the 124.250 ms.
The zero sampled overlap agrees with code structure; it is not inferred solely
from absence of a sample or a folded function name.

## 2. Where intern_prehashed is called

In all 1594 observed Source samples containing intern_prehashed, its immediate
caller is semantic_input::physical_identifier and the next caller is
semantic_input::next_source. There is no observed second interning entrypoint
inside member traversal.

| Observed outer context | Inclusive samples | Inclusive CPU ms | Own-leaf CPU ms |
|---|---:|---:|---:|
| read_endpoint on stack | 710 | 22.188 | 18.156 |
| assignment outside read_endpoint | 576 | 18.000 | 11.813 |
| object() outside read_endpoint | 294 | 9.188 | 3.906 |
| Other Source context | 14 | 0.438 | 0.031 |
| Total | 1594 | 49.813 | 33.906 |

44.54% of inclusive intern samples have read_endpoint on their stack. This is
**caller context, not exact identifier role**. The root name may already have
been read before entering read_endpoint; assignment's final advance can read
the next statement's first identifier. Consequently the remaining 55.46% cannot
be labelled exclusively non-endpoint identifiers. ETL does not contain token
offsets/arguments or per-call results to recover that classification exactly.

The union of read_endpoint and all intern_prehashed contexts is 4860 samples,
**151.875 CPU ms/PUBLISH**. Do not add 124.250 and 49.813 without subtracting
their 22.188 overlap. This is work observed, not work proven removable.

## 3. Is there duplicated identifier/member work?

The current ordinary path is:

```text
ready physical token + lexical hash
    -> next_source / physical_identifier
    -> intern_prehashed -> canonical string_id
    -> read_endpoint: WHO/type + member/index traversal using that string_id
    -> canonicalize_endpoint / string element handling
    -> Graph commit and SourceMap
```

Calls interleave through advance; this is a dependency diagram, not a claim
that all token interning precedes the read_endpoint call.

The lookups perform **different mappings**:

- String lookup establishes spelling -> canonical string_id and verifies full
  equality, preserving deterministic IDs and collision safety.
- WHO lookup uses scope, name and identity kind; a matching spelling alone
  does not determine which object is visible.
- Member lookup uses current record type plus string_id, yielding member,
  owner type, result type and inheritance steps. Equal names in different types
  need not identify the same member.

Source endpoint resolver has no string_table or identity_space dependency.
It accepts the canonical ID; it neither re-interns the spelling nor decodes the
token again. Thus a string-table lookup followed by typed member lookup is not
evidence of the same lookup performed twice.

Code also rules out one tempting duplicate-token hypothesis: peek_kind reads
into buffered only when empty; advance consumes that already-interned semantic
token without calling input.next again. Repeated peek does not re-intern it.
Scope dispatch already passes a prebound target WHO into read_endpoint, avoiding
the repeated LHS scope-chain lookup. RHS visibility still needs resolution.

prepare_member validates the current record/referent before advance consumes
the member identifier. Hoisting/batching interning past that boundary can change
the first error. Temporal declarations/rebindings, namespaces and shadowing,
canonical IDs, SourceMap and duplicate warnings remain semantic constraints.

There is a source-level repeated expression G.find(record) in record_type (a
presence check then access). Its survival as two machine operations under IPO
is **not established by ETL**. It does not justify another cache or a large
speedup claim; the prior type-preparation cache already lacked a reliable
full-PUBLISH benefit.

Conclusion: **no large avoidable duplicate sequence has been demonstrated in
the token -> intern -> member chain.** This is not proof that no optimization
exists. These stacks do not supply invocation counts, hit/miss distributions,
cycles per lookup or semantic-role traces needed to prove a new shortcut.

## 4. Already removed or previously tested work

| Mechanism | Current status / evidence |
|---|---|
| Ready Source tokens and lexical FNV hash | ON; 5889225 ready lexer tokens reported by saved phase telemetry. Re-decoding and hashing for a second token array already removed on the ready-token path. |
| Buffered peek/advance | Existing token is reused; no second next/intern at consumption. |
| Prebound LHS WHO | Existing direct pass-through from scope dispatch; RHS still uses visibility rules. |
| Member-transition cache | Already active unconditionally; type/member lookup on misses, path/type outputs reused on hits. Historical SOURCE-TYPE-PREPARE audit reported 92.66% hits; that hit rate was not remeasured here. |
| Endpoint scratch | reset clears length and state while retaining vector capacity; separate operands own their paths. |
| String Path Reuse | ON; existing string-element handles reused. Current STRING-BATCH audit recorded only 36512 string path interns for 508768 elements; no new path cache proposed. |
| String Batch | ON; verified by SOURCE-STRING-BATCH-AB-01, already production. |
| Full endpoint caches 13–15 and 37 | Rejected in their original series. Cache preparation/probing/copying outweighed removed traversal; do not repeat by renaming them. |
| Type preparation cache | NO ADOPTION; native full-PUBLISH comparison inconclusive. |
| Target WHO/type context | Retained experimental code, no proven native full-PUBLISH gain, not enabled. |
| String insertion vacant-slot reuse | STRING-INTERN-SLOT-01 not promoted; only targets misses, not the successful lookup cost. |
| Direct member spelling comparison | Historical PUBLISH_PROFILE experiment regressed Source; removed. Different older checkpoint, not a current timing comparison. |
| Parallel prebind / identifier caches | Not enabled in profiled build. They are not free elimination of work: binding requires its own pass/lookup and canonical-order/failure review. No new trial here. |
| New-link second lookup removal | SOURCE-V2-CRITICAL-01 NO ADOPTION; full native CI includes zero. |

References: `PUBLISH_SOURCE_LEXER_TOKENS_30.md`, `PUBLISH_PROFILE.md`,
`PUBLISH_ENDPOINT_REUSE_13.md`, `PUBLISH_ENDPOINT_CACHE_37.md`,
`PUBLISH_SOURCE_OWNED_ENDPOINTS_25.md`, `SOURCE_TYPE_PREPARE_01.md`,
`SOURCE_TARGET_CONTEXT_01.md`, `STRING_INTERN_SLOT_01.md`,
`RESOLVED_G_02_PATH_INTERN_COST.md`. Old timing/acceptance results retain their
original environment and checkpoint scope; they are not pooled with current ETL.

## 5. Decision and sub-second objective

No new candidate or native A/B was launched: the specified duplication gate
has not been met. No cache, extra table, model, parser or format change.
Production remains unchanged.

Removing roughly 300 ms exclusively from a roughly 430 ms Source wall interval
would require eliminating about 70% of that interval with other stages held
fixed. This analysis does not identify that much optional work. The selected
read_endpoint/intern union is about 152 sampled CPU ms and contains mandatory
checks; CPU weights are not a guaranteed bound on full wall-time speedup.
The data supports a narrower mapping of costs, not a demonstrated route to
sub-second PUBLISH or a promise to remove all member/name resolution.

Before a new implementation, require a concrete mapping/operation known to be
repeated with the same already-available result at the same semantic point.
Aggregate function heat, lexical name repetition and cache hit potential alone
do not establish that contract. The current evidence does not warrant repeating
the rejected cache/direct-spelling strategies.

## Reproduction and retained outputs

`experiments/source-v2-endpoint-cost-01/analyze.py` reads only the saved sampled
stacks and source files. Output: `build/source-v2-endpoint-cost-01/summary.json`,
`endpoint-partition.csv`, `provenance.json`; per-block distributions, immediate
callers and path caller stacks are retained. Stack hashes and nine code hashes
match before/after. Original current-V2 EXE/PDB identity is recorded.

```powershell
python experiments/source-v2-endpoint-cost-01/analyze.py build/source-v2-critical-01 build/source-v2-endpoint-cost-01-new
```

Choose a new output directory. Source/capture inputs are never overwritten.
