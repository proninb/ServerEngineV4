# SEMANTIC-BIND-02: Scaling & Memory Audit

Аудит реализован и выполнен. Новые scheduler, Parser, формат записей и constructor/reference binding не включались. Production PUBLISH не изменён.

**Установлены две конкретные проблемы: root-level параллелизм ограничен одним длинным root; промежуточные данные удерживают большие записи, плотный массив пустых ошибок и копию формы типа в результатах.** Allocator тоже выполняет много операций, но его вклад в обычное время отдельно не доказан.

## Как измерено

Два отдельных MSVC Release + IPO executable:

- `SemanticBind02Timing`: контрольный `run()` SEMANTIC-BIND-01 и профилированный вариант с per-root wall intervals / `QueryThreadCycleTime`. Замены allocator и lifecycle atomics здесь нет.
- `SemanticBind02Allocations`: все формы new/delete, отдельный PMR upstream hook, attribution по root/стадии, requested live/peak bytes и счётчики жизненного цикла request. Размер/выравнивание request проверены относительно обычной структуры. Этот вариант существенно вмешивается в выполнение; его времена не используются для вывода о масштабировании.

UnitProXL: 1/2/4/8/16 workers, по три пары control/timing, по одному исключённому прогреву каждого режима на конфигурацию. Затем по одному allocation-прогону на конфигурацию. Всего 30 измерений timing/control, 10 прогревов, 5 allocation diagnostics. Каждый процесс отдельно проверял Header WHO/field-type projection независимым Header V2. Сборки и другие тесты одновременно не выполнялись.

Материалы: `build/semantic-bind-02/results-final`, включая measured executables, source snapshots, hashes, все roots/workers/arrays/allocations/lifetimes CSV и `scaling-summary.json`.

## Почему Parser перестаёт масштабироваться

После контекстного token reuse непустыми остаются **60 из 2060 roots**. Самый длинный — `Types/unity_pro_xl.h`:

- 2 123 811 эффективных токенов: **60,6%** всего входа.
- 418 990 типовых запросов: **55,7%** всех запросов.
- Одна root task, которую текущий Parser выполняет одним worker.

Медианы, мс:

| Workers | Parser control | Parser с профилем | Длинный root | Доля root в интервале Parser |
|---:|---:|---:|---:|---:|
| 1 | 78,979 | 81,296 | 43,583 | 53,61% |
| 2 | 46,578 | 45,836 | 45,762 | 99,80% |
| 4 | 46,209 | 45,285 | 45,219 | 99,85% |
| 8 | 45,621 | 46,664 | 46,584 | 99,89% |
| 16 | 48,468 | 48,711 | 48,644 | 99,89% |

В трёх 16-worker измерениях длинный root начинался через 0,021–0,054 мс после старта Parser. Это уже практически немедленный запуск; перестановка очереди не устранит примерно 49 мс его работы.

На 16 workers медиана tail-интервала между последней задачей worker и завершением Parser — **40,8 мс**. Сумма task wall intervals / (workers × Parser wall) — **20,8%**. Это показатель занятости интервалами задач, **не OS CPU utilization**. Разложение dispatch delay / between-task gap / tail является wall-accounting, а не записью blocked/runnable состояний потоков; внутри task wall time может быть вытеснение или ожидание.

Вывод: **дисбаланс единицы работы доказан непосредственно**. При сохранении «один root — одна task» дальнейшее увеличение числа workers не может распараллелить critical root. Это не исключает дополнительных расходов allocator, памяти или конкуренции, особенно внутри самого длинного root.

### Влияние наблюдателя

Медиана парного `profile parse_ms − control parse_ms`: +4,179 мс на одном worker и +0,611 мс на 16. Для 16 workers диапазон пар −0,871…+1,204 мс. На 4 workers был выброс +11,916 мс. Трёх пар недостаточно для точной оценки малых эффектов или статистического обещания выигрыша.

Поэтому основание вывода — доминирование одного и того же task в каждом прогоне и воспроизведение плато в control, а не сравнение десятых долей миллисекунды. `model_total_ms` профилированного варианта дополнительно содержит memory walks между стадиями; сравнивать его как обычный pipeline total нельзя.

## Что занимает 145,7 MiB

Нативные размеры: `request` 120 байт, `type_expr` 48, `bound_type` 32, `issue` 32, `modifier` 16, symbol 56, declaration 80. Точный layout и padding записаны в `types.csv` / `request-layout.csv`.

Удерживаемый основной набор, ровно как в SEMANTIC-BIND-01:

| Массив | Capacity bytes | MiB |
|---|---:|---:|
| 751 783 TypeRequest | 90 213 960 | 86,035 |
| Bound types | 24 057 056 | 22,943 |
| Per-request failures | 24 057 056 | 22,943 |
| Входные modifier buffers | 5 510 832 | 5,256 |
| Скопированные bound modifier buffers | 5 510 832 | 5,256 |
| Local symbols в общем batch | 1 460 872 | 1,393 |
| Declarations | 1 922 160 | 1,833 |
| **Итого** | **152 732 768** | **145,657** |

Это **203,16 байта на запрос** при условном распределении всей суммы: MiB здесь переведены в байты. Сам request занимает 120 байт; усреднение не является его `sizeof`.

Дополнительно существуют forest adjacency, levels, WHO/representative arrays, временные maps и прочие buffers. Они не входили в прежние 145,7 MiB и учитываются отдельно. Vector headers внутри `request`/`bound_type` уже входят в размеры этих записей: второй раз прибавлять их нельзя.

В request одновременно находятся три local IDs, два spelling views (member и имя типа), event, две physical origins и собственный vector header модификаторов. Часть информации можно получать через definition/token indices; окончательный компактный формат пока не выбран. Удержание token arena ради этих индексов тоже имеет цену и требует отдельного lifetime-контракта.

**Две наблюдаемые избыточности:**

1. `binding.failures` содержит полноценный `issue` для каждого из 751 783 запросов, хотя на корректном корпусе все записи пустые. Для нынешнего результата «первая ошибка» можно исследовать минимум на worker с детерминированным reduction — без плотного массива на 22,9 MiB.
2. Binder копирует `r.type.layers` в каждый `bound_type`. Входные и выходные modifier buffers имеют одинаковые 344 427 элементов. Результат хранит ещё и отдельный vector header на каждый запрос; кроме 5,3 MiB payload это крупная доля массива bound types. Можно исследовать неизменяемую общую форму типа и отдельный resolved leaf.

Это основания для небольших изменений формата, а не уже измеренная экономия после их реализации.

## Время жизни и peak requests

Lifecycle marker считает реальные конструирования/copy/move/destruction в allocation-варианте; moved-from request остаётся живым объектом.

| Граница | Живые request instances |
|---|---:|
| После local Parser | 751 783 |
| После combine, до освобождения worker outputs | **1 503 566** |
| После освобождения worker outputs | 751 783 |
| После binding | 751 783 |

Максимум за весь instrumented pipeline — **1 503 566** во всех пяти allocation-прогонах. Дополнительные transient copies возникают и при росте локальных векторов: parse-only peak при 16 workers достиг 954 941, но он меньше combine peak.

Worker request vectors после разбора имеют capacity 853 190: 102 382 800 байт. Во время combine одновременно существует общий request vector на 90 213 960 байт. Только эти два массива временно занимают **183,675 MiB** capacity. Payload модификаторов при этом перемещён и не продублирован; исходные request objects и их vector headers продолжают жить до `locals.clear()` после всего combine.

Значит, раннее освобождение отдельного root после последнего использования или сегментированный immutable request storage имеют измеримое основание. Это отдельный вопрос от компактности одной записи; ни один вариант в этом аудите не включался.

## Allocations

Один worker, allocation diagnostic; cumulative requested bytes, не одновременно живая память:

| Стадия происхождения | Calls | Сумма requested MiB |
|---|---:|---:|
| Parser | **357 480** | **309,061** |
| Combine/forest preparation | 901 | 90,627 |
| Canonicalization, включая PMR | 24 045 | 2,174 |
| Binding | **553 988** | **77,192** |
| Numbering, включая PMR | 28 | 0,611 |

При 16 workers Parser и Binder имеют те же 357 480 и 553 988 calls. Число операций само по себе не уменьшается от параллельного выполнения. PMR был включён отдельным диагностическим hook: простая замена global new на MSVC пропускала его allocations. Первый exploratory allocation probe без hook не использовался в итоговой матрице.

Глобальный максимум tracked requested live bytes, включая preprocessing input и pool, — около **587,4 MiB**. Это не peak самой модели и не process working set. Здесь не учитываются служебные allocation headers аудитора, native allocator metadata/slack, прямые CRT/OS allocations и mapped file bytes. После явного teardown input/result/pool tracked live bytes вернулись в ноль во всех пяти allocation-прогонах.

Parser/Bind allocation counts — доказанный объём работы. Записанный `malloc_ns` содержит стоимость clock probes; allocation-вариант меняет upstream и использует контендирующие atomics. Поэтому относить разницу native времени к allocator или memory bandwidth по этим цифрам нельзя.

## Проверки и границы

- Timing и allocation executable: по 270 существующих model checks.
- Allocation calibration: aligned new, освобождение в другом потоке/стадии, точный copy/move/destruction request accounting.
- Шесть Header V2-verified audit/control fixtures: согласованность task/worker accounting, array capacities, lifetime boundaries, отсутствие оставшихся tracked allocations.
- Каждая из 45 native matrix observations сверена с независимым Header V2.
- CTest SEMANTIC-BIND-02: 3/3.

Семантическое покрытие осталось прежним: Header type-binding projection, без полной семантики constructors/references/defaults и Direct G. На UnitProXL по-прежнему исключены 29 236 callable/constructor-фрагментов. Никакой новый результат полного PUBLISH или обещание <1 с из этого аудита не следует.

## Следующее решение

Компактное хранение и lifetime проверены отдельно в [SEMANTIC-BIND-03](SEMANTIC_BIND_03.md): sparse diagnostics, shared shape и segmented requests имеют раздельные измерения, включая стоимость следующего потребителя. Для дальнейшего масштабирования Parser потребуется исследовать единицу работы внутри длинного root, сохраняя scope/event context; одна смена очереди задач этого не решит. Расширение constructor/reference semantics остаётся после этой проверки.
