# HEADER-CHUNK-02: parallel local extraction, ordered semantic assembly

Изолированный эксперимент поверх SB04. Production PUBLISH не подключён.
Discovery и packing берутся из `../header-chunk-01/boundaries.hpp` без изменений.

`extractor.hpp` использует грамматику SB04, но читает ограниченный диапазон
исходного token stream. Original root ordinal, event positions, spelling views
и physical origins сохраняются. Полный накопленный каталог типов отсутствует.
Worker создаёт только local symbols/declarations/requests и immutable shape
arena. Индексы внутри результата worker не являются глобальными WHO.

`pipeline.hpp` выполняет ordered assembly:

1. Определяет первое extraction failure каждого original root.
2. Отбрасывает результаты более поздних chunks этого root, включая namespace
   events после failure, как при остановке whole-root Parser.
3. Сортирует symbol/declaration events в исходном порядке; восстанавливает
   по одному root symbol и namespace events из discovery descriptors.
4. Переводит local symbol/definition references в индексы assembled model.
   Буферы requests перемещаются сегментами; modifier payload не копируется.
5. Запускает прежние canonicalization, temporal Binding и deterministic
   numbering SB04. Local G отсутствует.

Если discovery отменил разбиение root, одна worker task вызывает прежний
whole-root SB04 Parser. Это structural fallback; неподдержанная семантика
внутри декларации не объявляется поддержанной. Эксперимент доказывает только
проверяемую SB04 type-binding projection. Полный C++ Parser с зависимым от
symbol table выбором грамматической ветки сюда не перенесён.

## Сборка

Как и предыдущие эксперименты, использует ранее собранные MSVC Release engine
objects SB03; путь переопределяется `ENGINE_OBJECT_DIR`.

```powershell
cmake --build build/semantic-bind-03 --config Release --target SemanticBind03Engine --parallel 8
cmake -S experiments/header-chunk-02 -B build/header-chunk-02 -G "Visual Studio 18 2026" -A x64
cmake --build build/header-chunk-02 --config Release --target HeaderChunk02 HeaderChunk02Allocations -- /p:BuildProjectReferences=false
ctest --test-dir build/header-chunk-02 -C Release --output-on-failure
python experiments/header-chunk-02/run_matrix.py build/header-chunk-02 build/publish-subsecond-01/fixture/project.json build/header-chunk-02/results-final
```

Для повторной серии нужен новый output directory. Timing native,
последовательный, без одновременных тестов/сборок. Сохраняются CSV, stdout/stderr,
source snapshots, executables, project SHA256 и SHA256 engine objects.

## Проверки

- Exact comparison с whole-root SB04: порядок и количество symbols,
  declarations, requests; scope/owner/definition; begin/open/complete/events;
  origins; modifiers; exclusions; destinations; WHO, types, first diagnostic.
- Прямой и обратный порядок постановки chunks, workers 1/2/4/8/16,
  targets 1/32/8192; namespaces, aliases, incomplete types, duplicate
  definitions, syntax failures и structural fallback.
- Независимый V2 oracle: 33 fixture cases × workers 1/4 × targets 1/32/8192.
- UnitProXL: whole-root и два порядка extraction сравниваются до измерений,
  затем V2 проверяет WHO, типы и полный инвентарь полей.
- Allocation executable: request instances должны быть 0 перед consumer;
  tracked live bytes должны быть 0 после удаления последнего результата.

## Измерения

Whole-root и chunk runs чередуются внутри одного процесса при одинаковом
подготовленном входе и одном persistent pool. Один исключённый прогрев каждой
реализации и пять измеренных пар на конфигурацию. Workers 1/2/4/8/16,
targets 1024/8192/32768.

Chunk lifecycle включает discovery, task planning/scheduling, extraction,
ordered assembly, освобождение worker outputs, canonicalization, Binding,
numbering, освобождение batch/requests, одно sequential checksum consumption,
освобождение bound result и numbering maps. Oracle и memory capacity accounting
вне таймера. Preprocessing не входит в semantic lifecycle.

`critical_chunk_ms` — максимальная длительность исполнения одной task, включая
создание/передачу local output, но без ожидания в очереди. Это wall clock, поэтому
показатель может включать preemption/allocator contention. При structural fallback
такой task соответствует целому root. `parser_ms` включает dispatch и ожидание
всех worker tasks. `assembly_ms` включает release local outputs. Между `model_ms`
и полным lifecycle также находится возврат с уничтожением оставшихся временных
структур, batch release, consumer и final teardown.

`primary_bytes` — прежняя метрика основных массивов SB04, не peak RSS.
`retained_bytes` — bound records/ranges/destinations, diagnostics, shape payload
и owners после удаления requests, без numbering maps и allocator metadata.
Allocation executable отдельно измеряет requested allocation peak только
semantic model; input/pool startup исключены. Instrumented duration не используется.
