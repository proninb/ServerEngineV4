# EVALUATOR-SCALING-01 — cost breakdown и dependency audit

Дата: 2026-10-22. Основа — frozen SC03. Эксперимент только диагностический:
алгоритмы, структуры semantic model, production PUBLISH и исходники SC03 не менялись.
Код: `experiments/evaluator-scaling-01/`.

## Вывод

Прямой `parallel_for` вокруг текущего evaluator некорректен: в нём есть общий
ленивый schema cache, merge definitions/defaults, result vectors, counters и first
issue. При этом в проверенной projection не найдено чтения вычисленного значения
другого record. Между records читаются resolved types и schema metadata; pending,
reference generations, ordinal targets и construction values локальны record.
Это поддерживает гипотезу параллельного local evaluation после подготовки
неизменяемых schemas, но не доказывает корректность ещё не реализованного parallel
resolver. Duplicate merge и выбор ошибки потребуют детерминированной сборки.

Нового длинного root внутри evaluator не обнаружено. В текущей реализации
наибольший типичный record занимает 0,6654 мс; per-record timing включает общий
merge и growth result containers, поэтому это ещё не стоимость независимой задачи.
Отдельно доказана избыточная материализация пустого mutable state: 312 351
allocation на создание record containers в allocation-сборке.

## Контроль и методика

Исторические SC03 97,54 мс evaluator / 178,83 мс lifecycle остаются контрольной
точкой той серии. Новая native-серия использует тот же copied UnitProXL project,
MSVC Release, target 32768, заранее подготовленный effective input. Builds/tests
с замерами не пересекались. 16 extraction workers не означают evaluator workers:
evaluator во всех режимах остаётся последовательным.

При 16 workers: 3 fresh processes × 3 measured trials × 5 modes; ещё по одному
warmup trial каждого режима исключено. Порядок modes вращается. Отдельный процесс
при 1 worker даёт по 3 measured trials. Плюс allocation executable и frozen SC03.

| Режим, 16 workers | Evaluator, медиана мс | Lifecycle, медиана мс |
|---|---:|---:|
| Исходный resolver, mode 0 | 101,06 | 189,21 |
| Крупные стадии, mode 1 | 107,22 | 190,86 |
| Таймер каждого record, mode 2 | 109,42 | 194,96 |
| Все вложенные таймеры, mode 3 | 227,86 | 315,16 |
| Подробно 1/32 records, mode 4 | 111,91 | 192,61 |
| Отдельный frozen SC03 executable | 100,90 | 189,61 |

Mode 0 вызывает исходный SC03 resolver напрямую. Его совпадение с frozen bridge
показывает, почему нынешние 189 мс нельзя объявлять регрессией относительно
исторических 178,83 мс: это другая серия, а алгоритм тот же.
При 1 worker mode 0: evaluator 100,56 мс, lifecycle 239,29 мс; не интерпретировать
это как evaluator scaling — меняется только extraction pool.

### Надёжность детализации

Полные вложенные clocks увеличили evaluator более чем вдвое. Поэтому raw timings
коротких lookup НЕ являются точным разложением исходных 101 мс. Выборка уменьшает
общий overhead, но внутри выбранного record стоимость часов остаётся. Умножение
sample times на 32 не исправляет этот эффект и не даёт точной стоимости category.
Инструмент не вычитает произвольную оценку clock overhead и не скрывает residual.

Основные опоры вывода: крупные стадии, inclusive record distribution, точные
operation/allocation counts отдельной сборки и source/runtime dependency audit.
Любая дальнейшая оптимизация обязана измеряться на mode 0/frozen контрольной базе.

## Крупный breakdown

Mode 1, медианы при 16 workers:

| Часть | мс |
|---|---:|
| Action preparation | 10,08 |
| Canonical record catalog metadata | 13,27 |
| Record loop и residual/destruction | 83,03 |
| Final output copy/sort | 2,01 |

Медианы не складывать как точную медиану общего wall time. Residual включает
работу, для которой не включены мелкие clocks, и освобождение временных structures.
Catalog metadata — текущий проход по declarations/requests с заполнением maps
и request ranges; это стоимость реализации, не доказанный архитектурный минимум.

### Детальные наблюдения

Ниже **raw timings mode 3**, то есть части инструментированного evaluator около
227,86 мс, а не исходного resolver. Allocation columns получены отдельной сборкой;
они показывают сумму new calls/requested bytes, а не peak RSS.

| Category | Raw мс | Allocation calls | Requested bytes |
|---|---:|---:|---:|
| Action preparation | 10,08 | 78 160 | 44 996 997 |
| Catalog preparation | 12,79 | 144 178 | 13 424 106 |
| Own/shared member index build | 23,41 | 386 349 | 34 096 973 |
| Empty record state construction | 5,96 | 312 351 | 26 910 240 |
| Record state cleanup | 8,18 | 0 | 0 |
| Event sort/member processing residual | 53,25 | 54 416 | 5 681 244 |
| Member/schema lookup | 8,50 | 0 | 0 |
| Nested traversal exclusive | 0,83 | 5 295 | 169 440 |
| Type compatibility | 6,35 | 0 | 0 |
| Pending/generations lookup/update | 34,35 | 0 | 0 |
| Sparse values/normalization | 16,79 | 165 038 | 32 936 117 |
| Definition/nested merge | 5,88 | 34 733 | 2 839 881 |
| Final output | 2,27 | 5 318 | 1 529 812 |
| Other/global state/residual | 37,44 | 10 | 583 351 |

Всего внутри evaluator в allocation-сборке: **1 185 848 calls / 163 168 161 bytes**.
Это не весь semantic lifecycle и не метрика live memory. Cleanup имеет ноль new
calls, но освобождает buffers/nodes, поэтому ноль в этой колонке не означает
нулевую стоимость cleanup. Raw nested exclusive time исключает вложенные lookup,
index build, compatibility и nested-result writes; это не полная цена категории.

Выборочные clocks дают для state construction около 5,22 мс (4,998–7,366 по
наблюдениям), cleanup около 7,20 мс (5,981–11,453). Это диагностические оценки с
таймерами, не обещанный выигрыш при устранении containers. Для частых lookup
точные времена остаются неустановленными из-за clock overhead. Сырые и выборочные
данные целиком доступны в CSV/analysis.json.

## Что установлено по allocations и lookup

Для каждого record создаются шесть unordered containers (`fields`, `pending`,
`values`, `generations`, `waiting`, `pending_nested`) и `completed_nested` map.
В данной MSVC allocation-сборке это 13 начальных allocations на record:

`24 027 × 13 = 312 351`, ровно наблюдаемое число в state construction.

Это около 26,3% всех new calls внутри evaluator. Даже пустым records выделяется
состояние, включая pending/generation maps. Проверка lazy/conditional state
creation — обоснованный следующий локальный эксперимент, но никакой такой
оптимизации здесь не сделано. Количество allocations не переводится автоматически
в пропорциональный выигрыш wall time.

Детерминированные counts на UnitProXL:

- 378 791 insert в record-local field index;
- 160 построений shared nested member indices, каждый ровно один раз;
- 211 305 member/schema lookup scopes;
- 204 959 type compatibility/reference-kind checks;
- 1 251 455 pending/generation probes: 115 082 `generations.find` и по
  378 791 `pending.find`, `waiting.equal_range`, `pending_nested.find`;
- все 84 474 исполняемых references immediate; forward/stale/pending branches
  проверены fixtures, их large-data performance не измерена этим корпусом.

Ноль pending allocations на UnitProXL объясняет важное ограничение: raw 34,35 мс
нельзя называть стоимостью разрешения forward references. Здесь инструментируются
в основном проверки пустого временного состояния. Аналогично 23,41 мс index build
нельзя интерпретировать как чистую цену 160 shared schemas: эта category включает
сотни тысяч record-local member inserts.

## Распределение records

Mode 2: две отметки времени на record, без вложенных clocks. Таблица — медианы
статистик девяти measured runs при 16 workers; каждый run содержит 24 027 records.

| Статистика | Время |
|---|---:|
| Медиана record | 0,4 мкс |
| p95 | 17,7 мкс |
| p99 | 41,6 мкс |
| Максимум | 665,4 мкс |
| Суммарный inclusive record wall time | 75,70 мс |

Также сохранены per-definition medians по девяти runs: их сумма 67,65 мс. Это
отдельная агрегация, не сумма времени одного реального выполнения. Очень короткие
records близки к разрешению clock, поэтому десятые доли микросекунды не следует
считать точным временем полезной работы.

Максимум per-definition medians — definition 16384, `MCUP21_dat_iPQM_f01M`,
с одним action event. Definition 8192 тоже имеет заметный пик. Положение этих
пиков и код общего definitions index согласуются с growth/rehash общих containers,
а не с длинным семантическим record. Точная attribution каждого такого пика
потребовала бы отдельного per-record allocation trace; её здесь не заявляем.

Таким образом, нет аналога прежнего 48-мс indivisible Header root. Но суммарные
75,70 мс пока включают shared output/merge/cache work и overhead record clocks.
Их нельзя целиком объявить независимой parallel work. Численные granularity lower
bounds в analysis.json иллюстрируют только размеры наблюдаемых записей, не прогноз
ускорения и не оценку времени будущего parallel evaluator.

## Dependency audit

| Данные | Текущее использование | Следствие |
|---|---|---|
| Token/path/shape arenas, batch requests, canonical reps, bound types | Только чтение evaluator | Общие immutable inputs; lifetime до последнего task/merge |
| fields, members, pending, values, generations, waiting, pending_nested, completed_nested | Mutable state одного record | Может принадлежать одному task |
| schemas metadata и schema_definitions | Metadata построены до record loop | Можно читать совместно, сохраняя complete-event filter |
| schemas.members и indexed | Ленивое изменение при traversal | Нельзя считать текущий cache immutable |
| definitions canonical-owner index | Глобальная нормализация/проверка repeated definitions | Нужен ordered merge либо equivalent deterministic protocol |
| committed_nested | Глобальный owner/path merge/conflict | Сохранять прежний порядок и error event |
| result.defaults/nested, counters | Общие записи/appends | Нужны локальные outputs и deterministic reduction |
| result.first | Общий strict-earliest update | Выбирать по semantic event и прежнему tie priority |

Runtime observations одинаковы во всех measured dependency runs:

- traversal обращается к **160 schemas**, **3 173 named-record steps**;
- 160 builds, каждый schema строится один раз;
- **54 schemas** читаются несколькими records;
- наиболее общий schema нужен **161 record**.

Нельзя строить копию индекса этих типов в каждом worker. Также не следует
заранее индексировать все 24 027 records без измерения: сейчас нужны только 160.
Возможны подготовка нужного immutable subset или согласованное once-only build,
но в этом аудите ни один вариант не реализован и не выбран по скорости.

В текущей projection reference operands обращаются к member ordinals того же
record. Nested traversal читает типы/форму другого record, но не его вычисленные
construction values. Источник этой независимости — код модели и проверенные
правила, а не просто отсутствие конфликтов на текущем входе.

На UnitProXL **нет групп repeated complete definitions одного canonical owner**.
Поэтому real-data timing не проверяет стоимость duplicate merge. Fixtures отдельно
проверяют merge новых paths, равные defaults, conflicts, stale generations и
приоритет первой ошибки. При будущем параллелизме local evaluation может быть
независимым, но commit следует воспроизвести в исходном declaration order.

Первое diagnostic нельзя выбирать по завершению worker. Нужно сохранить seed
Parser/Type Binding issue, strict comparison по original semantic decision event,
а при равенстве — существующий приоритет стадии/исходного record/local order.
Physical origin остаётся исходным target/referent, включая pending operations,
которые ошибаются при более позднем declaration/complete event. Замороженный
каталог не отменяет проверки `schema.complete <= decision`: итоговый набор типов
не является разрешением использовать будущие declarations раньше времени.

## Проверки и артефакты

`ctest`: **4/4 passed**:

- 660 scheduling/lifetime checks обычной instrumented-сборки;
- те же 660 в allocation-сборке;
- 606 независимых Header V2 oracle comparisons;
- 8 запусков audit-mode fixtures (5 режимов каждый), включая duplicate merge,
  duplicate conflict, stale generations и global error priority.

Каждый corpus-run сравнивает полный type/CV checksum и первое issue
kind/event/detail/file/offset/length с исходным SC03. Изменения алгоритма, scheduler
и semantic data structures в эксперименте отсутствуют. Production PUBLISH не
менялся; новая архитектура остаётся projection, не полной Header V2/G parity.

Финальные результаты: `build/evaluator-scaling-01/results-final/`:

- `manifest.json`, frozen exe и `sources/`: launch snapshot и SHA256;
- `workers-*/summary.csv`, `records.csv`, `definitions.csv`, `dependencies.csv`;
- `allocations/summary.csv`: allocation attribution отдельной сборки;
- `analysis.json`, `top-records.csv`;
- `analyze-final.py`: точная postprocessing-версия, её SHA256 в analysis.json;
- `frozen-sc03-bridge.txt`.

Исходники timing executable не менялись после финальной серии. Postprocessing
script сохранён отдельно от launch snapshot, поскольку его агрегация дополнена
статистиками суммы real runs. Preliminary `results-audit`/`results-sampled` не
являются финальным контрольным результатом.

## Решение о следующем этапе

Оснований для немедленного parallel_for нет. Сначала минимальный отдельный
эксперимент должен отделить shared schema preparation и ordered merge от
record-local mutable evaluation, доказав ту же parity. После этого проверять
parallel workers без повторного построения одних и тех же indices.

Независимый локальный кандидат — устранение allocations неиспользуемого record
state. Измеренные 312 351 calls делают его обоснованным, но ожидаемый выигрыш
нужно проверить отдельно на исходном SC03 control. Для выбора между lookup
алгоритмами raw мелкие timers недостаточно надёжны; потребуется sampling profiler
или отдельные контролируемые experiments. Текущий аудит не обещает ни кратного
ускорения evaluator, ни PUBLISH менее секунды.
