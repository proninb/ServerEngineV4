# STATE-LAZY-01 — отложенное создание record-local containers

Дата: 2026-10-22. Изолированный эксперимент поверх EVALUATOR-ISOLATION-01.
Production PUBLISH, WHO, Parser, Type Binding и конечный G не менялись.
Код: `experiments/state-lazy-01/`.

## Результат

Критерии STATE-LAZY-01 выполнены на UnitProXL: allocations уменьшились,
636 независимых V2 oracle checks сохранены, exact parity с ISOLATION-01 пройдена,
retained result не вырос, выигрыш подтверждён полным semantic lifecycle.

Парная native-серия, существующий frontend/Type Binding pool 16 workers;
evaluator остаётся последовательным:

| Метрика | CONTROL — ISOLATION-01 | LAZY | Изменение |
|---|---:|---:|---:|
| Evaluator, медиана мс | 116,29 | 99,29 | −14,62% |
| Local evaluation, медиана мс | 68,73 | 53,89 | −14,84 мс |
| Полный semantic lifecycle, медиана мс | 200,74 | 183,82 | −8,43% |
| Allocation calls | 1 949 283 | 1 659 141 | −290 142 / −14,88% |
| Сумма запрошенных bytes | 762 531 463 | 735 941 487 | −26 589 976 |
| Peak tracked bytes | 234 445 436 | 234 445 436 | 0 |
| Consumer live bytes | 46 642 850 | 46 642 850 | 0 |
| Final retained estimate, bytes | 46 427 084 | 46 427 084 | 0 |
| Local-output capacity estimate, bytes | 23 491 040 | 23 491 040 | 0 |

Allocation metrics получены отдельной сборкой, а не timing executable.
Peak не снижается: ненужные пустые containers раньше жили внутри одного record,
а максимум этого lifecycle возникает позднее с полным local output/ordered commit.
Allocation bytes — накопленный объём запросов, не live memory и не peak RSS.

## Строго ограниченное изменение

CONTROL вызывает исходный `ei01::resolve` с включённой изоляцией. LAZY переиспользует
`ei01::prepare`, `prepared`, `record_result`, provenance и `ei01::commit` напрямую.
В evaluate_one изменены только семь объявлений containers на `lazy_container<T>`.
Текст функции после обратной замены этих семи declarations совпадает с исходным;
текст evaluate_all совпадает после снятия квалификации вызываемого evaluate_one.
Это проверено отдельным source-scope comparison.

Не менялись lookup/index algorithms, hash/comparison functions, bucket/load policies,
allocator, reserve, schema selection, temporal checks, normalization, commit,
порядок задач или формат local/final output. Параллелизм evaluator не добавлен.

Adapter хранит `std::optional<T>` inline в record state:

- default construction не создаёт T;
- empty()/size()/find()/equal_range()/iteration пустого state не создают T;
- первая запись вызывает обычный default constructor T, затем прежнюю operation;
- созданный T после erase последнего элемента остаётся созданным до конца record;
- никакой отдельной heap allocation для wrapper object нет.

После materialization lookup выполняет тот же стандартный контейнер. Для ещё не
созданного контейнера чтение даёт missing/empty; это часть lazy-state contract,
не новый алгоритм поиска. Adapter предназначен для текущего local evaluator:
итераторы disengaged state не должны переживать первую mutation. Все места его
использования проверены; обычные iterator/range/erase правила после создания T
сохраняются. Это не универсальная STL replacement API.

## Фактический источник 13 allocations

Локально проверены headers MSVC toolset 14.51.36231 / compiler 19.51 Release:

- `xhash`: default `_Hash` создаёт `_List` и `_Vec`, `_Min_buckets = 8`, затем
  `_Vec._Assign_grow(_Min_buckets * 2, ...)`;
- `list`: выделяется head/sentinel node;
- `xtree`: default tree выделяет head/sentinel node.

То есть каждый unordered container имеет два начальных allocations: sentinel и
bucket vector. std::map имеет один — tree sentinel. Пользовательского reserve
или нестандартного начального bucket count в record state нет.

`StateLazy01Allocations --container-probe` проверяет фактические семь типов:

| Container | CONTROL empty calls | CONTROL empty bytes | LAZY empty/read calls | CONTROL write calls | LAZY first write calls |
|---|---:|---:|---:|---:|---:|
| fields | 2 | 168 | 0 | 1 | 3 |
| pending | 2 | 168 | 0 | 1 | 3 |
| values | 2 | 200 | 0 | 1 | 3 |
| generations | 2 | 160 | 0 | 1 | 3 |
| waiting | 2 | 200 | 0 | 1 | 3 |
| pending_nested | 2 | 184 | 0 | 2 | 4 |
| completed_nested | 1 | 112 | 0 | 1 | 2 |

First write LAZY равен CONTROL construction + тому же write по calls и requested
bytes; это проверяется assertions, а не только выводится в лог. Дополнительный
allocation у pending_nested write — buffer вложенного vector при push_back.
Контейнеры, которые реально используются, по-прежнему платят обычную цену T.
Убирается создание тех, которые остаются ненужными.

Для текущих ISOLATION-01 payloads пустое состояние — 13 calls / 1 192 bytes на
record; 24 027 records дают 312 351 initial calls / 28 640 184 requested bytes.
Прежние 26,9 MB в SCALING-01 относились к SC03 payloads до добавления provenance
в ISOLATION-01. Число containers/calls осталось тем же; node sizes выросли.

Весь lifecycle потерял 290 142 calls / 26 589 976 requested bytes. Это меньше
всех 312 351 initial calls, поскольку необходимые containers всё равно создаются.
Оставшаяся память/allocations output, schemas и commit этим экспериментом не
оптимизировалась.

## Семантика и lifetime

На UnitProXL в обоих режимах:

- 24 027 complete definitions без gaps текущей projection;
- 727 868 construction-field comparisons с Header V2;
- 84 474 reference operations, 30 608 scalar operations;
- 5 403 nested defaults и 112 583 direct sparse defaults;
- ровно 160 prepared immutable schema indices;
- одинаковые WHO/types, events/origins, diagnostic kind/detail/event/origin,
  полные construction values и reference counters.

В evaluate_one меняется только существование backing container. Оставлены
прежние pending/generation update order, stale compatibility-before-generation
check, source/decision provenance, overwrite и nested traversal rules.
Ordered commit и equal-event priority непосредственно переиспользуются из EI01.

636 V2 oracle checks включают pending/forward/stale cases, repeated complete
owners, direct/nested merge/conflict и конкурирующие ошибки разных records.
Scheduling tests проверяют direct/reverse/permuted local tasks и reverse chunks.
Сохранены detached commit после освобождения requests/source/path arenas,
длинные owned names, copy/move/churn и исходный bound-result lifetime.
Consumer имеет 0 request instances, final tracked allocations после release — 0.

На реальном UnitProXL references по-прежнему immediate. Выигрыш на больших
forward/stale-heavy inputs пока не измерен: при реальном использовании maps
LAZY материализует их обычным способом и добавляет проверку состояния. Этот
результат не объявляет улучшение любой возможной Header workload.

## Методика и paired evidence

MSVC Release, target 32768, copied project из
`build/publish-subsecond-01/fixture/project.json`. Input preparation и oracle
вне таймера. Native execution вне sandbox, без builds/tests одновременно.
3 fresh processes при каждом из 1/16 workers, по 5 measured pairs + 1 excluded
warmup pair. Варианты чередуются. Workers — прежний pool frontend/Type Binding,
не evaluator worker count. Ни один record здесь не вычисляется параллельно.

Исторические ISOLATION-01 112,95 / 197,88 мс не используются вместо новой paired
базы 116,29 / 200,74 мс. Отдельный frozen ISOLATION executable дал 111,10 / 194,55 мс;
это другой процесс/code layout и только дополнительный bridge, не paired delta.

15 paired observations при 16 workers:

| Paired LAZY − CONTROL | Медиана, мс | Диапазон, мс | LAZY быстрее |
|---|---:|---:|---:|
| Evaluator | −14,56 | −24,04…−10,27 | 15/15 |
| Local evaluation | −12,61 | −22,29…−11,23 | 15/15 |
| Полный lifecycle | −15,16 | −22,78…−2,34 | 15/15 |

Разница variant medians и медиана paired differences — разные агрегаты.
Ranges lifecycle: CONTROL 182,17–206,82 мс, LAZY 169,12–191,76 мс.
При pool 1 worker evaluator 117,93 → 101,88 мс, lifecycle 262,34 → 243,33 мс.
Это дополнительное наблюдение при другой подготовке, не evaluator scaling.

### Coarse stages, 16 workers, медианы мс

| Стадия | CONTROL | LAZY |
|---|---:|---:|
| Action preparation | 12,33 | 12,81 |
| Canonical metadata | 6,92 | 6,83 |
| 160 immutable schema indices | 1,62 | 1,61 |
| Local evaluation | 68,73 | 53,89 |
| Ordered commit | 15,11 | 14,34 |
| Release / residual | 10,18 | 9,78 |
| Evaluator wall | 116,29 | 99,29 |

Медианы не складывать как точную общую медиану. Алгоритмы preparation/commit
не менялись; различие их наблюдаемых timings не означает самостоятельной
оптимизации этих стадий. Policy меняет allocation traffic и состояние heap/cache,
а замеры также имеют обычную вариацию.

В полный lifecycle входят discovery/planning, extraction, assembly,
canonicalization, Type Binding, весь evaluator, numbering, consumption и release.
Size scans и exact order validation находятся вне таймера. Allocation-сборка
не используется для timing conclusions.

## Проверки и воспроизводимость

`ctest`: **4/4 passed**:

1. 1 980 scheduling/lifetime checks обычной сборки против CONTROL.
2. Те же 1 980 checks allocation-сборки.
3. 636 независимых Header V2 oracle checks.
4. Empty-state/first-write allocation probe семи container types.

Дополнительные adapter tests проверяют empty find/range без materialization,
const lookup, duplicate try_emplace, erase/range и запись после последнего erase.
Финальные artifact hashes и new-file whitespace проверены; `git diff --check`
прошёл. Исходники EI01/SC03 и production code не менялись в этом этапе.

Артефакты: `build/state-lazy-01/results-lazy/`:

- `manifest.json`, frozen executables и `sources/` с SHA256;
- `measurements.csv`, `summary.json`, `paired-analysis.json`;
- `container-probe.txt`, `allocations-1.txt`, `allocations-16.txt`;
- `frozen-isolation-bridge.txt`;
- `scope-check.txt` с результатом ограничения diff семью declarations.

## Следующий этап

EVALUATOR-PARALLEL-01 на этой базе: evaluator pool 1/2/4/8/16 при фиксированных
16 workers существующего frontend/Type Binding. Сохраняются 160 immutable indices,
private record state/output, fixed declaration slots и sequential deterministic
commit. Нужны paired sequential control и полный lifecycle, а не только локальный
kernel. Сам parallel evaluator в STATE-LAZY-01 не реализован.

Full Header V2 grammar, String IDs/SourceMap и конечный G по-прежнему вне текущей
projection. Production PUBLISH не ускорен этим изолированным экспериментом.
