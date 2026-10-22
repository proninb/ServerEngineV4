# HEADER-SEMANTIC-COVERAGE-02 — reference operands и targets

Reference slice реализован. Definitions без gaps на UnitProXL выросли
**18 566 → 23 678 из 24 027**. Все **84 474** reference operands извлечены и
классифицированы: **75 285** разрешены и проверены против Header V2,
**9 189** остаются исключёнными вместе с definitions, содержащими nested targets.
Remaining gaps — **349 definitions**. Полное Header V2 coverage ещё не достигнуто.

Production PUBLISH не изменён. TypeRequest, canonicalization, WHO и алгоритм
ordered assembly не переписывались; global concurrent reference index не добавлен.
Evaluator по-прежнему последовательно обходит records.

## Воспроизведённые правила Header V2

Эталон: `parse_reference_name`, `apply_record_operand`,
`resolve_waiting_references` и завершение `parse_record` в
`server_engine/project/parser/header_parser_v2.cpp`.

Reference resolution и constructor evaluation выполняются в одном порядке
событий. Две независимые формы отложенного разрешения не смешиваются:

1. **Future destination**: операция ещё не имеет поля назначения. Pending map
   сохраняет последнее присваивание этому имени. Заменённая операция ещё не
   создала waiting reference и не обязана разрешаться позднее.
2. **Future referent**: destination уже существует и имеет reference type,
   но operand пока не является объявленным member. Создаются generation и
   waiting entry, значение destination сбрасывается в zero.

Успешный scalar или immediate reference override увеличивает существующую
generation. Когда referent появляется, Header V2 сначала проверяет типы и
только потом generation. Поэтому:

- stale waiter с совместимым типом удаляется, не меняя нового значения;
- stale waiter с несовместимым типом всё ещё вызывает ошибку;
- stale waiter с несуществующим именем остаётся ошибкой при завершении record.

Эти особенности не заменены более привычной моделью «override отменяет всё».
При завершении record pending destination errors имеют приоритет над unresolved
referent errors, как у V2. Decision event ошибки отделён от physical origin
старого operand: поздняя проверка может указывать на более раннее выражение.

Destination должен иметь outer lvalue/rvalue-reference modifier. С source
снимается один outer reference modifier; затем сравниваются canonical entity,
builtin и shape. Дополнительные C++ qualification conversions не добавлены:
например, Header V2 требует точного совпадения const qualifiers.

Final `construction_value::member_binding` хранит **one-based member ordinal
внутри record**. Это не request index, не WHO и не транзитивно вычисленный
referent чужой reference field. CV operand включён в checksum и V2 comparison.

## Извлечение и представление

Workers сохраняют destination name, reference name, original event и отдельные
origins destination/referent. Поддержаны `name` и `this.name`; синтаксис и
ошибки `this->name`/неполного `this.` воспроизводятся по V2.

Накопленного global semantic environment в workers нет. Assembly по-прежнему
восстанавливает исходный порядок и переназначает definition indices. Operation
record расширен **64 → 96 байт** для reference name/origin; это новый payload,
а не оптимизация или изменение TypeRequest. Старые SC01/HC02 файлы не изменены.

Pending targets, waiting names и generations локальны record. Сравнение типов
использует уже bound types и immutable shapes; повторного разбора токенов нет.
Final sparse defaults владеют CV values и request indices, не operation tape.
Requests и operation records освобождаются до consumer.

## Coverage UnitProXL

| Метрика | Scalar slice | Reference slice |
|---|---:|---:|
| Definitions без gaps | 18 566 | **23 678** |
| Definitions с gaps | 5 461 | **349** |
| Constructors в definitions без gaps | 18 459 | 23 571 |
| Scalar operations в definitions без gaps | 6 | **24 878** |
| Request-level V2 construction comparisons | 349 087 | **684 441** |
| Final non-zero-kind sparse defaults | 6 | 97 914 |

Исходный event inventory сохранился: 23 920 constructor begin events,
25 477 scalar operations и 134 220 operation events всего.

Classification reference operands:

| Категория | Операций |
|---|---:|
| Извлечены как reference operations | **84 474** |
| Обработаны в definitions без gaps | **75 285** |
| Разрешены immediate member lookup | **75 285** |
| Извлечены внутри оставшихся gapped definitions | **9 189** |
| Остались не классифицированы | **0** |

Проверено равенство `84 474 = 75 285 + 9 189` до измерения и во всех matrix rows.
Последние 9 189 **не выдаются за разрешённые**: вся их definition консервативно
исключена из value parity из-за nested targets. Аналогично 599 scalar operations
пока остаются вне этой проверки. 349 nested gaps совпадают с числом remaining
definitions данного корпуса; gaps по другим категориям здесь не встретились.

На этом реальном входе queue/pending/stale-generation counters равны нулю:
все исполняемые ссылки immediate. Следовательно, corpus benchmark не доказывает
их масштабирование или стоимость. Корректность forward/generation правил
подтверждена отдельными V2 fixtures.

684 441 — request-level comparisons, не число уникальных Graph members.
Прежняя независимая type-projection проверка подтверждает инвентарь 727 868
полей и WHO; её нельзя подменять construction parity исключённых definitions.

## Проверки

Три CTest проходят:

- **510 scheduling comparisons** в каждой timing/allocation сборке:
  17 входов × workers 1/2/4/8/16 × targets 1/32/8192 × прямой/обратный enqueue.
  Exact comparison включает WHO, types, symbol/declaration/request events,
  origins, destinations, kind/detail/origin первого diagnostic и CV values.
- **414 independent Header V2 fixtures**: 69 входов × workers 1/4/16 × targets
  1/32768. Поддержаны positive/negative known target, future destination,
  future referent, повторное присваивание, pending overwrite, late type mismatch,
  stale mismatch/unresolved, multiple waiters, reference-to-reference,
  const/array/rvalue-reference shapes, `this.name`, duplicate definitions.
- Для новых constructor/reference failures сверяются kind, полный detail и
  file/offset/length. Для приоритета существующих type-projection failures
  сохранена прежняя category/origin проверка. Полный контракт текстов всех
  Header diagnostics остаётся отдельной категорией coverage.
- Fixture driver отдельно подтверждает, что queued/stale/pending/superseded
  branches действительно исполнились, а не только присутствуют в коде.
- В real-corpus процессах выполняются exact forward/reverse scheduling
  comparison, независимый V2 oracle и полные CV comparisons для definitions
  без gaps. Измеренные checksums стабильны при 1 и 16 workers.
- Result lifetime проверен после уничтожения requests, tokens и исходного
  текста, после move/copy и heap churn. Последняя shape arena освобождается
  после последнего consumer; member operand сохраняется.

## Дополнительная стоимость

MSVC Release + IPO, native последовательно, target 32768. Три fresh processes
при 1 и 16 workers; по пять измеренных пар и одной excluded warmup pair:
**60 измеренных и 12 прогревочных lifecycle**, два allocation processes и
отдельный frozen SC01 bridge. Сборки/тесты одновременно с measurements не шли.

`scalar01` — common control этого executable: constructors enabled,
references disabled. Оба режима используют один immutable input, persistent
pool, новый IR и evaluator framework. Исходный frozen SC01 — bridge,
а не парный контроль цены расширения.

Медианы 15 измерений на режим при 16 workers:

| Метрика | Scalar common control | Reference slice |
|---|---:|---:|
| Discovery, мс | 5,40 | 5,36 |
| Extraction, мс | 17,34 | 17,27 |
| Ordered assembly, мс | 11,76 | 12,15 |
| Прежний Type Binding, мс | 29,04 | 29,07 |
| Constructor/reference evaluator, мс | **13,26** | **68,70** |
| Model, мс | 79,06 | 134,32 |
| Full semantic lifecycle, мс | **91,92** | **147,79** |
| Retained capacity, байт | 43 039 512 | 45 804 492 |

При одном worker lifecycle **152,87 → 208,88 мс**. Диапазон reference lifecycle
при 16 workers: **141,40–156,02 мс**. Медианы стадий нельзя складывать как точное
разложение общей медианы.

Новый evaluator stage включает не только name lookup: формирование per-record
индексов, scalar operations ранее исключённых definitions, reference type
checks, pending/generation state, normalizing и сборку sparse CV output.
Поэтому 68,70 мс не объявляются неизбежной стоимостью самих references.
Records пока обходятся последовательно; внутри record порядок обязателен,
глобальная последовательность между независимыми records — выбор прототипа.
В этом slice evaluator не распараллеливался и не оптимизировался.

Frozen SC01 bridge дал **83,61 мс** в той же сессии. Common control тяжелее
старого SC01: он уже содержит расширенный IR и новый per-record state framework.
Поэтому эффект включения references сравнивается с **91,92 мс**, а не с
историческими 81,90 мс или отдельным bridge. Это не измерение production PUBLISH.

Full lifecycle включает discovery/planning, extraction/assembly, Type Binding,
совместный evaluator, numbering, consumption и освобождение всех результатов.
Oracle и capacity accounting находятся снаружи. Preprocessing не включён,
как в SC01/HC02; вход одинаковый для paired modes.

## Память и освобождение

Отдельная allocation-сборка, 16 workers:

| Метрика | Scalar common control | Reference slice |
|---|---:|---:|
| Allocation calls | 862 633 | 1 468 623 |
| Total requested bytes | 536 721 010 | 635 895 387 |
| Peak tracked requested bytes | **175 517 331** | **186 821 213** |
| Live bytes перед consumer | 43 255 200 | 46 020 219 |
| Request instances перед consumer | **0** | **0** |
| Tracked live bytes после последнего результата | **0** | **0** |

Peak вырос примерно на **6,4%** относительно common control. Более высокая
стоимость формирования данных измерена, но source-level allocation causes
отдельным profiler здесь не атрибутируются. Input/pool startup и OS heap metadata
не включены; requested bytes — не RSS. Instrumented duration не используется.

Final CV output вырос, потому что reference bindings теперь материализованы,
но Parser requests и operation tape не удерживаются до downstream consumption.
Retained capacity исключает numbering maps и allocator metadata.

## Следующий slice

Следующий — **nested targets**: remaining 349 definitions одновременно
блокируют 9 189 references и 599 scalar operations. После него — member
initializers и оставшиеся callable/layout rules, полный diagnostics,
String ID order и SourceMap parity. Эти категории не объявляются поддержанными
только потому, что их gaps не встречаются на текущем корпусе.

До полного покрытия не меняются production PUBLISH и Direct G. Ни 65,85 мс
HC02, ни 147,79 мс этого partial reference slice не являются временем полной
Header-семантики или production PUBLISH.

Код, контракты и команды: [README](../experiments/header-semantic-coverage-02/README.md).
Raw CSV/stdout/stderr, source snapshots, exe/engine hashes, allocation outputs
и frozen bridge: `build/header-semantic-coverage-02/results-references/`.
