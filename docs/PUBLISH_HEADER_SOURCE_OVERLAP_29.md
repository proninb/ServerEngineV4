# Header/Source overlap в PUBLISH

Обновление шага 30: default PUBLISH теперь получает готовые tokens/hashes
непосредственно от lexer. При доступности этого буфера повторный decode/hash
из данного pipeline больше не выполняется. См. `PUBLISH_SOURCE_LEXER_TOKENS_30.md`.

Добавлен `SERVER_ENGINE_HEADER_SOURCE_OVERLAP_V2` (по умолчанию OFF).
После физической подготовки Header include closure, до Header semantics,
owner захватывает read-only Source descriptors и запускает подготовку Source
на отдельном coordinator с существующими execution lanes.

```text
prepared Header closure → Header preprocessing/semantics → готовые типы G
                         Source decode/hash[/syntax]    → join
                                                        → Source binding/commit
```

## Контракт владения

Worker читает только заранее захваченные spans исходных байтов и lexical words.
Он не читает меняющиеся метаданные File Context, string_table, identity_space,
Graph или Source Map. Header V2 использует собственную prepared closure и меняет
семантическое состояние и dependency topology; исходные байты и исходные lexical
arenas до join остаются неизменными. Encoded baseline words также неизменны.
В worker не выполняется string interning; даже optional PREBIND не читает общую
таблицу строк при overlap. Полные string_id/identity_ref назначает owner.

При обычной конфигурации подготовка включает decode и prehash. При дополнительных
`SERVER_ENGINE_SOURCE_MODEL_V2=ON` и `SERVER_ENGINE_SOURCE_MODEL_DIRECT_V2=ON`
она также строит существующую синтаксическую модель, операции которой затем
потребляет owner. Эти два флага не включаются автоматически.

Join выполняется перед Source binding и при любом раннем выходе из Header.
Ошибка speculative preparation оставляет canonical cursor fallback; первая
Header/Source ошибка и порядок Source declarations/commit сохраняются.
`identity_ref`, Graph и формат двух артефактов не изменены.

## Учёт времени

Добавлены snapshot/prepare/wait telemetry. Snapshot — захват descriptors на owner,
prepare — wall time worker, wait — ожидание перед Source binding. Эти интервалы
нельзя складывать со стадиями: prepare перекрывается с Header.
`source_ns` включает join, replay и уничтожение подготовленных токенов, включая
выходы по ошибке. Общий `total_ms` остаётся главным показателем ускорения.

Первая реализация копировала исходные байты и lexical words: копирование заняло
12–17 ms при подготовке около 10 ms. Она заменена захватом immutable spans;
её диагностические результаты сохранены в `owned-snapshot/`, в итоговую серию
они не включены.

## Проверки и результат без Source Model

MSVC Release IPO, одинаковые новые исходники с overlap OFF/ON, копия UnitProXL.
До замеров зафиксированы 8 ABBA/BAAB blocks плюс один исключённый прогревочный
блок. Во время замеров сборки и тесты не запускались. После каждого PUBLISH
сверены SHA-256 обоих файлов и выполнен audit baseline executable.

| Показатель | Baseline | Overlap |
|---|---:|---:|
| Обычный PUBLISH, 16 измерений каждого | 1428.253 ms | 1412.293 ms |
| Stage total, 2 пары | 1460.920 ms | 1418.140 ms |
| Stage Header | 226.549 ms | 231.298 ms |
| Stage Source | 494.360 ms | 461.087 ms |

Средний выигрыш на блок: 15.961 ms; Student-t 95% CI **[-6.096, 38.017] ms**,
7/8 блоков в пользу overlap. Интервал включает регрессию, поэтому флаг остаётся
OFF. Не следует выдавать более сильный stage результат за доказанный выигрыш.
В stage серии захват descriptors занял 0.168 ms, worker — 9.876 ms,
ожидание — 0.008 ms. Подготовка действительно скрыта за Header, но это около
10 ms работы, а не вся Source стадия и не вся Header стадия.

Все 42 полных запуска (36 ABBA + 6 stage, включая прогревы) сохранили эталонные
SHA-256 и прошли audit. Основная конфигурация прошла 22/22 CTest, overlap —
frontend regressions с проверкой фактического запуска preparation, девять
интеграционных сценариев и 13 точных сравнений ошибочной диагностики, включая
ошибки одновременно в Header и Source.

Исходники и интерфейс: `parser.cpp`, `source_parser_v2.cpp/.hpp`.
Бинарные файлы, скрипты, CSV и `analysis.json`:
`build/publish-header-source-overlap-29/`.

## Синтаксические операции

Дополнительно проверена конфигурация overlap + SOURCE_MODEL + MODEL_DIRECT.
Frontend regression отдельно подтверждает, что не менее 260 statements берутся
из построенных операций, а не возвращаются в token replay. На UnitProXL
`source_model_direct_assignments=563613`: все 196469 scalar assignments и
367144 links проходят direct validation.

Она прошла frontend regressions, девять интеграционных сценариев и 13 точных
сравнений диагностик. Все шесть полных stage запусков с прогревами сохранили
эталонные SHA-256 и прошли baseline audit. Две измеренные пары: total
1461.560 → 1430.440 ms, Source 498.884 → 471.344 ms. Построение syntax model
на worker заняло 14.137 ms, захват descriptors — 0.174 ms, ожидание — 0.009 ms.
Это диагностическая серия, не контролируемое подтверждение дополнительного
ускорения от MODEL_DIRECT. Оба model флага также остаются OFF по умолчанию.
Результаты этой комбинации находятся в `model-validation/`, executable там
содержит MODEL_DIRECT; основной `candidate.exe` содержит только overlap.
