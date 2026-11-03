# BACKING-COVERAGE-13 — frozen-source allocation coverage audit

2026-10-30. **Source inventory/review deliverable COMPLETE; exhaustive allocation
coverage OPEN; implementation NOT AUTHORIZED; release BLOCKED.**

This is a source-level audit only. No production C++/allocator/UNLOAD/PGO change,
server/replay run, new benchmark, compile, commit or push. The diagnostic/replay
chain stays closed. No allocation domain, arena, private heap or page allocator
has been selected.

## 1. Exact scope and proof limits

All **287 files** in the frozen TCP snapshot were SHA-256 checked against
`build/project-unload-memory-01/source-manifest.json` (manifest SHA
`bc76f8dbb587c8d62e468f455501388dd620de6b4c2ac28db17e7a6da910c085`).
Source scanning covers **259 server_engine C++/header files**, including the 164
Project files; tests/CMake are verified but not counted as production allocation
sites. This is the frozen accepted TCP source snapshot based on e238ae1, not a
claim of identity to that Git commit before the TCP fixes.

Two separate evidence products are provided:

| Product | Content | What it does NOT prove |
|---|---|---|
| allocation-sites.json | 2379 source-anchored lexical records: 983 dynamic-storage spellings, 985 potential allocation operations, 411 borrowed-storage references | Exact allocation execution/count, active preprocessor configuration, implicit allocations or whole-program ownership |
| reviewed-allocation-paths.json | 159 reviewed owner/backing paths: 144 Construction, 10 Resident, 4 Connection, 1 Server | 159 distinct runtime allocation sites or complete coverage of all source/library allocation paths |

Each reviewed path has file/line, function or owning declaration, concrete
operation/storage, logical owner, backing API, last consumer, boundary, transfer,
worker safety, nested paths, physical release proof and eligibility. Declarations
are explicitly marked as storage declarations: their constructor/growth/new sites
are independently enumerated, not conflated with one allocation per declaration.

The lexical inventory retains **1992 Unknown-policy records**, including references,
declarations and caller-sensitive operations; this is not a count of Unknown live
blocks. Component policies mark 381 Construction and 6 Resident lexical records,
but those policies are leads, not per-call semantic proof. Automatic function
contexts are lexical aids and can remain unresolved. The manually reviewed ledger
is distinct and narrower. Source spellings, static sites and prior ETW traffic
percentages are different denominators and must not be combined into a coverage %.

The earlier first-pass allocation-inventory.json/reviewed-owner-paths.json are
retained locally; use the final filenames above. The final inventory excludes
preprocessor directive spellings such as `#include <new>` from allocation calls.

## 2. Reviewed Construction paths

Paths below are relative to the frozen source root; exact anchors and all fields
are in reviewed-allocation-paths.json.

| Component | Concrete dynamic paths | Last consumer / boundary | Backing coverage obligations |
|---|---|---|---|
| string_table | records, bytes, index (string_table.hpp:105–107); candidate index construction in ensure_index_capacity (:299); alias-protection stable_value.assign (:440); bytes.insert/records.push_back (:460/465) | Canonical spelling/identity and image encoding; operation return | All 3 vectors **and** transient alias string/rehash capacity. Returned string_views borrow byte storage; old and new index capacities overlap |
| identity_space | records, kinds, index (identity.hpp:196–198); candidate index (:436), resolve appends (:571/577) | Semantic resolution and persistence; operation return | 3 buffers plus temporary rehash; strings must outlive identity consumers. IDs are values, not ownership of spelling |
| graph | **21 direct vectors** at graph.hpp:916–955 | Parser/Semantic, physical-plan workers and encoder; operation return | Every dense/sparse column and index plus reallocation candidates. Record/index types reviewed here are scalar/range/value storage; aliases and future element changes need separate checks |
| graph composite child | graph.hpp:411 constructor_defaults → constructor_defaults.hpp:77 values vector; constructor_default_table::add inserts | Default construction lookup/encoding; operation return | **Additional vector not counted among the 21 direct graph vectors**; routing graph's visible fields alone misses it |
| source_map | 17 direct owning containers at :550–572; delta has 8 at :389–398 | Provenance/dependency/persistence; operation return | vector buffers, unordered_set buckets **and nodes**, rehash candidates, vector<bool> packed backing. Root-local sets are not zero-allocation merely because cleared per root |
| assign_table | entries/files/bytes at :95–97 | Assign parsing/persistence; operation return | All buffers and temporary string work; source()/target() expose borrowed spelling |
| file_context | 21 direct vectors at :546–580, including path/content/topology/baseline overlays | Acquisition, lexer/parser, persistence; operation return | Multiple default-allocator buffers. Baseline SourceSave mappings are borrowed, not allocated by file_context |
| worker file bytes | file_identity.cpp:1108 string(file_size) → execute_acquire result.snapshot.bytes → file_context.cpp:2268 move | Worker read, parser/lexer; operation return | Allocates before the owner commits the result. Allocator-aware outer container at commit cannot retroactively reroute the worker's existing buffer |
| owned_content | file_context.hpp:553; resize at file_context.cpp:2265, make_unique<string> at :2268 | File content views including later-discovered includes; operation return | **Three levels:** vector pointer slots, individually allocated string objects, string char storage/SSO. Current separate objects deliberately stabilize SSO views while outer tables grow |
| lexical_generation | 7 direct owning members at :339–353: nested source vectors, records[], directive_records[], arena_values[], replacement buffers | Lexer, parsers, persistence; operation return | Inner per-file token vectors, top-level arrays, per-arena words[]/directives[], growth old/new arrays. Default unique_ptr<T[]> uses new[] independently of any outer vector allocator |
| lexical_stream | reserve_words/reserve_directives candidates at lexical_stream.cpp:61/108 | Per-file lexer and publication/include replay; stream/operation completion | Separate unique_ptr arrays; optional output token-vector pointers borrow caller storage. Moving a stream does not alter allocation origin |
| semantic_input include frontier | prepared_include at semantic_input.hpp:121 and related caches/frontier | Include preparation/replay; semantic input/operation completion | vector/map/set storage, nodes, filesystem path strings, file snapshots, nested lexical_stream arrays; include workers borrow these. A frontier-vector allocator covers only one layer |
| preprocessor configuration | predefines/include_directories/root_directory at preprocessor_configuration.hpp:24–27 | Preprocessing; operation completion | vector elements contain independent name/replacement strings; include_directories has inner strings; root path owns native-string storage |
| preprocessor_v2 | resolutions vector<unique_ptr<resolution_record>> and index at :304–305, once buffers | Root/header preprocessing; operation completion | Outer buffer and individually allocated records; remaining nested record paths and callback targets need caller review |
| composed configuration | project_configuration_manifest.hpp:73 files vector, nested file_proof.path | Composition/validation/persistence; operation completion | Child paths and composer-local canonical/path strings/temporary pending vectors also allocate |
| resolver local scratch | source_endpoint_resolver.hpp:293 and transition/path vectors | Source endpoint parsing; resolver scope | Prepared cache is configuration-conditional, not a new Server cache. Source-only inventory does not prove a macro is active |
| physical plans | shm_layout's 8 vectors; shm_runtime_v2's 5 vectors + shm_type_batch's 17 vectors | Plan encode/materialization and resident binding derivation | Nested area storage is outside full_construction_context's visible members; LOAD physical-plan spans may borrow image storage instead of allocating those execution columns |
| workers | execution_lanes_windows.cpp:171/186 state/worker[]; manifest.cpp:253 vector<jthread> | Synchronous completion, stop/join at helper scope | Outer arrays do not cover CRT thread startup data, OS stacks or event handles. Partial startup/error cleanup and native wait failures remain OPEN |

All these heap-backed paths currently use ordinary default allocator/C++ new
backing. Logical deallocation/reset is visible, but **none has an independently
proven page-release guarantee merely from destroying its STL owner**.

## 3. Construction/Resident transfers: explicit exclusions

| Site | Actual ownership/transfer | Decision for Construction-only backing |
|---|---|---|
| runtime_binding.cpp:227/232/237 | prepare_runtime_bindings allocates next.object_offsets/member_offsets/base_offsets, then :331 moves next → output. create_resident_project moves bindings into project at :1560 | **EXCLUDE from Construction from the initial allocation**. A later move does not repair short-lived backing |
| runtime/project_runtime.cpp:1555 | make_unique<project>; copied project path plus moved compiled mapping/SHM/bindings | Resident object and retained nested members, including path, must survive PUBLISH through UNLOAD |
| project.ic_catalog_value.items | vector of entries, each with description string/path; server load/set_ic_catalog moves local catalogue into Project | Retained catalogue is Resident. JSON/path parsing temporaries must be separated by actual caller; not all IC helper allocations are Resident |
| construct_full_project:1602 | final compiled read-only mapping opens, moves into project; compiled_view points within it | Resident mapping/view pair; retire view consumers before UNLOAD unmap |
| construct_full_project:1639 | runtime.bin read-only mapping passes to create_resident_project for physical-plan spans | Temporary Construction mapping, **not** a resident project field; it can close after materialization consumers complete |
| runtime/project_runtime.cpp:1178 | fixed_shared_memory::create; mapping moves into resident project | Resident; reset closes this process's mapping/view on UNLOAD, not other processes' independently owned maps |

Runtime V2 scratch reset at project_runtime.cpp:1550–1551 occurs after bindings
are detached. This is a logical separation already present, not a new allocator
feature or an excuse to allocate bindings from a future Construction domain.

## 4. Server/Connection paths which must remain independent

Reviewed connection allocation at communication_connection.cpp:89 creates the
connection object with nested session/outbound/request/subscription state. The
outbound deque owns block/map arrays; its variant payloads own further strings,
vectors or diagnostics. peer.receive_buffer/send_buffer at tcp_endpoint.cpp:437/440
belong to the connection/transport lifetime. Endpoint implementation allocation
at :1315 is Server-owned; peer buffers remain individually connection-scoped.

Consequently a request allocating a response while handling PUBLISH is not
automatically Construction-owned. Serialized replies can legitimately survive
the operation until written, within the aggregate bounded infrastructure budget.
Opaque callback contexts/subscription targets and shared helper output ownership
remain caller-sensitive; no generic Server label is used to conceal Project storage.

## 5. Backing APIs and release proof

- Standard containers use their default allocator; unique_ptr owns an object or
  array allocated separately. No memory_resource, global operator-new override,
  HeapCreate or VirtualAlloc routing was found in scanned production sources.
  Hidden runtime/library allocation behavior is not closed by that lexical search.
- Writable compiled/runtime mappings are Construction-owned in persistence
  helpers (project_full_construction.cpp:375/480). Final read-only compiled mapping
  is a different Resident owner. Mapping APIs are distinguished from CRT heap
  sites in the reviewed ledger.
- Win32 mapping wrappers create/map views and RAII resets unmap/close them.
  This supports the mapping lifetime claim but does not demonstrate that unrelated
  heap commit returns, or close every exceptional/native API failure proof.
- STL growth may allocate new backing while old capacity remains live. Catching
  bad_alloc or swapping a temporary to empty proves neither complete physical
  reclamation nor coverage of nested allocations.

## 6. What is still Unknown / prevents implementation approval

This audit does **not** claim exhaustive source/call-graph closure. Specific gaps:

1. Active-build macro configuration and branch/call reachability. Inventory scans
   both normal and experiment-guarded source, not a preprocessed production TU.
2. Constructor/default argument/assignment/operator[] allocation paths, typedefs,
   user-defined composite members, nested STL allocator propagation and implicit
   filesystem/JSON/format/exception/library allocations beyond reviewed paths.
3. Caller-specific outputs of runtime/IC/persistence/diagnostic helpers. Class
   names or allocation time alone cannot establish the lifetime owner.
4. All parser/preprocessor scratch and nested includes, remaining source families,
   direct/borrowed view escapes, callback targets and fault/partial initialization
   paths. Source anchors are available but not automatically certified.
5. Worker CRT/TLS/stack and OS-native resource allocations, with no assumption
   that a caller's container allocator controls them. The same resource must be
   safely deallocatable by the actual last consumer thread without TLS switching.

The prior 35.90% allocation-count / 48.05% traffic Construction subset remains a
trace-classification result, **not this audit's full coverage**. The 159 reviewed
paths and 2379 lexical records cannot replace that missing semantic proof.

## 7. Comparison criteria for future designs (none selected)

| Variant | Must demonstrate before selection | Present finding |
|---|---|---|
| Explicit allocator-aware containers | Resource passed through every nested allocation and growth path; transfer-safe Resident storage; stable borrowed views; cross-thread deallocation | Basic three-vector tables are comparatively bounded, but a top-level allocator does not cover strings, unique_ptr arrays or helper locals |
| Lifetime-bound arena for suitable scratch | Alignment, destruction of nontrivial elements, growth peak, all workers/borrowers complete, zero invalid retained pointers | Logical lifetimes identify candidates; reallocation/SSO/include pointers and nested resources need explicit integration |
| Page-backed large work buffers | Concrete eligible buffers, page release on all exits, retained aliases ruled out, bounded reserve/commit and direct server verification | Lexical arrays/file buffers are candidates for analysis only; no threshold, backend or implementation chosen |

Before implementation, close a **concrete declared subset** with an allocation
path graph from owner → growth/new/factory → nested resource → last consumer →
release. State remaining shared-heap Project traffic explicitly. Do not promise
the whole-process budget from a partial migration, steal Resident vectors from
Construction backing, create a second full G, or silently route Unknown allocations.

## 8. Direct ServerEngineV4 verification plan, after authorization

Any future source candidate must use a separate snapshot/build and leave measured
OFF/USE/PGD references frozen. Verification is on the real server, not replay:

1. Logical live/allocation/borrower and worker-barrier accounting for the declared
   backing subset, including partial startup and failure unwinding.
2. PUBLISH/LOAD/BUILD/REBUILD parity for G/SourceMap/WHO/WHERE, values/links,
   runtime plans and native SHM, with the existing artifact/oracle checks.
3. Same TCP connection through UNLOAD → GET_STATE → LOAD/PUBLISH; Project-scoped
   callback retirement without closing healthy connections.
4. Unchanged quiescent memory gate and exact prior soak protocol: <=128 MiB
   aggregate Private Bytes drift and <=16 handle drift. No new per-domain
   exemption, heap trim, forced restart, altered checkpoint or baseline substitution.
5. Separate physical mapping/backing measurements and ordinary external process
   memory/handle samples. Passing logical zero-live accounting alone is insufficient.
6. Performance/regression gate and, for any actual source change, newly trained
   matching PGO profile only after correctness/physical release pass.

## Decision and artifacts

BACKING-COVERAGE-13 produces a source-anchored candidate catalogue and 159 reviewed
paths, including nested allocation and transfer hazards. **Full semantic/backing
coverage remains OPEN; this is not implementation approval.** No runtime proof or
guaranteed physical backing release was added. Release remains BLOCKED.

Authoritative local artifacts:

- `build/backing-coverage-13/allocation-sites.json` — frozen source files/hashes,
  lexical sites, component policies and explicit Unknown records.
- `build/backing-coverage-13/reviewed-allocation-paths.json` — reviewed records with
  the requested owner/backing/consumer/boundary/transfer/worker/release fields.
- `build/backing-coverage-13/validation.json` — source anchors, required fields and
  unchanged snapshot checks (validation of audit evidence, not ownership PASS).

Audit tooling: `experiments/backing-coverage-13/`. No production implementation,
allocator, UNLOAD, PGO, commit, push or deployment change. PGO OFF / NO ADOPTION.
