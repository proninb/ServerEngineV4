# HEADER-GRAPH-INDEX-01 — 23 октября 2026

**Принято:** growth factor 4× у существующего transient member-name index в G.
Production остаётся на Header/Source V2 с ранее принятым lazy state. Нового индекса,
промежуточной semantic модели или Graph слоя нет.

## Что установил аудит

Capacity уже готовилась один раз на record: 23 810 ensure calls дают лишь 19
allocation/growth events. Проблемой не были allocations на каждое поле.
Однако при росте повторно вставлялись 1 047 893 entries, помимо исходных 727 868.
Два fresh полных PUBLISH и один Header-only дали одинаковые counts.

| Счётчик UnitProXL | 2× reference | 4× candidate |
|---|---:|---:|
| Ensure calls | 23 810 | 23 810 |
| Реальные growth events | 19 | 10 |
| Просмотренные slots при rehash | 2 097 144 | 699 048 |
| Повторные вставки при rehash | 1 047 893 | 349 190 |
| Новые canonical member entries | 727 868 | 727 868 |
| Probes новых вставок | 1 269 686 | 1 071 051 |
| Probes повторных вставок | 1 222 095 | 374 134 |
| Максимальный insert probe count | 40 | 40 |
| Сумма выделенных slots | 4 194 296 | 2 796 200 |
| Final capacity, slots | 2 097 152 | 2 097 152 |

Probe counts включают финальный свободный slot. Lookup probes отдельно не
инструментировались. Counters атомарные и включены только в diagnostic executable;
его wall times не использовались для выбора или оценки оптимизации.

## Изменение алгоритма

При заполнении index capacity выбирается как max(прежняя capacity для required,
4 × old capacity). Порог заполнения остаётся не более 1/2; hash, линейное probing,
key comparison и порядок canonical G append прежние. Меняется только transient
hash-table layout. При overflow guard или отказе большого allocation используется
прежняя меньшая capacity. Старый index не изменяется до успешного rebuild.

4× policy может оставить до 2× slots относительно 2× на другом размере corpus.
На UnitProXL обе policy заканчиваются 24 MiB index; сумма requested buffer bytes
снизилась примерно с 48 до 32 MiB. Это cumulative allocation, не peak RSS.
При нехватке памяти fallback проверен принудительным throw в тестовой сборке.
Graph delta index и persisted index layout не изменяются.

## Same-executable полный PUBLISH

Native MSVC Release /O2 /Ob2 + IPO; два warmup pairs и двадцать measured pairs,
чередование порядка. Один executable без diagnostic counters, полный UnitProXL
с Headers+Sources. Lazy state включён в обеих конфигурациях.

| Медиана | 2× | 4× |
|---|---:|---:|
| Полный PUBLISH, мс | 1324,970 | 1319,060 |
| Header semantic, мс | 209,587 | 201,730 |
| Peak working set, bytes | 2 574 020 608 | 2 573 402 112 |

Медиана парного выигрыша PUBLISH **6,235 мс**, быстрее **18/20**.
Средний парный выигрыш **9,256 мс**, paired bootstrap 95% CI среднего
**[4,175; 15,862] мс**. Header: медиана парного выигрыша **8,238 мс**,
быстрее **20/20**. Outliers не исключались; снижение peak WS не заявляется
как отдельный значимый результат.

## Bridge против принятого V2+lazy executable

Две warmup + двенадцать measured pairs, исходный frozen V2+lazy до изменения Graph
против candidate. Это отдельная серия с двумя executable; её не объединяем с A/B.

| Медиана, мс | Принятый V2+lazy | Новый index growth |
|---|---:|---:|
| Полный PUBLISH | 1339,925 | 1321,785 |
| Header semantic | 210,857 | 202,274 |

Медиана парного выигрыша PUBLISH **10,620 мс**, быстрее **12/12**.
Средний выигрыш **17,238 мс**, 95% CI **[8,382; 31,005] мс**. Среднее чувствительно
к сохранённому outlier 86,11 мс; не выдаём его за ожидаемый универсальный эффект.
Header: медиана парного выигрыша **9,132 мс**, быстрее **12/12**.

Это небольшой подтверждённый выигрыш, не достижение subsecond PUBLISH.
Результаты разных серий нельзя складывать с прежними lazy-state миллисекундами.

## Корректность

Все 72 полных PUBLISH двух timing серий (включая warmups) сохранили оба golden:

- compiled.bin: `78c3e0ee6bdcc7652bd2a0623d6addef656964a1d7ecdb79dfd40b87a2b8eea0`;
- runtime.bin: `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`.

Hashes относятся к тому же full fixture; physical paths входят в compiled provenance.
Канонические members, WHO, String IDs, SourceMap и runtime contents не изменились.

Focused growth/conflict test проверяет 96 records / 5547 members, оба define API,
lookup всех предыдущих records после каждого роста, одинаковые имена у разных
типов, отсутствующие имена, repeated definitions и отказ конфликтующего definition.
Wide-allocation failure/fallback покрыт тем же тестом. Focused и существующий
compiled-project/runtime suite прошли **2/2**. Production frontend suite с
22 semantic contract fixtures также прошёл, без A/B/audit macros.

## Артефакты и область применения

Код policy: `server_engine/project/graph/graph.cpp`.
Benchmark-only controls: `member_index_policy.hpp`.
Воспроизведение: `experiments/header-graph-index-01/README.md`.
Counts, raw logs, frozen EXE, CSV, hashes и CI сохранены под
`build/header-graph-index-01/`. Perf проверен на Windows/MSVC/UnitProXL;
другие corpus могут иметь другую final capacity и выигрыш.
