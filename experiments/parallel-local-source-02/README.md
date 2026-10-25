# PARALLEL-LOCAL-SOURCE-02 — Semantic Event Pipeline

Первый implementation slice: declaration-order DFS tasks, независимая local extraction, global typed linking, настоящий symbolic reference linker и final-cell reduction. **Production и Runtime не изменены. Parallel execution и materialization не добавлены.**

```powershell
cmake -S experiments/parallel-local-source-02 -B build/parallel-local-source-02
cmake --build build/parallel-local-source-02 --config Release --parallel 8 --target ParallelLocalSource02
ctest --test-dir build/parallel-local-source-02 -C Release --output-on-failure
python experiments/parallel-local-source-02/run_real.py build/parallel-local-source-02/real-validation
```

CLI:

```powershell
build/parallel-local-source-02/Release/ParallelLocalSource02.exe project.json report.json
build/parallel-local-source-02/Release/ParallelLocalSource02.exe project.json reverse.json --reverse
```

`--reverse` меняет порядок выполнения extraction, не назначенные task ranks. Global visibility проверяется по source_order. Это simulation другого ready order, не многопоточный benchmark. Optional `--oracle compiled.bin` проверяет WHO/type/construction и one-to-one typed-operation projection на корпусе без repeated logical writes; oracle не используется для выбора новых alias-aware winners.

Файлы:

* `model.hpp`: flat owned arrays, local indices, origins; без Graph/identity_space/string_table.
* `extract.hpp`: единственное место чтения Source tokens и синтаксиса endpoint/literal/declaration.
* `planner.hpp`: отдельный временный rank по DFS, существующие file_id сохраняются.
* `bind.hpp`: visibility/extern/types, member transitions, enum/value compatibility; input API не принимает tokens или Source buffer.
* `aliases.hpp`: final bindings, reference chains, nested binding targets, cycles, native member/object bindings, canonical dereference-free storage targets, LWW/warnings.
* `main.cpp`: Header V2 catalog, token release до semantic linking, local-model release до aliases, origin validation и output.

46 fixture runs и оба полных UnitProXL прохода успешно завершаются. UnitProXL: 3373 tasks, 717096 events, 946656 extracted/typed endpoints, 705238 explicit writes, 367144 bindings. Один declaration initializer объясняет отличие от 705237 assignments V2.

Прототип ещё дорогой: local arrays 174,48 MiB, bound arrays 178,97 MiB; alias stage в одиночных проверках около 1,13–1,18 с. Это correctness runs, не полный lifecycle/PUBLISH и не доказательство ускорения. Унаследованный V2 Runtime/bulk builder не вызывается из нового consumer. Подробности и ограничения — `docs/PARALLEL_LOCAL_SOURCE_02.md`.
