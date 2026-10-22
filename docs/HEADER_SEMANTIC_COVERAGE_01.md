# HEADER-SEMANTIC-COVERAGE-01 — аудит и первый отдельный slice

**Статус: полное Header V2 coverage ещё не достигнуто.** Реализован первый
изолированный slice: managed constructors с прямыми scalar operands.
Reference targets, nested paths и другие категории этим результатом не закрыты.
Production PUBLISH остаётся на прежней реализации; новых его измерений нет.

## Почему первым выбран constructor scalar slice

Header V2 разделяет scalar literal parsing, применение operand к известному
полю, pending targets, reference generations и nested paths. Следовательно,
переносить весь constructor parser одним изменением было бы плохой проверкой
новой архитектуры. Начальный slice переносит scalar events и temporal target
resolution, сохраняя явные gaps для зависимых категорий.

Извлечение использует прежние discovery/chunk descriptors и SB04 type shapes.
Workers не получают global semantic environment, не назначают WHO и не строят G.
Constructor events переносятся ordered assembly с original event/origin и
переназначенным definition index. Старый Type Binder не переписан.

## Матрица покрытия относительно Header V2

| Категория | Текущее состояние | Следующая проверка |
|---|---|---|
| Type binding projection, WHO/events | Сохранена HC02; positive corpus сверяется с V2 | Не считать полной семантикой типа |
| Constructor declarations, `= default`, bare `noexcept` | Добавлены | Точные negative diagnostics покрыты fixtures |
| Direct scalar initializer list/body assignments | Добавлены | Values, pending/overwrite, duplicate definitions |
| Reference operands/targets | Явные gaps | Member lookup, forward referents, generations, совместимость, видимость |
| Nested constructor paths | Явные gaps | Record/array traversal и constructor_defaults |
| Member initializers | Явные gaps | Порядок declaration defaults относительно pending constructor values |
| Остальные callables/operators/destructors | Прежние exclusions | Grammar validation и polymorphism effects |
| Layout/inheritance | Прежняя проекция типов/base targets | Access, record flags, polymorphism и полный layout contract |
| Diagnostics | Новые constructor diagnostics проверены точно на fixtures; прежние type checks сохранены | Полный единый контракт всех ошибок |
| String IDs / SourceMap | Не реализованы новой архитектурой | Legacy interning event order, contributions/dependencies и byte parity |

Функции-эталоны: `parse_scalar_constant`, `parse_constructor`,
`apply_record_operand`, `parse_member_initializer`,
`resolve_waiting_references`, `apply_nested_operation`, `parse_method_tail`,
`parse_record` в `server_engine/project/parser/header_parser_v2.cpp`.

## Что реализовано

Поддерживаются signed/unsigned decimal constants, prepared binary64 real bits,
true/false/nullptr, sign и пустой operand. Сохраняется различие CV kind для
numeric `0` и `false`/`nullptr`, а также sign bit `-0.0`.

Input adapter сохраняет уже подготовленные number kind/bits. Binder не
перечитывает токены и не декодирует literals заново. Numeric kind помещён в
прежний padding: `sizeof(token)==48` подтверждено static assertion.

В отдельном evaluator после Type Binding:

- операция над известным полем проверяется сразу;
- для будущего поля сохраняется последнее pending assignment;
- pending применяется при появлении поля;
- unknown target диагностируется при завершении record;
- повторные definitions сравниваются также по normalized constructor values;
- duplicate constructor распознаётся по begin event до более поздней ошибки
  параметров второго constructor.

Decision event ошибки отделён от physical origin сообщения. Например,
несовместимое pending значение проверяется при появлении поля, но сообщение
указывает на constructor operand. Это важно для правильного первого diagnostic.

Финальный результат владеет только sparse `(request index, construction_value)`.
Operation tape и requests удаляются до downstream consumer. CV kind `zero`
может быть не материализован, но unsigned integer `0` сохраняется отдельно.

Если definition содержит хотя бы один gap, **вся definition исключается из
constructor-value parity**. Это консервативный выбор: неподдержанная операция
может изменить позднее значение другого поля. Type projection продолжает
проверяться отдельно. `category_status=partial` прямо отражает это ограничение.

## Реальный coverage audit UnitProXL

| Показатель | Количество |
|---|---:|
| Extracted constructor begin events | 23 920 |
| Extracted scalar operations | 25 477 |
| Reference operand gaps | **84 474** |
| Nested target gaps | **349** |
| Member initializer / body grammar / structural / other operand gaps | 0 |
| Все operation events | 134 220 |
| Definitions без category gaps | 18 566 |
| Definitions с gaps | **5 461** |
| Constructors в definitions без gaps | 18 459 |
| Scalar operations в definitions без gaps | 6 |
| Request-level comparisons construction values с V2 | **349 087** |

Счётчики gaps относятся к операциям, а `5 461` — к definitions; эти показатели
нельзя складывать как разные множества records. Из-за whole-definition exclusion
почти все extracted scalar operations пока тоже остаются вне value parity.
Наличие 25 477 scalar events не означает, что все их эффекты уже проверены.

349 087 — **request-level comparisons**, не число уникальных Graph members.
Отдельная прежняя проверка type projection подтверждает полный инвентарь
727 868 полей и WHO. Она не превращается автоматически в construction parity.

Для этого корпуса следующий фактический приоритет — **reference targets**,
затем nested paths. Нулевой счётчик member initializer gaps в данном входе
не означает поддержки этой категории.

## Цена добавленного slice

Target 32768. Общий executable с двумя режимами: projection control и
constructor events/resolution. Immutable input и persistent pool одинаковы;
режимы чередуются. Три fresh processes при 1 и 16 workers, по пять измеренных
пар и одной excluded warmup pair: 60 измеренных и 12 прогревочных lifecycle.

Медианы 15 измерений на режим, 16 workers:

| Метрика | Projection control | Constructor slice |
|---|---:|---:|
| Discovery, мс | 5,41 | 5,18 |
| Extraction, мс | 14,95 | 16,33 |
| Ordered assembly, мс | 4,87 | **8,93** |
| Прежний Type Binding, мс | 27,94 | 26,65 |
| Новый constructor resolution, мс | — | **10,00** |
| Model, мс | 54,47 | 69,10 |
| Полный semantic lifecycle, мс | **67,07** | **81,90** |
| Retained capacity, байт | 43 039 392 | 43 039 512 |

При одном worker lifecycle: **121,09 → 144,85 мс**. Медианы отдельных стадий
не складываются в точное разложение общей медианы. Lifecycle включает discovery,
planning, extraction, assembly, оба resolution этапа, numbering, consumption и
освобождение данных. Oracle и capacity accounting находятся снаружи таймера.

Constructor resolution этого прототипа обходит records последовательно.
Зависимость порядка обязательна **внутри record**; последовательный обход
независимых records — выбор текущей реализации, а не доказанное архитектурное
ограничение. Этот slice не исследует параллелизацию evaluator и не тюнингует
Type Binder.

Рост assembly при добавлении 134 220 событий подтверждает замечание о цене
переноса локальной семантики. Он измерен, но не является основанием немедленно
заменять ordered assembly concurrent structures.

Frozen HC02 bridge в той же сессии дал медиану **64,87 мс**. Он нужен для
ориентира; эффект slice оценивается по парному common control **67,07 мс**,
а не по историческим 65,85 мс или отдельному bridge process. Общий control
также содержит расширенные типы IR и input adapter, хотя coverage отключено.

## Память и lifetime

Отдельная allocation-сборка, 16 workers:

| Метрика | Projection | Constructor slice |
|---|---:|---:|
| Allocation calls | 597 323 | 788 359 |
| Total requested bytes | 422 843 058 | 501 610 050 |
| Peak tracked requested bytes | 162 244 812 | **171 093 171** |
| Live bytes перед consumer | 43 255 080 | 43 255 200 |
| Живые requests перед consumer | **0** | **0** |
| Tracked live bytes после последнего результата | **0** | **0** |

Peak вырос примерно на **5,5%**, а retained result — всего на 120 байт:
шесть sparse defaults по 20 байт. Большая часть новой цены относится к
временным operation/action structures и формированию результата, а не к
долговременному constructor output. Именно поэтому небольшой финальный
результат нельзя использовать как оценку стоимости всей семантической стадии.

Input/pool startup и OS heap metadata не включены; это requested bytes, не RSS.
Allocation timing не используется. Original request-independent lifetime SB04
сохранён.

## Проверки

Три CTest проходят:

- 240 scheduling comparisons в каждой timing/allocation сборке: 8 inputs ×
  workers 1/2/4/8/16 × targets 1/32/8192 × прямой/обратный порядок задач.
- 210 independent V2 fixtures: 35 inputs × workers 1/4/16 × targets 1/32768.
  Positive cases сравнивают точные construction_value, negative constructor
  cases — kind, полный detail и file/offset/length первого сообщения.
- Проверяются pending overwrite до поля, немедленная ошибка после появления
  поля, aliases/namespaces, pointers/arrays, false vs numeric zero, signed zero,
  real bits, duplicate constructors, duplicate definitions и explicit partial
  status для reference/nested/member initializer gaps.
- В real-corpus процессах сверяются прямой/обратный chunk order, type projection
  и constructor values в definitions без gaps. В allocation runs подтверждены
  нулевые request instances перед consumer и нулевые tracked bytes в конце.

Для прежних type-projection diagnostics сохраняется прежняя category/origin
проверка. Полное совпадение текста **всех** Header diagnostics не заявляется.
Raw rows `projection/not_enabled` не являются проверкой новой семантики;
`constructors/partial` прямо означает незавершённое coverage.

## Следующие отдельные slices

1. Reference targets: сохранить operand names/origins, восстановить temporal
   lookup и поколения forward bindings; добавить независимые negative V2 cases.
2. Nested paths: record/array traversal и sparse constructor defaults.
3. Member initializers: declaration-default/pending-constructor ordering.
4. Остальные callable rules и polymorphism, затем layout/inheritance contract.
5. Полный diagnostics, String ID order и SourceMap parity.

До прохождения этих этапов ни 65,85 мс HC02, ни 81,90 мс текущего partial slice
не являются временем полной Header-семантики или production PUBLISH.

Код и команды: [README](../experiments/header-semantic-coverage-01/README.md).
Raw CSV/stdout/stderr, source snapshots, exe/engine hashes и frozen bridge:
`build/header-semantic-coverage-01/results-constructors/`.
