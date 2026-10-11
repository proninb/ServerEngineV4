# V3-18 tests

`test_v3_18_fields.cpp` tests source-token-only default initializer decoding with no Graph access.

The `mock_project` uses opt-in macros for integration tests:

- `TEST_CTOR` + `TEST_ROOT_TYPEDEF`: valid constructor and sentinel-owner typedef; FULL PASS.
- `TEST_FIELD_INIT`: `field=7` with V2 Graph value `7`; FULL PASS.
- `TEST_FIELD_INIT` + `TEST_FIELD_INIT_MISMATCH`: same source with Graph value `8`; PARTIAL.
- `TEST_GRAPH_ONLY_DIRECT`: source has default-zero field and V2 Graph nonzero `9`; PARTIAL.
- `TEST_CTOR` + `TEST_GRAPH_ONLY_NESTED`: Graph-only nested default; PARTIAL.

All mocks provide fake Graph and preprocessing infrastructure; they are NOT substitutes for MSVC and actual UnitProXL Graph parity.

## V3-22 profiling regression

The V3-22 paired benchmark adds a C++20 standalone executable compiled from
`benchmarks/v3_22_perf_benchmark.cpp`, which includes V3-21's benchmark TU
without changing production sources. The identical mock API tests are used.

Local positive commands (Linux compiler, *mock only*):

```sh
g++ -std=c++20 -O2 -DTEST_CTOR -DTEST_ROOT_TYPEDEF \
    -include tests/mock_project/mock.hpp -I tests/mock_project -I . \
    benchmarks/v3_22_perf_benchmark.cpp -o v322_perf_mock
./v322_perf_mock test.json pairs.csv phases.csv summary.csv 2 7

g++ -std=c++20 -O2 -DTEST_CTOR -DTEST_ROOT_TYPEDEF \
    -DCW_V322_ALLOCATION_PROFILE \
    -include tests/mock_project/mock.hpp -I tests/mock_project -I . \
    benchmarks/v3_22_perf_benchmark.cpp -o v322_alloc_mock
./v322_alloc_mock --alloc-only test.json allocations.csv
```

Negative gate: compile `-DTEST_CTOR -DTEST_GRAPH_ONLY_NESTED` instead.
The executable must reject `HEADER_GRAPH_PARITY_FAILED` and return nonzero.
Mock results are not representative performance data.

## V3-24 direct Graph write regression

The native Graph write API is only present in the benchmark's copied engine
overlay. The local mock Graph implements the same interface independently so
we can compile/test the producer without modifying production files.

```sh
cd header-syntax-v3-24
g++ -std=c++20 -O2 -DTEST_CTOR -DTEST_ROOT_TYPEDEF \
    -include tests/mock_project/mock.hpp -I tests/mock_project -I . \
    benchmarks/v3_24_perf_benchmark.cpp -o /tmp/v324-mock
/tmp/v324-mock mock.json /tmp/v324-pairs.csv /tmp/v324-phases.csv \
    /tmp/v324-summary.csv 2 7
```

The `TEST_GRAPH_ONLY_NESTED` fixture must *fail* with
`HEADER_GRAPH_PARITY_FAILED` (never a false PASS). Also compile with
`-DCW_V323_ALLOCATION_PROFILE` and execute `--alloc-only` to verify its
separate profiling path. MSVC/UnitProXL is a distinct mandatory gate.
