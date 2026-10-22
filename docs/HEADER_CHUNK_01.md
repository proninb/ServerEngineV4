# HEADER-CHUNK-01 — стоимость безопасного разделения root

Первый этап завершён: реализованы и измерены boundary discovery и планирование
участков. Chunk Parser не добавлен. Production PUBLISH и SB04 не изменены.

## Основной результат

На effective input UnitProXL найдено 24 027 целых declarations, покрывающих
все 3 503 734 токена, без fallback. Самый длинный root — прежний
`unity_pro_xl.h`, original root ordinal 4, 2 123 811 токенов.

При target 8192 и 16 workers контрольной модели:

- Boundary discovery всех roots: **5,43 мс**.
- Отдельная проба discovery длинного root: **2,85 мс**.
- Упаковка units/context references в chunks: **0,043 мс**.
- Всего 473 chunks; внутри длинного root — **273**.
- Живой план: **670 686 requested bytes**, примерно **0,64 MiB**.
- Peak allocations самого планирования: **1 092 488 bytes**, примерно **1,04 MiB**.

Проход существенно дешевле whole-root Parser, но сам по себе добавляет работу.
Это достаточная причина проверить разбор по дескрипторам; это ещё не результат
ускорения Parser или полного PUBLISH.

## Что делает проход

Вход уже прошёл effective preprocessing. Проход распознаёт namespace wrappers
и границы целых struct/class/union/typedef declarations, учитывая вложенность
`{}`, `()`, `[]`. Внутренние типы, поля, выражения и тела функций не разбираются.
Модификаторы, requests, identities, WHO и G не создаются.

Unit хранит original root, namespace descriptor и полуоткрытый диапазон
исходных токенов. Namespace descriptor содержит parent index, индекс токена
имени и исходный semantic event. Ни токены, ни накопленный каталог объявлений
не копируются. Семантический порядок пока представляется исходными координатами,
а не итоговым каталогом, который сделал бы будущие объявления видимыми раньше.

Дескрипторы группируются только в пределах одного root/context и при смежных
диапазонах. Большая declaration не разрезается. Неподдержанная namespace-форма,
незавершённая declaration, неверная вложенность или namespace depth больше 128
отменяют **все** tentative boundaries этого root. Для такого root потребуется
сохранить whole-root Parser. Это ограниченная грамматика эксперимента, не
универсальный splitter произвольного C++.

## Гранулярность и цена плана

Медианы пяти измерений на конфигурацию; timing executable MSVC Release + IPO.
Workers относятся к SB04 control: discovery намеренно последовательный.

| Target, токенов | Chunks всего | Chunks длинного root | Самый большой chunk | Discovery, мс | Упаковка, мс | Capacity плана, байт |
|---|---:|---:|---:|---:|---:|---:|
| 1024 | 3909 | 2335 | 2515 | 5,44 | 0,125 | 828 080 |
| 8192 | 473 | 273 | 8190 | 5,43 | 0,043 | 670 608 |
| 32768 | 126 | 66 | 32766 | 5,36 | 0,036 | 659 952 |

Самая большая **неделимая declaration — 2515 токенов**. Поэтому target 1024 —
мягкий предел. Ни один из вариантов не режет declaration ради равного размера.
Количество токенов не используется как подмена critical chunk duration.

`context_ms` измеряет упаковку дескрипторов с уже найденными namespace indices.
Стоимость подготовки полного семантического окружения будущего Parser этим
показателем **не измерена**. В проверяемой реальной проекции UnitProXL namespace
events отсутствуют: все 24 027 declarations глобальные. Вложенные/reopened
namespaces покрыты синтетическими проверками, но реальная цена сложной иерархии
namespace этим корпусом не доказана.

## Контроль SB04 на том же подготовленном входе

Target 8192; пять измеренных итераций и один исключённый прогрев. Это
whole-root Parser и прежний semantic lifecycle, а не chunked реализация.

| Workers | Discovery, мс | SB04 Parser, мс | SB04 model, мс | SB04 lifecycle, мс |
|---|---:|---:|---:|---:|
| 1 | 5,23 | 75,22 | 118,97 | 128,07 |
| 2 | 5,48 | 47,72 | 88,88 | 97,63 |
| 4 | 4,94 | 45,53 | 82,34 | 91,46 |
| 8 | 5,31 | 43,20 | 78,36 | 87,76 |
| 16 | 5,43 | 46,74 | 80,74 | 90,40 |

Плато whole-root Parser воспроизводится. Разница между 8 и 16 workers здесь
не устанавливает оптимальный scheduler: контрольные серии выполнялись в разных
процессах и не исследуют разбор chunks.

Lifecycle включает Parser, combine, canonicalization, Binding, numbering,
удаление requests, одно sequential checksum consumption и удаление результата
с numbering maps. Discovery и упаковка находятся вне этого таймера. Складывать
эти показатели и называть сумму измеренным новым lifecycle было бы неверно:
ordered assembly и реальное chunk scheduling пока отсутствуют.

## Память и lifetime

Отдельный allocation executable, 16 workers, target 8192:

| Показатель | Requested bytes |
|---|---:|
| Peak самого планирования | 1 092 488 |
| Живой план | 670 686 |
| Peak SB04 model + удерживаемый план | 155 460 080 |
| Живые данные на границе consumer, включая план | 43 616 920 |
| После последнего consumer и удаления плана | 0 |

На границе consumer — **0 request instances**. Input, pool startup, OS heap
metadata и mapped/native память не входят в эти числа; это не RSS.
`Capacity плана` и requested live bytes немного различаются из-за небольших
поддерживающих allocations. План намеренно удерживается до consumer, чтобы
показать стоимость этого lifetime без преждевременного освобождения.

## Проверки и воспроизводимость

Два CTest проходят. В каждой сборке выполняются прежние 270 scheduling/semantic
проверок SB04 плюс проверки discovery: forward/definition sequence,
reopened/nested namespace, aliases, pointer-to-array, методы/constructor bodies,
несколько targets, пустые roots и отмена частичного плана при ошибке.

На реальном корпусе **каждый** declaration start/complete сопоставляется с
SB04, каждый namespace parent — с его symbol event. Проверяются число units и
контекстов. Отдельный Header V2 oracle подтверждает SB04 WHO, types и инвентарь
727 868 полей. Во всех 90 итерациях совпадает checksum 751 783 type requests.

Это parity структурного плана и SB04 control. **Parity будущего chunk Parser
по WHO/types/diagnostics/events ещё не доказана.** В частности, этот этап не
измеряет chunk Parser wall time, critical chunk duration и новый chunked
semantic lifecycle. Необходимый Parser context пока предложен как ссылки на
namespace/event descriptors, а его фактическое потребление не реализовано.

Серия: 1/2/4/8/16 workers × 1024/8192/32768 targets, по пять измерений и одному
прогреву — 75 измеренных и 15 исключённых итераций в 15 fresh processes.
Дополнительно три allocation processes. Native, последовательно, без
одновременных сборок или тестов; оригинальный UnitProXL не изменяется.

Исходники и команды: [README](../experiments/header-chunk-01/README.md).
Raw CSV, stdout/stderr, source snapshots, executables и SHA256 engine objects:
`build/header-chunk-01/results-final/`. Предварительная прерванная серия
`results-boundaries` в итоговые числа не включена.

## Решение следующего этапа

Для проверяемой проекции структурный проход не повторяет Parser и стоит
примерно 5,4 мс на весь вход. Не обнаружена необходимость копировать полный
каталог типов при планировании. Поэтому следующий изолированный прототип —
разбор declarations по этим descriptors с сохранением original root/events,
ordered assembly, temporal Binding и сравнением с SB04/V2 при разных порядках
запуска. Только он покажет, достаточно ли такого контекста и перекрывает ли
ускорение workers стоимость discovery, assembly и дополнительных allocations.
