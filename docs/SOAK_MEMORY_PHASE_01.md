# SOAK-MEMORY-PHASE-01 — переходы локализованы в интервалах PUBLISH

2026-10-27. **PASS DIAGNOSTIC / RELEASE BLOCKED.** На точном frozen OFF executable собраны восемь внешних снимков на цикл и четыре VirtualQueryEx карты. Переходы quiescent Private Bytes воспроизвелись на cycle 80 → 100 → 110. Большой подъём и последующее снижение впервые наблюдаются после PUBLISH, до последующих UNLOAD/TCP операций. Конкретный C++ owner и причина удержания private commit пока не установлены; allocator не менялся.

## Протокол и границы

Executable SHA-256 `6cd312cbc030765718ad0e2fb847723d97efc0395455052cc8a8522843cc3429`, `build/tcp-pgo-release-02/ab-01/binaries/ordinary/ServerEngineV4.exe`. Исходники/сервер не пересобирались, PGO/PGD, allocator, heap trim, caches, Parser/G/Persistence/Runtime не менялись. Оригинальный release-gate harness и результаты OFF/USE сохранены.

Отдельный диагностический harness: `experiments/soak-memory-phase-01/run.py`. Обычный unsandboxed запуск, тот же цикл выбора fixture и reconnect mode, первые 10 циклов warmup. Внешние GetProcessMemoryInfo/GetProcessHandleCount после восьми указанных операций сохраняют cycle/fixture/stage/PrivateUsage/WorkingSetSize/handles/UTC timestamp в JSONL и CSV. RUN/FREEZE проверяется через GET_STATE; artifact/native/value oracle сохранён.

Остановка объявлена до запуска: min cycle 100 и наблюдённая последовательность high→low→high; защитные пределы 180 циклов/300 секунд. Map-trigger high — абсолютные 128 MiB, только для выбора диагностических карт; исходный per-fixture release drift gate не менялся и здесь не пересчитывается.

Результат: **111 полных циклов (0…110), 888 фазовых снимков, 73,149 секунды** вместе с startup/warmup/картами. Это не повторный 600-second gate и не замер производительности. Дополнительные внешние наблюдения и карты меняют timing, хотя не добавляют telemetry allocations в target process. Ошибок protocol/oracle не было, процесс штатно завершён.

## Первая операция подъёма и последующее снижение

Первый phase sample выше 128 MiB во всём прогоне: **cycle 80, UnitProXL, after_publish**, elapsed 50,249 секунды. Затем quiescent high на 80, low на 100, rehigh на 110. Номера совпали с ключевым прежним переходом, но время/амплитуда diagnostic run не подменяют gate-серии.

PrivateUsage, MiB:

| Checkpoint | cycle 80 — подъём | cycle 100 — снижение | cycle 110 — повторный подъём |
|---|---:|---:|---:|
| Перед PUBLISH | 24.777 | 159.609 | 29.438 |
| После PUBLISH | 159.660 | 25.168 | 178.918 |
| После RUN/FREEZE | 159.660 | 25.168 | 178.918 |
| После UNLOAD №1 | 151.520 | 17.020 | 170.773 |
| После LOAD | 159.652 | 26.512 | 178.906 |
| После UNLOAD №2 | 151.520 | 18.391 | 170.773 |
| После TCP error/disconnect | 151.520 | 19.254 | 170.773 |
| После reconnect/GET_STATE | 151.520 | 19.254 | 169.379 |

- Cycle 80: before→after PUBLISH **+134,883 MiB**; первый UNLOAD снимает около 8,14 MiB, но 151,52 MiB остаются и после второго UNLOAD/reconnect.
- Cycle 100: before→after PUBLISH **−134,441 MiB**. Снижение уже произошло до RUN/FREEZE, первого UNLOAD, LOAD и reconnect этого цикла. Значит, оно не впервые появляется в позднем TCP teardown или в следующем UNLOAD.
- Cycle 110: before→after PUBLISH **+149,480 MiB**. После UNLOAD остаётся около 170,77 MiB; reconnect меняет лишь около −1,39 MiB, до 169,38 MiB.

Это локализация **между внешними command snapshots**, не трассировка внутренних allocations. Нельзя из этих данных выбрать lexical/Parser/Graph encoding/Runtime-plan подфазу или исключить совпавшую asynchronous allocator maintenance. Внутренний PUBLISH telemetry не вводился.

Handles закономерно меняются по состоянию: before PUBLISH 88, resident PUBLISH/LOAD 91, после UNLOAD 88, после TCP error 87, после reconnect 88. Все снимки каждого соответствующего stage имели эти значения. Dominant private-memory transitions не совпадают с ростом handles от reconnect.

## Карты VirtualQueryEx: private commit, а не retained SHM view

Карты сняты в исходном quiescent состоянии после reconnect/GET_STATE, когда состояние Project уже UNLOADED. Запрос read-only, без ReadProcessMemory, чтения payload или suspension. Сканирование заняло 2,97–6,98 мс. PrivateUsage до и после каждой карты совпал; сама карта всё равно не считается атомарной.

| Карта | PrivateUsage MiB | committed MEM_PRIVATE MiB | committed MEM_MAPPED MiB | committed MEM_IMAGE MiB | Фиксированный SHM base |
|---|---:|---:|---:|---:|---|
| initial-low (cycle 9) | 9.684 | 8.488 | 1.605 | 18.543 | MEM_FREE |
| first-high (cycle 80) | 151.520 | 150.262 | 1.605 | 18.543 | MEM_FREE |
| following-low (cycle 100) | 19.254 | 17.996 | 1.605 | 18.543 | MEM_FREE |
| second-high (cycle 110) | 169.379 | 168.121 | 1.605 | 18.543 | MEM_FREE |

У всех четырёх quiescent карт адрес `0x0000010000000000`, используемый fixed_direct SHM, имеет MEM_FREE. MEM_MAPPED и MEM_IMAGE commit неизменны. Поэтому наблюдаемый высокий quiescent объём в этих картах не объясняется оставшимся mapped SHM view по фиксированному адресу.

Между first-high и following-low committed MEM_PRIVATE уменьшился **на 132,266 MiB**, а reserved MEM_PRIVATE вырос на те же 132,266 MiB. У high/low/rehigh совпадают **32 private allocation bases и размеры их address extents**; у 21 изменилась committed часть. Например, у private extent `0x1442ebc0000` общий размер 15,8125 MiB во всех трёх картах, committed часть **11,633 → 1,395 → 13,797 MiB**. Таблица всех allocation bases сохранена в private-allocation-comparison.csv.

Это наблюдение согласуется с изменением commit/reserve внутри существующих private reservations. Оно **не идентифицирует heap implementation, CRT owner, live C++ objects или конкретный вызов decommit** и не исключает промежуточное освобождение/повторное использование адреса между картами. PrivateUsage описывает private commit, не live-object count ([Microsoft Learn: PROCESS_MEMORY_COUNTERS_EX](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex)).

VirtualQueryEx классифицирует регионы по state/type/allocation base; copy-on-write страницы могут сохранять MEM_MAPPED/MEM_IMAGE type, поэтому простое суммирование MEM_PRIVATE не обязано точно равняться PrivateUsage ([VirtualQueryEx](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualqueryex), [MEMORY_BASIC_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-memory_basic_information)). Разницу этих счётчиков не объявляем отдельной утечкой.

## Вывод и следующий диагностический предел

Для dominant перехода область поиска сужена до private-memory lifetime/commit behavior, проявляющегося в ходе большого UnitProXL PUBLISH. Первое повышение видно после PUBLISH 80, снижение — уже после следующего большого PUBLISH 100, повторное повышение — после PUBLISH 110. Два UNLOAD и TCP reconnect сами не снимают высокий уровень на цикле 80.

Следующий шаг для конкретного исправления — сопоставить **outstanding heap allocations / alloc-free stacks / owner** в high и low с этими private reservations, в отдельной диагностической серии. Эти данные нужны, чтобы различить освобождённые C++ objects с удержанным heap commit и реально живого/отложенного owner. Таких heap/live-object наблюдений сейчас нет, поэтому allocator retention остаётся поддержанной направлением гипотезой, а не доказанной окончательной причиной. Никакого локального «исправления» наугад здесь не делалось.

Исходные OFF и USE memory gates остаются FAIL, performance/correctness результаты не пересматриваются, release заблокирован. PGD/оба raw gate ряда и executable проверены до/после на неизменность. Threshold 128 MiB не повышался.

## Evidence

Local root `build/soak-memory-phase-01/`: status.json, phases.jsonl/csv, stage-deltas.csv, analysis.json, private-allocation-comparison.csv, четыре пары *-regions.csv / *-summary.json и executed-harness.py. Переносимый набор: `experiments/soak-memory-phase-01/results/20261027/`. Метаданные VirtualQueryEx содержат адреса/размеры/flags, не содержимое страниц. Бинарники, corpus, PGD и credentials не экспортированы. Commit/push и deployment не выполнялись.
