# HEADER-GRAPH-INDEX-01

Один эксперимент: growth factor 4× вместо 2× у существующего transient
`graph::member_name_index`. Формат slots, hash, probing, canonical records и
serialized image не меняются. `graph_delta` не меняется. Capacity по-прежнему
готовится один раз на record, до изменения canonical payload.

Большой allocation при отказе повторяется с прежней меньшей capacity; integer
overflow guard сохраняется. Другой corpus может получить до 2× index capacity
относительно прежней policy на промежуточной границе. На UnitProXL final capacity
совпала; total PUBLISH peak WS существенно не вырос.

## Сборка

Используются неизменные MSVC Release+IPO benchmark objects принятого V2+lazy из
`build/header-v2-state-lazy-01/ServerEngineV4PublishBenchmark.dir/Release`.
`graph.obj` и `publish_benchmark.obj` исключаются и собираются заново. Graph class
layout/ABI не менялся. Требуются исходные objects; executable не подменяет DLL.

```powershell
cmake -S experiments/header-graph-index-01 -B build/header-graph-index-01 -G "Visual Studio 18 2026" -A x64
cmake --build build/header-graph-index-01 --config Release -- /m:4
ctest --test-dir build/header-graph-index-01 -C Release --output-on-failure
```

- HeaderGraphIndex01Audit: counts-only диагностика. Не использовать её times
  для ranking production или сравнения скорости.
- HeaderGraphIndex01: timing executable без audit counters.
- HeaderGraphIndex01Tests: рост/lookup/conflicts в режимах 2×/4× и принудительный
  отказ wide allocation с fallback.
- HeaderGraphIndex01Regression: существующий compiled_project_tests.cpp с новым
  production Graph, без A/B runtime switch. Два legacy runtime модуля добавлены
  специально для этого suite; в production benchmark они больше не используются.

CW_GRAPH_MEMBER_INDEX_REFERENCE=1 выбирает прежний рост; =0 выбирает 4×.
Переменная читается только benchmark/test target, до построения Graph. В обычном
ServerEngineV4 этот environment switch и все audit counters отсутствуют.

## Native paired PUBLISH

Не запускать сборки/tests одновременно с timing. Сохраняются frozen executable,
stdout/stderr, CSV, source/executable hashes и paired bootstrap CI.

```powershell
python experiments/header-v2-state-lazy-01/run.py build/header-graph-index-01/Release/HeaderGraphIndex01.exe build/header-only-publish-01/run-20261023/full/project.json build/header-graph-index-01/new-results --control-env CW_GRAPH_MEMBER_INDEX_REFERENCE
python experiments/header-v2-state-lazy-01/run.py build/header-graph-index-01/Release/HeaderGraphIndex01.exe build/header-only-publish-01/run-20261023/full/project.json build/header-graph-index-01/new-bridge --control-env CW_GRAPH_MEMBER_INDEX_REFERENCE --baseline build/header-v2-state-lazy-01/results-20261023/ServerEngineV4PublishBenchmark.exe --pairs 12
```

Generic CSV labels eager/lazy здесь означают 2×/4×. Lazy Header state включён в
обоих случаях. Время audit builds с atomic counters не смешивается с timing.
Golden compiled/runtime проверяются после каждого полного PUBLISH.

Raw counts: `build/header-graph-index-01/audit/counts.json`, `candidate-audit.json`.
Timing: `results-20261023/`; independent original-executable bridge: `bridge-20261023/`.
Итог: `docs/HEADER_GRAPH_INDEX_01.md`.
