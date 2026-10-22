# EVALUATOR-PARALLEL-01

Эксперимент поверх STATE-LAZY-01. Фиксированы 16 workers существующего
frontend/Type Binding pool. Evaluator использует 1/2/4/8/16 из того же pool;
второй pool и дополнительные OS threads не создаются.

После завершения Type Binding coordinator подготавливает прежние immutable
schemas. `pool.run(N)` запускает N обработчиков. Каждый берёт следующий record
через общий `atomic<size_t>::fetch_add(relaxed)`, вызывает неизменённый
`sl01::evaluate_one` и пишет только в назначенный original declaration slot.
Private lane counters занимают отдельные cache lines. После pool barrier
координатор выполняет прежний `ei01::commit` и release. Work stealing, новые
очереди, grouping/chunk scheduling и параллельный commit не добавлялись.

CONTROL вызывает исходный sequential LAZY resolver на coordinator thread.
PARALLEL с N=1 включает dispatch/barrier и работу одного pool thread, поэтому
является отдельной точкой сравнения с прямым последовательным вызовом.
В CSV evaluator_workers — конфигурация пары; effective_evaluator_workers у
sequential всегда 1, у parallel N. Frontend workers всегда 16.

В callbacks читаются только const batch/types/prepared/canonical и task list.
Нет записей в global semantic result vectors/counters/issue или schema cache.
Каждый record_result имеет одного writer; output vector заранее создан и не
меняет размер. Завершение всех pool jobs предшествует commit и уничтожению
Parser requests/source/prepared context. Diagnostics и duplicate merge
определяются прежним sequential commit, а не порядком завершения workers.

```powershell
cmake -S experiments/evaluator-parallel-01 -B build/evaluator-parallel-01 -G "Visual Studio 18 2026" -A x64
cmake --build build/evaluator-parallel-01 --config Release --target EvaluatorParallel01 EvaluatorParallel01Allocations -- /p:BuildProjectReferences=false
ctest --test-dir build/evaluator-parallel-01 -C Release --output-on-failure
python experiments/evaluator-parallel-01/run_matrix.py build/evaluator-parallel-01 build/publish-subsecond-01/fixture/project.json build/evaluator-parallel-01/results-parallel
```

Native performance запускать вне sandbox без одновременных builds/tests.
По 3 fresh processes на evaluator count, 5 измеренных пар и 1 warmup pair;
counts проходят в разном порядке, sequential/parallel чередуются в процессе.
Pool startup вне таймера и одинаков для CONTROL/PARALLEL. Scheduling/barrier,
preparation, local evaluation, commit, release и весь semantic lifecycle внутри.
Отдельная allocation-сборка измеряет memory при всех пяти counts. Timing выводы
не строятся по allocation executable. Изменение retained result не допускается.

Tests: exact parity с sequential LAZY и независимым Header V2, WHO/types,
constructor/reference counters, direct/nested CV, first diagnostic и origins,
reverse/permuted tasks, duplicate definitions, source/request lifetime.
Дополнительный shared-schema stress содержит 128 records с pending/forward/stale
operations; workers пишут разные slots и читают один индекс. Он проверяет
семантику, но не является forward-heavy performance benchmark.

Production PUBLISH не подключён. Семантическое покрытие не расширено: это текущая
constructor/type projection, не full Header V2, String IDs/SourceMap или финальный G.
