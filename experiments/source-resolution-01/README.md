# SOURCE-RESOLUTION-01

Изолированные диагностические ablations Source V2. Production Parser, Graph API и Graph ABI не изменяются. CMake генерирует копию Source translation unit и заново компилирует текущий core с IPO.

```powershell
cmake -S experiments/source-resolution-01 -B build/source-resolution-01
cmake --build build/source-resolution-01 --config Release --parallel 8 --target SourceResolution01
ctest --test-dir build/source-resolution-01 -C Release --output-on-failure
python experiments/source-resolution-01/run.py build/source-resolution-01/measurements --pairs 15
cmake -S experiments/source-resolution-01 -B build/source-resolution-01-clean -DSOURCE_RESOLUTION_DIAGNOSTICS=OFF
cmake --build build/source-resolution-01-clean --config Release --parallel 8 --target SourceResolution01
ctest --test-dir build/source-resolution-01-clean -C Release --output-on-failure
python experiments/source-resolution-01/candidate_run.py build/source-resolution-01/clean-candidate-measurements --exe build/source-resolution-01-clean/engine/Release/SourceResolution01.exe --pairs 15
```

Режимы `CW_SOURCE_RESOLUTION_MODE`:

| Mode | Работа |
|---|---|
| 0 | Обычный Source без загрузки trace |
| 1 | Захват нормального исполнения |
| 2 | Обычный Source с загруженной trace; контроль одинакового footprint |
| 3 | Готовые semantic tokens; новые строки интернируются в прежнем порядке |
| 4 | Готовые результаты read-only identity lookup; новые WHO создаются обычным API |
| 5 | Готовые endpoints; реальные token reads, интернирование и зависимости сохраняются |
| 6 | Provenance исключён из окна и восстановлен теми же API в прежнем порядке после него |

`CW_SOURCE_RESOLUTION_TAPE` задаёт файл trace. Версия 2 привязана к содержимому Source и исходной Header-модели. Это временный локальный диагностический формат, не persistence ABI.

Проверки `CW_SOURCE_RESOLUTION_VERIFY=1` сверяют входы/выходы прокси. Измерение использует `0`, исключая подробную проверку каждого вызова. Оба режима проверяют итоговые golden artifacts отдельно от таймера. Trace IO, fingerprint, restoration и external SHA не входят в `stage_ms`. Окно включает создание/уничтожение Parser, обычные Graph mutations и extern validation; подготовка Source input находится снаружи.

Обычные `source_ms` / `total_ms` oracle-режимов включают дополнительную диагностическую работу и не являются результатом production-оптимизации. Для oracle A/B используется только одинаковое окно `stage_ms`. Внутри нет per-call clocks.

Эффекты не складываются: mode 3 исключает cursor и повторный identifier interning, но сохраняет Parser dispatch; mode 4 сохраняет создание новых WHO; mode 5 пересекается с синтаксисом и identity lookup, сохраняет реальные token reads и требует материализации endpoint; mode 6 не показывает стоимость только `sources.add`. Это контролируемые замены работы, а не exclusive CPU profile.

Correctness fixtures включают namespace, extern, cross-file writes, повторные присваивания, массивы, строки и rebinding; все frontend regression tests выполняются также с неактивным диагностическим режимом. Устаревший Source trace должен отклоняться.

Один реальный кандидат включается через `CW_SOURCE_READY_CURSOR=1` только в этом executable. Генерируемый `semantic_input.cpp` специализирует чтение ready Source tokens без нового кэша и повторного разбора. Для Header/compact input/detailed telemetry используется исходный маршрут. Frontend suite выполняется с обоими cursor. Full-PUBLISH runner сравнивает оба варианта в одном executable с mode 0 и без trace, а также проверяет неактивный executable относительно production baseline. Результаты oracle и кандидата не смешиваются.

Для решения о кандидате **обязательна clean-сборка с `SOURCE_RESOLUTION_DIAGNOSTICS=OFF`**: она использует исходный `source_parser_v2.cpp`, полностью без диагностических прокси. Калибровка первой, диагностической сборки выявила ~48 мс overhead Source даже в mode 0; её предварительный A/B не используется для production-выводов.

Итог 15 clean-пар: Source −11,56 мс, full PUBLISH −16,09 мс с 95% CI [−27,87; 25,14] выигрыша. Полный эффект не доказан: **NO ADOPTION**, production не изменён. Подробности — `docs/SOURCE_RESOLUTION_01.md`.
