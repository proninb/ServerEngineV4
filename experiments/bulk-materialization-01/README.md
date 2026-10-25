# BULK-MATERIALIZATION-01

Изолированный Source V2 prototype: bounded validated mutation buffer → batch array extension и index construction. Objects, WHO, types, endpoint resolution/path interning и SourceMap используют текущую модель. Production-файлы не изменяются.

**Результат: NO ADOPTION.** На UnitProXL 15 пар дали регрессию полного PUBLISH 34,73 мс (95% bootstrap CI [20,33; 41,75]), или 2,66%. Source медленнее во всех 15 парах. Требование ≥10% ускорения не выполнено.

```powershell
cmake -S experiments/bulk-materialization-01 -B build/bulk-materialization-01
cmake --build build/bulk-materialization-01 --config Release --parallel 8 --target BulkMaterialization01
ctest --test-dir build/bulk-materialization-01 -C Release --output-on-failure
python experiments/bulk-materialization-01/run.py build/bulk-materialization-01/measurements --pairs 15
```

Переменные только экспериментального executable:

* `CW_BULK_MODE=0/1`: исходные mutations либо bulk candidate.
* `CW_BULK_BLOCK=65536`: верхняя граница unique pending targets двух категорий; допустимо 1…1048576.
* `CW_BULK_AUDIT=1`: counters/retained buffer capacities. В timed series отключено.
* `CW_BULK_VERIFY=1`: проверить все final construction index lookups. В timed series отключено.

`generate.py` создаёт отдельные Graph/Source translation units. Для private доступа используется существующий field-free friend `resolved_g01::access`. Макрос `CW_RESOLVED_G_01` включён только в standalone targets; все RG01 observers/includes удалены из генерируемых translation units. Replay, Header clone и RG02 path optimizations отсутствуют. Graph layout/records/API production не меняются; core полностью собирается из текущих исходников с IPO.

Mode 1 проверяет допустимость операции сразу, назначает будущий slot по прежнему first-insertion order, обновляет компактную pending запись при duplicate и выдаёт прежний `replaced` для warnings. SourceMap получает те же handles/origins в прежнем порядке. Blocks расширяют существующие G arrays, записывают новые/заменённые payloads и достраивают target indices без вызовов обычных initialization/link mutation API. После каждого flush G снова полностью материализован; до него только pending handles ещё не имеют соответствующей записи массива. Успешный Source заканчивается явным flush; semantic-error exit материализует успешно проверенный prefix при уничтожении локального batch.

Текущий Runtime final-state порядок сохранён: все final link bindings разрешаются до final initialization list, а repeated target сохраняет первый слот и последнее значение. Prototype не вводит последовательный интерпретатор Source statements и не сортирует writes по WHO.

Все 8 CTest suites проходят: исходный frontend, frontend с блоками 1/7/65536, три Header-prefix проверки, positive/negative artifact/diagnostic comparisons. В большой серии 46 PUBLISH calls сохраняют оба golden digests. Подробный отчёт — `docs/BULK_MATERIALIZATION_01.md`; сырые данные — `build/bulk-materialization-01/measurements`.
