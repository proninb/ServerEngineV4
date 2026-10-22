# HEADER-SEMANTIC-COVERAGE-03 — nested targets

Дата: 2026-10-22. Изолированный эксперимент, production PUBLISH не изменён.
Исходники: `experiments/header-semantic-coverage-03/`.

## Результат корректности

UnitProXL прошёл независимый Header V2 oracle и обратный порядок chunks.

| Метрика | SC02 control | SC03 |
|---|---:|---:|
| Definitions без gaps | 23 678 | 24 027 |
| Definitions с gaps | 349 | 0 |
| Сравнено construction fields, request-level | 684 441 | 727 868 |
| Исполнено reference operations | 75 285 | 84 474 |
| Исполнено scalar operations | 24 878 | 30 608 |
| Извлечено scalar operations | 25 477 | 30 608 |
| Извлечено nested operations | 0 | 5 403 |
| Проверено nested defaults | 0 | 5 403 |
| Sparse direct defaults | 97 914 | 112 583 |
| Operation events | 134 220 | 144 405 |

SC02 останавливал extraction оставшегося constructor body на первом nested
assignment. Поэтому SC03 не только разблокировал прежние 599 scalar operations
и 9 189 references в definitions с gaps, но и извлёк ещё 5 131 scalar operation.
Raw references остались 84 474. Все исполняемые references UnitProXL по-прежнему
immediate; large-scale forward/stale performance этим корпусом не проверяется.

Нулевая доля gaps относится к текущей constructor/type projection этого корпуса.
Это не полная Header V2 parity: member initializers, remaining callable/layout
rules, String IDs и SourceMap требуют самостоятельных slices. Nested defaults
сравниваются по canonical owner, spelling и полному CV; порядок старых string IDs
не воспроизводится и byte parity G пока не заявляется.

## Реализация

Workers извлекают `.member` / `[decimal-index]` в плоскую локальную path arena.
Operation содержит arena/offset/count, original event и origin корневого target.
Assembly сохраняет владельцев arenas и существующий порядок событий.
TypeRequest, canonicalization, numbering и discovery HC01 не изменены.
Никаких WHO, G или глобального semantic environment в workers нет.

Evaluator остаётся последовательным. Для каждого canonical record выбирается
первое завершённое definition. Метаданные содержат complete event и диапазон
исходных requests; индекс собственных fields материализуется при первом traversal
данного типа. Схема недоступна раньше своего complete event. Lookup по base fields
не выполняется, как и в V2. Pointer/reference/cv wrappers не разворачиваются.
Конечный тип должен быть intrinsic без modifiers; nested reference operands V2
не поддерживает, и SC03 воспроизводит соответствующую unsupported diagnostic.

Pending nested operations сохраняются все, в порядке событий. При появлении
поля порядок: direct pending, waiting references, waiting nested. Повторный path
в одном definition заменяет значение. Между definitions новые paths объединяются,
одинаковые значения дедуплицируются, разные дают конфликт у завершающего semicolon.
Приоритет terminal diagnostics: неизвестный direct target, неизвестный nested
root, неразрешённый reference. Physical origin сохраняется от исходной операции.

Если discovery отвергает malformed root, SC03 запускает тот же extractor без
split; это позволяет проверить точную diagnostic незакрытого bracket. Discovery
не менялся. Не поддержанная структурная грамматика явно остаётся gap.

Финальный результат владеет canonical path string и CV. Он не держит request или
path arenas; существующие immutable modifier arenas имеют прежний SB04 lifetime.
Operation вырос с 96 до 112 байт, path step — 24 байта. Это экспериментальный
transport format, не окончательный Direct G layout.

## Парная native-серия

MSVC Release, target 32768, одна подготовленная effective token projection на
процесс. 3 fresh processes для каждого из 1/16 workers; по 5 измеренных пар и
1 исключённой warmup pair. Порядок режимов чередуется. Validation и audit scans
вне таймера. В lifecycle входят discovery, planning, extraction, assembly,
canonicalization, Binding, sequential evaluator, numbering, consumption и
освобождение данных. Builds/tests не выполнялись параллельно с замерами.

Контроль `references02` использует новый 112-byte IR/framework, но nested выключен.
Это парный контроль стоимости категории; не побайтовая копия старого бинарника.
Отдельный frozen SC02 bridge дал reference lifecycle 146,49 мс при 16 workers;
новый control — 149,31 мс. Исторические 147,8 мс не используются как парная база.

Медианы, мс:

| Стадия | SC02 control, 16 workers | SC03, 16 workers |
|---|---:|---:|
| Discovery | 5,12 | 5,26 |
| Extraction | 18,14 | 17,94 |
| Ordered assembly | 12,93 | 15,07 |
| Type Binding | 28,34 | 28,15 |
| Constructor/reference/nested evaluator | 71,44 | 97,54 |
| Model | 135,57 | 163,68 |
| Полный lifecycle | 149,31 | 178,83 |

Lifecycle вырос на 19,77%. Медианы стадий не суммировать как точное разложение
медианы lifecycle. При 1 worker lifecycle 214,61 → 243,30 мс. При 16 workers
диапазоны измеренных lifecycle: control 143,87–170,16 мс; SC03 171,15–201,74 мс.
Это время экспериментальной semantic projection, не production PUBLISH.

## Allocation / lifetime audit

Отдельная инструментированная сборка, 16 workers:

| Метрика | SC02 control | SC03 |
|---|---:|---:|
| Allocation calls | 1 539 927 | 1 786 424 |
| Сумма запрошенных bytes | 656 455 999 | 710 979 520 |
| Peak tracked bytes | 189 043 349 | 208 324 937 |
| Consumer live bytes | 46 020 219 | 46 642 850 |
| Основные retained arrays/strings, bytes | 45 804 492 | 46 427 084 |
| Request instances на границе consumer | 0 | 0 |
| Tracked bytes после освобождения результата | 0 | 0 |

Peak tracked memory вырос на 10,20%: 189,0 → 208,3 MB, десятичные единицы.
Allocation bytes — накопленный объём запросов allocator, не peak RSS.
Retained estimate учитывает capacity основных массивов и heap-backed nested
strings (MSVC SSO capacity 15); consumer live включает дополнительные tracked
allocations. Метрики не следует отождествлять с полным процессным RSS.

## Проверки и воспроизводимость

`ctest`: 3/3 passed. Нормальная и allocation-сборки: по 660 scheduling checks
(1/2/4/8/16 workers, targets 1/32/8192, прямой/обратный порядок). Python:
101 fixture × 3 worker counts × 2 targets = 606 независимых V2 oracle checks.

Проверены multidimensional arrays, arrays of records, late root, overwrite,
все pending nested operations, repeated-definition merge/conflict, scalar leaf
restrictions, inherited-member rejection, malformed paths, depth limit и
приоритет первой diagnostic kind/detail/file/offset/length. Исходные events и
origins дополнительно сравниваются между schedules. Lifetime test уничтожает
Parser requests, paths, tokens и source text до копирования/чтения результата;
проверяются expired path-owner, сохранение path/CV и copy/move с memory churn.

Артефакты: `build/header-semantic-coverage-03/results-nested/`:

- `manifest.json`, frozen executables и `sources/`: SHA256 исходников, exe,
  imported engine objects и project manifest;
- `measurements.csv`, `summary.json`: 60 measured lifecycles + 12 warmup;
- `allocations-1.txt`, `allocations-16.txt`;
- `frozen-sc02-bridge.txt`;
- `Testing/Temporary/LastTest.log` находится в build directory.

`git diff --check` и отдельная проверка whitespace новых файлов прошли.

## Следующий эксперимент

EVALUATOR-SCALING-01: отдельно измерить catalog/index preparation, path/member
lookup, type compatibility, pending/generations и sparse/final output. В рамках
SC03 evaluator не распараллеливался. Сначала определить стоимость частей и
зависимости между records, затем выбирать структуры данных и параллелизм.
Production PUBLISH остаётся вне этого эксперимента.
