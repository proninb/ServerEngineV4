# EVALUATOR-ISOLATION-01 — последовательная изоляция records

Дата: 2026-10-22. Эксперимент поверх SC03, без parallel evaluator, STATE-LAZY
и изменений production PUBLISH. Код: `experiments/evaluator-isolation-01/`.

## Результат

Граница между record-local evaluation и глобальным commit реализована и проверена.
На UnitProXL сохранены все 24 027 definitions без gaps текущей constructor/type
projection: 727 868 construction fields, 84 474 references, 5 403 nested defaults.
WHO, types, события, origins, значения и counters совпадают с исходным SC03;
положительные/отрицательные inputs дополнительно проверены независимым Header V2.

Все record tasks пока выполняются последовательно. Прямой, обратный и hash-
переставленный порядок локального вычисления дают одинаковый ordered commit.
Схемы подготовлены до evaluation и доступны только через const context.
На реальном корпусе построены ровно **160 member indices**, не 24 027.

Изоляция имеет измеренную цену. Парная серия при 16 extraction workers:

| Метрика | Исходный SC03 | ISOLATION-01 |
|---|---:|---:|
| Evaluator, мс | 97,30 | 112,95 |
| Полный semantic lifecycle, мс | 182,23 | 197,88 |
| Peak tracked bytes | 208 324 937 | 234 445 436 |
| Consumer live bytes | 46 642 850 | 46 642 850 |
| Retained result estimate, bytes | 46 427 084 | 46 427 084 |
| Request instances перед consumer | 0 | 0 |

Разница медиан evaluator +16,09%, lifecycle +8,59%, peak +12,54%.
Это архитектурный эксперимент корректности, не заявка на ускорение.

## Контракты реализации

| Стадия | Вход | Изменяемые данные / результат |
|---|---|---|
| `prepare` | batch, canonical reps, bound types | Собственный prepared context; после возврата const |
| `evaluate_one` | Только const batch/types/prepared/canonical и definition ID | Собственный record state и возвращаемый record_result |
| `evaluate_all` | Const context, заданный task order | Отдельный output slot original declaration для каждого record |
| `commit` | Только local results и конечный binding_result | Ordered merge, итоговые vectors/counters и first diagnostic |

`evaluate_one` не получает глобальный binding_result и не пишет в его vectors,
issue или counters. Нет lazy cache updates во время traversal. Общие schemas,
requests, tokens и shapes должны оставаться живы до завершения local evaluation;
после получения local outputs Parser requests и подготовленный контекст могут
быть освобождены до commit. Это проверено отдельными lifetime tests.

`commit` не принимает batch, canonicalizer, requests, source tokens или schemas.
Модель local output владеет normalized member names и nested path strings.
Descriptor хранит canonical owner, original definition ID, begin/complete events
и physical origins. Значения содержат CV и provenance: assignment event, decision
event, physical origin. Для deferred reference resolution сохраняется исходный
assignment event; decision event соответствует появлению referent.

Это экспериментальный transport, не финальный Direct G format. После commit
конечный bound result имеет прежний формат; provenance и local transport здесь
не добавлены в production G.

### Подготовка immutable schemas

Сначала строятся исходные action lists и canonical record metadata с первым
завершённым definition. Для каждого definition сохраняется диапазон собственных
requests. Preparation проходит nested paths и строит индекс только по встреченным
named record types, каждый один раз. На UnitProXL индексированы 160 schemas.

Preparation не генерирует semantic diagnostics. Для некорректного input она может
подготовить потенциально используемый индекс, но решение об ошибке остаётся в
local evaluation с исходным event/origin. Runtime traversal по-прежнему проверяет
`schema.complete <= decision event`. Финальный каталог не разрешает видеть
будущие definitions раньше времени. Поиск остаётся по собственным members,
без автоматического перехода к base fields или разворачивания pointer/reference.

Индексы после preparation читаются через `const prepared&`; флаг indexed больше
не меняется в evaluate_one. Будущим workers не потребуется создавать одинаковые
member indices независимо друг от друга.

### Local state и ordered commit

Pending destinations, generations, waiting references, values и completed nested
results принадлежат одному record. Все правила SC03 сохранены, в том числе:

- latest direct pending operation и все pending nested operations по порядку;
- type check stale reference раньше проверки generation;
- direct pending → waiting references → waiting nested при появлении поля;
- nested overwrite внутри record и terminal diagnostic priority;
- sparse zero semantics и member-binding ordinal без перенумерации.

Завершение local evaluation не публикует глобальные defaults. Результат помещается
в slot исходного declaration ID. Commit обходит slots в прежнем порядке:

1. Объединяет local issue strict-earliest правилом и суммирует counters.
2. Добавляет direct sparse outputs, включая прежнее partial-output поведение
   ошибочных/incomplete definitions SC03.
3. Для успешных complete definitions сравнивает normalized direct values
   с первой definition того же canonical owner.
4. После успешного direct merge объединяет nested defaults по owner/path;
   новые paths добавляются, одинаковые значения дедуплицируются, разные дают
   прежний conflict у complete event/semicolon origin.

Local failure исключает merge данного definition, как и прежде. Порядок задач
не меняет порядок global merge. Использование fixed declaration slots является
контрактом `evaluate_all`/`commit`; commit не сортирует произвольно поданный span.

Parser/Type Binding seed issue сохраняет прежний приоритет при равном event.
Среди равных local events побеждает прежний declaration order; внутри record
сохраняется исходный порядок принятия решения. Сообщение не выбирается по времени
завершения task. Отдельные synthetic tests проверяют оба equal-event случая.

## Пустые containers не оптимизировались

У record сохранены шесть обычных unordered containers и completed_nested map.
Lazy/conditional creation, новый allocator, concurrent index или новый scheduler
не добавлялись. Число record-state containers не сокращалось.

Payload values/waiting расширен provenance для контракта local output. Именами
normalized output теперь владеют strings, а не views в Parser input. Это часть
архитектурной изоляции и её измеренных расходов; ни прирост памяти, ни возможный
локальный выигрыш catalog preparation нельзя приписывать STATE-LAZY.

## Измерения

MSVC Release, target 32768, copied UnitProXL project. 3 fresh processes для каждого
из 1/16 extraction workers; по 5 measured pairs и одной исключённой warmup pair.
Варианты чередуются, оба используют один и тот же input и executable/framework.
Контроль вызывает исходный SC03 resolver. Nested coverage включён в обоих.

Исторические 101,06 / 189,21 мс из EVALUATOR-SCALING-01 сохранены как ориентир
прошлой серии. Новая разница вычисляется только по текущему парному контролю
97,30 / 182,23 мс. Отдельный frozen SC03 bridge дал 94,54 / 175,51 мс, но он
работает другим процессом/executable layout и не используется как paired delta.

Для 15 сопоставленных пар при 16 workers:

- медиана paired evaluator delta: +18,33 мс; диапазон +12,70…+24,81 мс;
- медиана paired lifecycle delta: +15,88 мс; диапазон +7,15…+26,82 мс.

Это другие агрегаты, чем разница двух variant medians. Диапазоны lifecycle:
SC03 168,91–205,79 мс; isolated 184,36–222,58 мс.
При 1 extraction worker evaluator 99,51 → 121,83 мс, lifecycle 241,16 → 261,81 мс.
Evaluator в обеих сериях последовательный: это не проверка его worker scaling.

### Стадии изолированного evaluator, 16 workers

| Стадия | Медиана, мс |
|---|---:|
| Actions preparation и сортировка | 13,05 |
| Canonical metadata / request ranges | 7,20 |
| Preparation 160 schema indices | 1,68 |
| Sequential record-local evaluation | 65,13 |
| Ordered commit | 16,26 |
| Release / прочий residual | 10,56 |
| Evaluator целиком | 112,95 |

Residual считается на каждом trial как evaluator wall минус отдельно измеренные
стадии. Он включает освобождение prepared/local outputs и остальной framework,
не является точным allocation teardown profile. Медианы стадий не складывать
как точную медиану общего времени. Coarse phase clocks не имеют миллионов
вложенных lookup timers из диагностического SCALING-01.

В lifecycle включены discovery, planning, extraction, ordered assembly,
canonicalization, Type Binding, evaluator preparation/local evaluation/commit,
numbering, consumption и освобождение данных. Oracle, order-parity validation,
retained/local-size scans находятся вне измеряемого lifecycle.

### Memory / allocation lifecycle

Отдельная allocation-сборка при 16 workers:

| Метрика | SC03 control | Isolated |
|---|---:|---:|
| Allocation calls за semantic lifecycle | 1 786 424 | 1 949 283 |
| Сумма запрошенных bytes | 710 979 520 | 762 531 463 |
| Peak tracked bytes | 208 324 937 | 234 445 436 |
| Consumer live bytes | 46 642 850 | 46 642 850 |
| Parser request instances у consumer | 0 | 0 |
| Final tracked bytes | 0 | 0 |

Local output capacity estimate до commit: **23 491 040 bytes** (23,49 MB,
десятичные единицы). Учитываются плотные record descriptors/counters, capacities
vectors, normalized owned names и nested paths; MSVC heap-backed strings считаются
при capacity >15. Это transport estimate, не полный RSS и не отдельный peak.

Peak вырос на 26 120 499 bytes. Одновременно живы Parser input, prepared schemas,
все local outputs и структуры ordered commit. Текущий commit копирует normalized
owned values в definitions index; передачу владения/streaming commit здесь не
оптимизировали. После evaluator local outputs и prepared context освобождаются:
consumer live и retained final representation совпадают с SC03. Lifetime результата
не стал зависеть от record-local transport.

## Проверки

`ctest`: **3/3 passed**.

- Обычная сборка: 1 980 scheduling checks против исходного SC03.
- Allocation-сборка: те же 1 980 checks.
- 106 fixtures × 3 worker counts × 2 chunk targets = **636 Header V2 oracle checks**.

В scheduling series проверяются 1/2/4/8/16 extraction workers, targets 1/32/8192,
прямой/обратный порядок chunks и три порядка record tasks. Сравнение exact:
WHO/types, declarations/requests/events/origins, direct CV, nested owner/path/CV,
constructor/reference counters и первое diagnostic. На корпусе дополнительно
проверены все три task orders и reverse extraction.

Negative fixtures включают repeated complete owner, direct definition conflict,
nested merge/conflict, pending targets, stale generations, ошибку несовместимого
referent после declaration и конкурирующие ошибки нескольких records. Они
проверяют и независимый V2 first diagnostic, и точное поведение исходного SC03.

Detached-commit tests освобождают requests, path arenas, tokens, source strings,
canonicalizer и prepared context до commit. Weak owners requests/path expired;
после copy/move и memory churn local results успешно проходят commit. Отдельно
проверены длинные owned names, repeated-definition merge/conflict и сохранение
assignment/decision/physical provenance. Финальный bound result также проходит
прежние SB04/SC03 lifetime checks.

## Артефакты и границы

Финальная серия: `build/evaluator-isolation-01/results-isolation/`:

- `manifest.json`, frozen executables, `sources/`: SHA256 исходников, exe,
  engine objects и project manifest;
- `measurements.csv`, `summary.json`: 60 measured lifecycles и 12 warmups;
- `allocations-1.txt`, `allocations-16.txt`;
- `frozen-sc03-bridge.txt`.

UnitProXL не содержит duplicate complete-owner groups; их корректность доказана
fixtures, а стоимость на больших repeated-definition данных пока не измерена.
Forward/stale references на реальном корпусе по-прежнему не являются нагрузкой.
Границы full Header V2 grammar, String IDs/SourceMap и конечного G не расширялись.
Производственный PUBLISH не менялся.

Следующий отдельный этап — STATE-LAZY-01 на этой изолированной базе. Только после
его собственного парного измерения — PARALLEL-01. Наличие const inputs и local
outputs теперь даёт проверяемый контракт для будущих workers, но само parallel
execution в этом эксперименте не реализовано и не объявляется проверенным.
