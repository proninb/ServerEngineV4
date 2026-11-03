# SOAK-MEMORY-ATTRIBUTION-01 — OFF воспроизводит высокий memory level

2026-10-27. Выполнен **один новый OFF control**, 600 секунд по прежнему gate-протоколу. **OFF MEMORY FAIL / prior USE MEMORY FAIL / NO DEPLOYMENT.** Высокий quiescent Private Bytes и нарушение gate воспроизводятся без PGO. Общий lifecycle/allocator — обоснованное направление следующей диагностики; root cause, утечка и конкретный удерживающий owner этой работой не установлены.

PGO performance/correctness из [TCP-PGO-RELEASE-02](TCP_PGO_RELEASE_02.md) не пересматривались. A/B, training, сборка и USE soak не повторялись. Parser V2, Graph, SourceMap, Persistence, Runtime и TCP fixes не изменялись; allocator/forced trim/pretouch/cache не добавлялись. Release остаётся заблокированным.

## Контроль и неизменность

Точный OFF executable: `build/tcp-pgo-release-02/ab-01/binaries/ordinary/ServerEngineV4.exe`, SHA-256 `6cd312cbc030765718ad0e2fb847723d97efc0395455052cc8a8522843cc3429`. USE reference из прежнего soak: `509373908a081d913876445ab4c09f54dd072d4e23c16e6fc158b42d1720b554`.

До запуска текущий soak.py побайтово совпадал с сохранённым executed USE harness (`410c3fa8…`; точный hash в preflight). Добавлен только аргумент `--arm` и выбор executable/output directory. Diff, оригинал и выполненная OFF-версия сохранены. Body lifecycle/connection pattern, 10 warmup cycles, native/artifact oracle, 600 секунд, checkpoint и thresholds не менялись. OFF выполнен unsandboxed.

Порог: отдельно для каждого fixture last-five minus first-five median quiescent Private Bytes ≤128 MiB и handles ≤16. Checkpoint остаётся **UNLOADED после reconnect/GET_STATE**. Предыдущие USE JSONL/status, все исходные release evidence hashes, executable и сохранённая PGD проверены на неизменность. Итоговый PGD hash `b3a6fe175e377900959fab72754ee8ffed109b942da09a6fbf93b3feb545a11a` совпадает с A/B и completion manifest; это уточняет pre-USE hash в первоначальном тексте отчёта, не меняя профиль.

## Этап A — полный OFF gate

| Метрика | Новый OFF | Прежний USE |
|---|---:|---:|
| Measured elapsed | 604,762 сек | 604,925 сек |
| Measured cycles | 1071 | 1041 |
| Дополнительный warmup | 10 | 10 |
| Handles на checkpoints | 88…88 | 88…88 |
| Private Bytes min…max | 13,812…261,449 MiB | 12,199…258,656 MiB |
| Gate | FAIL | FAIL сохранён |

Все OFF protocol/lifecycle/payload assertions прошли; процесс штатно завершён. Exit code=1 — финальная memory assertion, не crash. Всего OFF с warmup: 1081 завершённый цикл, 271 duplicate LOGIN closure, 270 partial-frame reset, 270 oversized-frame closure, 270 disconnect; 1081 pre-login SHUTDOWN отклонён.

| Fixture | OFF первая → последняя медиана MiB | OFF drift MiB | OFF gate | USE drift MiB | USE gate |
|---|---:|---:|---|---:|---|
| unit | 19.410 → 260.316 | +240.906 | FAIL | +238.758 | FAIL |
| strings | 15.668 → 31.594 | +15.926 | PASS | +241.359 | FAIL |
| inheritance | 14.809 → 31.594 | +16.785 | PASS | +241.855 | FAIL |
| links | 15.047 → 31.633 | +16.586 | PASS | +241.852 | FAIL |

Только OFF UnitProXL нарушил итоговый per-case drift gate. Это всё равно означает FAIL общего OFF memory gate. Низкие конечные окна трёх малых fixture **не означают**, что они никогда не наблюдали высокий уровень или что общий паттерн пропал.

## Этап B — общий префикс и временные ряды

OFF успел выполнить на 30 measured cycles больше. Поэтому исходные «последние пять» OFF и USE относятся к разным точкам одинаковой последовательности, а не к одному и тому же lifecycle phase.

На общем префиксе **1041 measured cycles** (равные cycle, fixture и reconnect mode) последние пять медианных точек для **каждого** из четырёх fixture:

| Общий префикс | OFF | USE |
|---|---:|---:|
| Конечная private median | 260,203 MiB | 257,008 MiB |
| UnitProXL drift | +240,793 MiB | +238,758 MiB |
| strings drift | +244,535 MiB | +241,359 MiB |
| inheritance drift | +245,395 MiB | +241,855 MiB |
| links drift | +245,156 MiB | +241,852 MiB |

Следовательно, различие итогового окна малых fixture — существенный эффект точки остановки; смотреть только конечные медианы полного 600-second OFF было бы недостаточно. Это дополнительное alignment-сравнение, **не замена** исходного неизменяемого release checkpoint или gate.

Медиана парной разности USE−OFF на одинаковых циклах — около **−11,5…−12,0 MiB**, в последней четверти общего префикса −7,7…−9,4 MiB. Это описательные значения двух последовательных прогонов, не статистическое доказательство экономии памяти от PGO: memory A/B не рандомизирована и не реплицирована.

### Когда появляется высокий уровень и когда он снижается

- В обеих версиях первое превышение абсолютных 128 MiB и UnitProXL baseline+128 MiB — **cycle=80** (71-я measured iteration): OFF 44,31 сек, USE 46,73 сек. Для малых fixture первые drift-пересечения — следующие циклы 81/82/83. Совпадает completed-cycle position, не elapsed time.
- Доля quiescent samples выше **абсолютных** 128 MiB: OFF **62,46%**, USE **64,84%**. Абсолютный порог здесь служит описанию рядов; release gate использует per-case baseline плюс drift.
- Самая длинная последовательность наблюдавшихся высоких checkpoints: 50 samples в каждом arm; span OFF **23,29 сек**, USE **23,92 сек**. Это не доказательство непрерывного высокого memory между checkpoints во время PUBLISH/RUN/LOAD.
- Возвраты с >128 к ≤128 MiB: OFF **24**, USE **25**. После завершённых UnitProXL-циклов — соответственно **21/24** и **22/25**. Остальные возвраты встречаются после малых fixture.
- По последнему reconnect mode эти возвраты чаще помечены oversized-frame или duplicate LOGIN. Но UnitProXL стоит только на циклах, где mode по формуле тоже бывает 0/2. Это корреляция расписания, не доказательство, что memory освобождает именно LOGIN или frame error.

У исходного harness только один phase checkpoint. Он показывает возврат **после полного цикла**, но не различает момент после PUBLISH, UNLOAD и reconnect. Дополнительная phase instrumentation здесь не вводилась. Отдельный диагностический прогон с такими snapshots — следующий шаг, если требуется attribution; прежний gate-тест при этом надо оставить неизменным.

[График по elapsed time и общим циклам](../experiments/soak-memory-attribution-01/results/20261027/memory-timeline.svg). [Aligned CSV](../experiments/soak-memory-attribution-01/results/20261027/aligned-cycles.csv). На графике линии соединяют quiescent samples, не непрерывную фазовую трассу.

## Вывод и оставшиеся ограничения

PGO **не требуется** для воспроизведения этого высокого memory level и UnitProXL gate failure. Идентичная причина в двух arms не доказана, но гипотеза «drift бывает только у USE» опровергнута данным OFF control. Следующий приоритет — общая реализация lifecycle/allocator и момент освобождения ресурсов, без изменения алгоритмов или подбора нового порога.

Оба прогона функционально завершились, handles стабильны. Это не исключает удержание heap allocations, caches, deferred metadata или allocator retention. Конкретный механизм, отсутствие/наличие утечки и production memory budget ещё не установлены. Сравнение одной OFF-серии с одной прежней USE не оценивает межсерийную вариабельность и не доказывает причинную роль PGO.

Release остаётся заблокированным; первоначальный USE FAIL не аннулирован. Этап C не запускался: по запросу выполнено только необходимое сейчас OFF измерение и сравнение рядов. PGO A/B не повторять без новой причины.

## Evidence

Local root: `build/soak-memory-attribution-01/`: preflight.json, harness-selection.patch, оригинал/выполненный harness, analysis.json, aligned-cycles.csv, memory-timeline.svg, off-soak.log. Raw OFF: `build/tcp-pgo-release-02/soak-off-01/{status.json,samples.jsonl}`. Raw USE остаётся в прежнем soak-01 и portable release-02 evidence.

Переносимый набор: `experiments/soak-memory-attribution-01/results/20261027/`; analyzer и plotter находятся рядом в experiments. Executable, PGD, корпус и credentials не копировались. Commit/push, deployment и изменение защиты не выполнялись.


## Follow-up: SOAK-MEMORY-PHASE-01

[Восемь внешних фазовых снимков и карты VirtualQueryEx](SOAK_MEMORY_PHASE_01.md) собраны на frozen OFF отдельно от gate: dominant rise впервые после PUBLISH 80, снижение уже после следующего PUBLISH 100, повторный rise после PUBLISH 110. Quiescent разница находится в committed MEM_PRIVATE, fixed SHM base свободен, MEM_MAPPED стабилен. Private reservations одинаковы по bases/extents, но committed части меняются. Конкретный live owner/heap механизм ещё не идентифицирован; старые OFF/USE gate FAIL сохранены. Новых allocator/production исправлений нет.
