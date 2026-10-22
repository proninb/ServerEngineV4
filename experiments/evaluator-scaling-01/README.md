# EVALUATOR-SCALING-01

Диагностический эксперимент поверх SC03. Production PUBLISH и исходники SC03
не меняются. Новый `constructors.hpp` повторяет тот же evaluator, добавляя только
таймеры, allocation attribution и наблюдение использования schema cache.
При выключенном audit вызывается исходный SC03 resolver напрямую.

Режимы `--audit`:

- 0: исходный resolver, без instrumentation;
- 1: action/catalog preparation, finalize и остаток;
- 2: дополнительно inclusive wall time каждого record;
- 3: полные вложенные exclusive timers и attribution allocations;
- 4: подробные таймеры только для выборки records с вероятностью 1/32.

Sample selector детерминирован: hash(definition XOR trial) modulo 32. Все records
выполняются, семантический алгоритм/порядок остаётся прежним. Выборка меняет только
наблюдение. Буферы audit заранее выделены вне lifecycle. CSV пишется после замера.
В modes 3/4 dependency counters фиксируют schema accesses, builds и record users.

Детальные таймеры сильно искажают короткие операции. Их raw/scaled times нельзя
суммировать в разложение исходных 97,54 мс или считать доказанной стоимостью lookup.
Режим 4 уменьшает общий overhead, но не устраняет локальный clock overhead внутри
выбранного record. Надёжнее крупные стадии, mode-2 record distribution и allocation
counts из отдельной сборки. State construction/cleanup имеют одну пару таймеров
на record и рассматриваются отдельно от частых member lookup timers.

Per-record inclusive time включает общие output/merge/cache operations исходного
resolver. Это распределение текущей реализации, не доказанная стоимость будущей
независимой задачи. Особенно учитывать rehash/growth глобальных containers.

```powershell
cmake -S experiments/evaluator-scaling-01 -B build/evaluator-scaling-01 -G "Visual Studio 18 2026" -A x64
cmake --build build/evaluator-scaling-01 --config Release --target EvaluatorScaling01 EvaluatorScaling01Allocations -- /p:BuildProjectReferences=false
ctest --test-dir build/evaluator-scaling-01 -C Release --output-on-failure
python experiments/evaluator-scaling-01/run_audit.py build/evaluator-scaling-01 build/publish-subsecond-01/fixture/project.json build/evaluator-scaling-01/results-final
python experiments/evaluator-scaling-01/analyze.py build/evaluator-scaling-01/results-final
```

Performance запускать native вне sandbox, без параллельных builds/tests.
run_audit сохраняет source/exe/engine object SHA256, raw CSV, frozen SC03 bridge.
3 fresh processes × 3 измеренных trials × 5 modes при 16 workers; отдельно
1 процесс при 1 worker, allocation executable и frozen SC03 executable.
У каждого процесса исключён первый trial каждого режима. Extraction workers
не являются evaluator workers: evaluator во всех режимах последовательный.

Проверки сохраняют SC03 scheduling/lifetime и 606 V2 oracle comparisons;
отдельный тест сравнивает все пять режимов на duplicate merge/conflict,
stale generations и приоритете diagnostics. На корпусе каждый режим проверяет
checksum типов и полных CV, а также kind/event/detail/origin первой ошибки
относительно исходного resolver. Oracle validation выполняется до timed series.

Никакого parallel_for, изменения индексов, lazy-state optimization или нового
merge алгоритма в этом эксперименте нет. Dependency audit должен отдельно
учесть shared lazy schemas, общий definitions index, nested merge, result vectors,
counters и earliest diagnostic. Zero duplicate owners на UnitProXL не отменяет
необходимость duplicate fixtures и ordered merge в следующем эксперименте.
