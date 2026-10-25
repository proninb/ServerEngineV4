# SOURCE-COST-BREAKDOWN-01

Реализован отдельный диагностический executable текущего Source V2. Никаких новых
object databases, operation streams, dispatcher/window или Graph algorithms.
Production файлы не менялись: probes добавлены в генерируемые копии Source V2 и
resolver header. Сборка использует остальные объекты свежего default production
benchmark. WHO, WHERE, visibility, typed paths, G commit и SourceMap сохранены.

## Контроль стоимости наблюдения

Native MSVC Release + IPO, PGO OFF, build-копия UnitProXL. Четыре варианта в каждом
из восьми блоков; один warmup block исключён. Порядок ротируется и разворачивается.
Всего 32 измеренных вызова полного PUBLISH плюс четыре warmup. Все используют
одинаковый coarse publish-timeline, с отключёнными прежними token/member counters.
Во время серии не было builds/tests/trace. После каждого вызова оба persisted
артефакта сверялись с прежними golden SHA256; все 36 вызовов совпали.

Медианы, мс:

| Вариант | Полный PUBLISH | Source |
| --- | ---: | ---: |
| Production baseline без новых probes | 1335,18 | 410,86 |
| Диагностический executable, probes off | 1359,72 | 437,77 |
| Тот же executable, counts | 1362,28 | 441,90 |
| Тот же executable, timed | 2092,33 | 1176,96 |

Медианы парных разностей внутри блока, не разности независимых медиан:

| Сравнение | Полный PUBLISH, мс | Source, мс |
| --- | ---: | ---: |
| off − production | +24,54 | +27,25 |
| counts − off | +4,87 | +2,83 |
| timed − counts | +728,17 | +732,45 |
| timed − production | +755,14 | +764,79 |

Даже режим off не бесплатен: instrumentation оставляет дополнительные проверки,
wrappers и изменённую генерацию кода. Серия не разделяет эти причины. Counts minus
off колеблется, в том числе меняет знак в отдельных парах; нельзя считать +2,83 мс
универсальной константой. Все восемь timed/production пар медленнее; диапазон
парной прибавки полного PUBLISH +735,78…+779,06 мс.

Главный результат аудита: **сплошные clocks на этих коротких операциях сильно
искажают исследуемый Source**. Exclusive accounting устраняет двойной учёт вложенных
интервалов, но не стоимость наблюдателя. Полученные времена нельзя объявить
декомпозицией исходных 410,86 мс или использовать как прогноз ускорения.

## Объём реальной работы

Числа совпали во всех measured counts/timed процессах:

| Граница | Вызовов |
| --- | ---: |
| Cursor advance/peek | 6 472 110 |
| Name binding/identity resolve | 1 423 428 |
| Object type lookup | 946 656 |
| Endpoint read | 946 656 |
| Primitive member/array transitions | 3 103 630 |
| Endpoint path interning | 760 228 |
| Graph write APIs | 717 096 |
| Provenance APIs | 3 995 766 |
| Object declaration handler | 137 584 |
| Assignment handler | 579 512 |
| Scalar parser | 196 470 |
| String assignment handler | 15 899 |

Object lookup выполняется один раз на вызов read_endpoint. Повторный object lookup
внутри каждого шага пути этим измерением не обнаружен. Member count здесь — число
вызовов prepare/append/array helpers, а не число уникальных членов и не доказательство
дублирующего разбора. Один string batch Graph call применяет множество char records.

Cursor включает input.next и interning; binding — find_visible/identities.resolve.
Отдельное чистое время string interning не выделено. Graph writes включают объектные
declarations/extern completions, scalar initializations, links и string batches.
Read-only типовые проверки, scalar compatibility и прочая работа остаются во внешних
syntactic/residual scopes. Это явные границы инструмента, не полное назначение каждой
машинной инструкции одной semantic категории.

## Разбивка instrumented execution

Средние exclusive интервалы timed, мс. Они складываются в средний inclusive Source
root **1174,419 мс**. Идентичность суммы проверена для каждого отдельного запуска.
Это другая статистика, чем медиана coarse Source в таблице выше.

| Категория | Среднее exclusive, мс |
| --- | ---: |
| Endpoint syntax/residual | 244,596 |
| Cursor | 240,648 |
| Graph writes | 123,129 |
| Assignment residual | 109,520 |
| Member/array transitions | 95,338 |
| Provenance | 91,055 |
| Path interning | 83,705 |
| Name binding | 62,575 |
| Source preparation/dispatch/release residual | 48,751 |
| Declaration residual | 24,982 |
| Object lookup | 19,593 |
| String residual | 18,764 |
| Scalar residual | 11,760 |

Например, inclusive read_endpoint = 555,298 мс, exclusive = 244,596 мс; в inclusive
уже входят cursor, object/member lookup и provenance. Их нельзя прибавлять повторно.
Residual scopes также включают bookkeeping вложенных probes и непромеренную работу.
Из этих чисел не следует, что endpoint syntax занимает 244,596 мс в production,
или что удаление object lookup даст 19,593 мс выигрыша.

## Корректность и воспроизводимость

CTest **2/2**. Accounting test проверяет вложенность, повторную категорию,
отключение счётчиков/часов, восстановление стека после исключения и сохранение
reference return value wrapper. Parity test: **36 проверок**, девять fixtures
в baseline/off/counts/timed. Успешные fixtures совпали по compiled/runtime bytes;
ошибочные — по return code и точному stderr после удаления только profiler rows.
Проверены repeated scalar/reference/string writes, nested/array paths, extern,
namespace, invalid member после корректного префикса и undeclared forward target.

Код и команды: `experiments/source-cost-breakdown-01/README.md`.
Raw stdout/stderr, measurements.csv, costs.csv, summary.json:
`build/source-cost-breakdown-01/results/`. Generated source manifest сохраняет SHA
двух исходников; executable hashes записаны в summary.json. Frozen binaries,
исходники инструмента и object manifest сохраняются в build-каталоге.

## Архитектурное решение

Измерены вызовы и стоимость самого инструментирования. **Главный bottleneck
обычного Source пока не доказан.** Оснований вводить WHO/type database вместо двух
плотных Graph accesses не появилось. Приоритеты по instrumented times назначать рано.

Следующий метод для реального распределения CPU — sampling profile (например ETW),
отдельно от этой clock-heavy сборки, либо узкое измерение конкретной крупной операции
с повторным observer control. Затем один минимальный алгоритмический эксперимент и
парный полный PUBLISH. Этот этап не обещает и не демонстрирует ускорение production.
История всех Source writes — отдельный продуктовый контракт; здесь она не реализована.
