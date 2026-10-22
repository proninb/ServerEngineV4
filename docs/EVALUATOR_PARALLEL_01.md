# EVALUATOR-PARALLEL-01

Эксперимент завершён: параллельный evaluator уменьшил полный semantic lifecycle
UnitProXL до 144,63 мс при 8 workers и 145,09 мс при 16 workers. Это текущая
constructor/type projection, а не production PUBLISH или полная Header V2 grammar.
Production PUBLISH этим экспериментом не изменён.

## Реализация

Изолированный код находится в `experiments/evaluator-parallel-01`.
CONTROL вызывает исходный последовательный LAZY resolver в том же executable.
PARALLEL сохраняет прежние preparation, `sl01::evaluate_one` и ordered commit.
Меняется только распределение независимых records.

Frontend и Type Binding используют существующий pool из 16 потоков. После их
завершения coordinator готовит immutable metadata и 160 необходимых schema
indices. Затем `pool.run(N)` запускает N обработчиков: каждый получает следующий
record через атомарный индекс и пишет только в его заранее назначенный declaration
slot. Общие batch, bound types, schemas и task list доступны только для чтения.
Mutable state и output принадлежат отдельному record. После barrier coordinator
выполняет прежний последовательный commit, включая merge и выбор первого diagnostic.

Второй pool и дополнительные evaluator OS threads не создаются. Стадии разделены
barrier; активного frontend одновременно с evaluator нет. В измерениях фактическое
число evaluator threads и peak active lanes совпало с N; каждый запуск обработал
24 027 records. WHO, semantic order, schema lookup и формат результата не менялись.

## Методика

MSVC Release, target 32768, frontend/Type Binding workers всегда 16. Для каждого
evaluator count 1/2/4/8/16 выполнены три свежих процесса, по пять измеренных пар
CONTROL/PARALLEL и одной исключённой warmup pair в процессе. Порядок вариантов
чередуется; порядок counts меняется между сериями. Итого 150 измеренных lifecycles
и 30 warmup runs. Native performance измерялся вне sandbox, без параллельных
builds/tests. Allocation executable запускался отдельно и не использовался для
выводов о времени.

Pool startup одинаков и вне таймера. Discovery, extraction, assembly, canonicalization,
Binding, preparation, scheduling/barrier, evaluation, commit, consumption и release
входят в полный lifecycle. Oracle и дополнительные проверки порядка вне таймера.

## Результаты

Медианы, мс. Последовательный контроль новый, парный, в том же executable.

| Evaluator workers | Local CONTROL | Local PARALLEL | Evaluator CONTROL | Evaluator PARALLEL | Lifecycle CONTROL | Lifecycle PARALLEL |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 54,09 | 54,77 | 103,05 | 104,24 | 185,36 | 188,45 |
| 2 | 53,20 | 38,74 | 101,31 | 85,45 | 186,43 | 170,25 |
| 4 | 53,14 | 21,81 | 101,10 | 69,17 | 186,80 | 153,87 |
| 8 | 52,67 | 13,31 | 101,26 | 60,74 | 185,77 | 144,63 |
| 16 | 53,24 | 9,90 | 101,87 | 60,49 | 186,49 | 145,09 |

Разность внутри каждой пары — PARALLEL минус CONTROL. Она отличается от разности
двух независимых медиан; отрицательное значение означает ускорение.

| Workers | Медиана парной разности lifecycle, мс | Диапазон разностей, мс | Ускорившиеся пары |
| --- | ---: | ---: | ---: |
| 1 | +1,41 | −6,47…+19,96 | 5/15 |
| 2 | −15,68 | −27,94…−3,08 | 15/15 |
| 4 | −29,85 | −41,85…−21,12 | 15/15 |
| 8 | −35,55 | −51,46…−27,60 | 15/15 |
| 16 | −40,76 | −65,49…−28,22 | 15/15 |

Все 15 lifecycles при 8 workers находятся в диапазоне 132,66–157,13 мс,
при 16 — 129,31–157,47 мс. Цель менее 160 мс выполнена во всех измерениях этих
двух конфигураций. При 4 workers медиана ниже 160 мс, но максимум 177,86 мс.

16 workers ускоряют local evaluation относительно 8, однако медианы полного
lifecycle различаются всего на 0,45 мс. Это плато полного пути в данной серии,
а не доказательство универсально лучшего count. 8 workers — разумный кандидат
для следующего эксперимента на этом корпусе; production default не назначался.
Точка N=1 включает выполнение на pool thread вместо coordinator и расходы
dispatch/barrier, поэтому её разность нельзя считать чистой ценой scheduler.

## Оставшиеся расходы

| Стадия, медиана мс | 8 workers | 16 workers |
| --- | ---: | ---: |
| Discovery | 5,40 | 5,57 |
| Extraction | 21,73 | 21,32 |
| Ordered assembly | 14,00 | 13,81 |
| Type Binding | 27,46 | 27,84 |
| Action preparation | 12,72 | 12,77 |
| Canonical metadata | 7,08 | 7,55 |
| Immutable schema indices | 1,72 | 1,68 |
| Local evaluation | 13,31 | 9,90 |
| Ordered commit | 15,61 | 15,23 |
| Evaluator release / residual | 11,06 | 12,06 |
| Critical evaluator lane | 12,35 | 8,89 |

Медианы стадий нельзя складывать как точное разложение медианы lifecycle.
Release/residual вычисляется внутри каждого trial как разность evaluator wall и
измеренных его стадий; включает освобождение prepared/output и прочие расходы.
Последовательные preparation, commit и release теперь ограничивают дальнейший
эффект распараллеливания. Причины неполного масштабирования local stage требуют
отдельного профилирования; этот эксперимент не устанавливает allocator contention
или memory bandwidth как доказанную причину.

## Память

Одинаковые результаты allocation audit для всех пяти counts:

| Метрика | CONTROL | PARALLEL |
| --- | ---: | ---: |
| Allocation calls | 1 659 141 | 1 659 142 |
| Requested allocation bytes | 735 941 487 | 735 941 575 |
| Peak tracked bytes | 234 445 436 | 234 445 436 |
| Consumer live bytes | 46 642 850 | 46 642 850 |
| Requests at consumer | 0 | 0 |
| Live bytes after final release | 0 | 0 |

Дополнительные один allocation и 88 bytes относятся к callable для `pool.run`
и освобождаются до commit. Retained model estimate также одинаков: 46 427 084 bytes;
он отличается от фактического consumer live allocation counter. Local outputs
занимают прежние 23 491 040 bytes по model estimate. Thread stacks не включены
в tracked allocation counter; существующий pool из 16 потоков одинаков для обоих
вариантов. Дополнительных evaluator threads нет.

## Корректность

CTest: 4/4 успешно. Обычная и allocation-сборки проходят по 2025 scheduling checks.
Независимый Header V2 oracle: 1080 checks — 108 fixtures, пять evaluator counts и
два chunk targets. Проверены WHO/types/events/origins, direct/nested construction
values, diagnostics, duplicate merge/conflict, pending/stale generations,
обратный и перемешанный порядок задач и lifetime после освобождения requests.

Дополнительный stress использует 128 records, читающих общий immutable schema,
с forward/pending/stale operations, пять counts, три порядка и три повтора.
Negative oracle case проверяет ошибку nested path. На UnitProXL все 24 027
definitions сохраняют projection value parity; проверено 727 868 полей,
84 474 reference operations и 30 608 scalar operations. На этом корпусе references
разрешаются немедленно: большие forward-heavy потоки по производительности не
измерялись. ThreadSanitizer не запускался; проверка владения и barrier дополнена
точными scheduling/stress тестами.

## Артефакты и воспроизведение

Команды сборки и запуска приведены в `experiments/evaluator-parallel-01/README.md`.
Результаты: `build/evaluator-parallel-01/results-parallel/`:

- `measurements.csv`, `summary.json`, `paired-analysis.json` — полные измерения;
- `run-*-evaluator-*.stdout.txt` и `.stderr.txt` — исходные логи;
- `allocations-*.txt` — отдельные memory audits;
- `manifest.json`, frozen executables и snapshots — привязка результатов к исходникам;
- `frozen-state-lazy-bridge.txt` — дополнительный контекст старой сборки, не парный контроль.

Исходный проект UnitProXL не изменялся; использована копия fixture в build.
Полная Header V2 parity, member initializers, оставшиеся callable/layout rules,
String IDs, SourceMap и конечный G остаются за рамками эксперимента.
