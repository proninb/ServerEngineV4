# HEADER-CHUNK-01: boundary and context planning audit

Первый этап эксперимента. **Новый chunk Parser не реализован.** Стенд измеряет
стоимость структурного прохода и построения плана; whole-root SB04 служит
контролем на том же подготовленном входе. Его время не выдаётся за chunked
lifecycle или за время параллельного разбора участков.

## Контракт

`boundaries.hpp` читает effective tokens без копирования. Допустимая единица —
завершённая struct/class/union/typedef declaration в глобальном или namespace
scope. Внутренние поля, типы, имена ссылок и тела функций не анализируются:
проход учитывает только вложенность `{}`, `()`, `[]` и конечный `;`.

Namespace задаётся immutable descriptor: original root, parent descriptor,
индекс токена имени, original semantic event. Unit и chunk хранят namespace
descriptor и исходный полуоткрытый диапазон токенов. Никакой итоговый каталог
видимых типов и никакой снимок накопленного Parser state не создаётся.

Chunks объединяют только соседние declarations в одном root/context. Target
мягкий: declaration больше target остаётся целой. Проход не меняет исходные
root ordinals и event coordinates. Неподдержанная namespace-форма, превышение
128 уровней namespace, неверная вложенность или незавершённая declaration
отменяют весь план разбиения этого root. Fallback означает необходимость
сохранить whole-root Parser, а не успешный произвольный C++ разбор.

Для положительного корпуса отдельная проверка сопоставляет **каждую** границу
с SB04 declaration begin/complete и каждый namespace parent с исходным symbol
event. Независимый Header V2 проверяет SB04 WHO/types/field inventory.
Это доказывает parity плана событий и контекста в проверяемой грамматике,
но ещё не parity будущего chunk Parser.

## Сборка и измерения

Требуются ранее собранные MSVC Release engine objects SB03; альтернативный
путь передаётся `-DENGINE_OBJECT_DIR=...`.

```powershell
cmake --build build/semantic-bind-03 --config Release --target SemanticBind03Engine --parallel 8
cmake -S experiments/header-chunk-01 -B build/header-chunk-01 -G "Visual Studio 18 2026" -A x64
cmake --build build/header-chunk-01 --config Release --target HeaderChunk01 HeaderChunk01Allocations -- /p:BuildProjectReferences=false
ctest --test-dir build/header-chunk-01 -C Release --output-on-failure
python experiments/header-chunk-01/run_matrix.py build/header-chunk-01 build/publish-subsecond-01/fixture/project.json build/header-chunk-01/results-final
```

Серия native, последовательная, без параллельных тестов/сборок. По 5 измерений
и одному прогреву: workers 1/2/4/8/16 × targets 1024/8192/32768. Output directory
должен быть новым. Сохраняются raw stdout/stderr, CSV, summary, источники,
исполняемые файлы и SHA256 ранее собранных engine objects.

- `discovery_ms`: все roots, включая namespace descriptors и временный stack.
- `largest_root_discovery_ms`: отдельное структурное сканирование самого
  длинного root; эта дополнительная диагностическая проба не является частью
  `discovery_ms` и не суммируется с ним.
- `context_ms`: упаковка units в chunks с уже найденными индексами namespace.
  Это **не** стоимость подготовки полноценного semantic environment.
- `sb04_*`: контрольный whole-root Parser/model/lifecycle. Lifecycle включает
  удаление requests, один sequential consumer и удаление результата/maps.
  Discovery/context находятся снаружи этого таймера; их времена нельзя выдавать
  за реальный новый полный lifecycle.
- Allocation executable отдельно измеряет planning peak, живой план, peak
  model+plan и ноль requests перед consumer. Input/pool startup не включены;
  это requested bytes, не RSS. Время instrumented executable не используется.

Оставшиеся вопросы: фактическое использование scope descriptors chunk Parser,
детерминированная ordered assembly без повторной материализации, первый exact
diagnostic и полноценный WHO/type/event parity после разбиения, critical chunk
duration, scheduling, реальный новый semantic lifecycle. Они намеренно не
подменяются оценкой по количеству токенов.
