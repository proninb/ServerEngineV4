# PUBLISH: lexer передаёт готовые Source tokens

Принят `SERVER_ENGINE_SOURCE_LEXER_TOKENS_V2=ON` по умолчанию в CMake.
Требуется `SERVER_ENGINE_SOURCE_PREHASH_PARALLEL_V2=ON` (также default).
Для сравнения прежний путь доступен с SOURCE_LEXER_TOKENS_V2=OFF.

## Изменение pipeline

Раньше lexer записывал compact words, после Header Source preparation повторно
декодировала слова, вычисляла identifier hashes и создавала массив токенов.
Теперь PUBLISH lexer одновременно сохраняет готовый transient token:
kind, offset, length и уже вычисленный для keyword recognition хеш имени.
Source заимствует массив напрямую; decode/hash и создание второго массива
исключены. Если syntax model выключена и все токены готовы, Source preparation
также не запускает execution lanes.

```text
PUBLISH lexer → ready tokens + существующий identifier hash
             → [Source syntax operations] → validation/binding → commit
```

Compact words пока тоже записываются: существующее представление хранения и
cursor fallback сохранены. Новый transient token объявлен в `source_token.hpp`.
При включённой SOURCE_MODEL/MODEL_DIRECT builder читает borrowed tokens и пишет
только operations, без копирования tokens. Перекрытие Header/Source шага 29
совместимо с этим путём, но его флаг остаётся OFF по умолчанию.

## Владение и семантика

Owner выделяет per-file vector slots до lexical workers; один файл имеет одного
писателя. Hot buffer создаётся только для начального PUBLISH source_only scope.
BUILD/REBUILD не включают capture. Source читает immutable spans, имена intern
выполняются в каноническом порядке после Header, identity_ref и commit не меняются.
Хеш не заменяет сравнение полного spelling. В lexer не появляются semantic IDs.
После Source pass, включая ошибки, hot buffers освобождаются до materialization
resident runtime. Это освобождение входит в source_ns.

Если выделение optional token vector не удалось, частичный массив очищается,
capture выключается, lexer заканчивает compact words. Source использует прежний
decode/cursor путь. Не требуется повторное чтение или повторный lex файла.

## Проверки

Frontend regression сравнивает каждый ready token с compact decoder, проверяет
хеши идентификаторов, покрытие полного потока и extended offset после комментария
длиной 70000 байт. Проверяется фактическое использование lexer tokens и их
освобождение после Source. Девять интеграционных сценариев дали прежние артефакты;
13 ошибочных сценариев — точную baseline диагностику.

На копии UnitProXL `source_lexer_token_count=5889225`, равный полному числу
Source tokens. Все 42 полных benchmark запуска с прогревами (36 ABBA + 6 stage)
сохранили эталонные SHA-256 compiled.bin/runtime.bin и прошли независимый audit.

## Нативный MSVC Release IPO

Одинаковые новые исходники с flag OFF/ON. До серии зафиксированы восемь
чередующихся ABBA/BAAB блоков плюс один исключённый прогревочный блок; никаких
сборок и тестов во время замеров. В каждом измеренном блоке по два запуска
каждого варианта. Доверительный интервал рассчитан по восьми block differences.

| Показатель | Flag OFF | Flag ON |
|---|---:|---:|
| Обычный PUBLISH, 16 измерений каждого | 1428.328 ms | 1390.873 ms |
| Stage total, 2 пары | 1382.960 ms | 1351.270 ms |
| Stage lexical | 177.966 ms | 178.452 ms |
| Stage Source | 440.971 ms | 428.925 ms |
| Source setup | 8.520 ms | 0.025 ms |
| Peak working set | 2574444544 bytes | 2572591104 bytes |

Средний выигрыш полного PUBLISH: **37.456 ms (2.62%)**;
Student-t 95% CI **[16.725, 58.186] ms**, 8/8 блоков в пользу нового пути.
Все блоки, включая более шумный пятый, сохранены без удаления выбросов.
Stage серия диагностическая: её изменение нельзя приписывать только decode/hash
или складывать с предыдущими выигрышами. PUBLISH < 1 s пока не достигнут.

Сырые результаты, executables, скрипты и `analysis.json`:
`build/publish-source-lexer-tokens-30/`.

## Итоговая конфигурация и совместимость

После включения flag ON основная конфигурация (сервер и все test targets)
пересобрана и прошла **22/22 CTest**. Финальный PUBLISH дал эталонные SHA-256
и прошёл audit ранее сохранённым baseline executable.

Отдельно проверены SOURCE_LEXER_TOKENS + HEADER_SOURCE_OVERLAP + SOURCE_MODEL +
MODEL_DIRECT: frontend regressions, девять интеграционных сценариев
и шесть полных stage запусков с SHA/audit. На UnitProXL
одновременно подтверждены 5889225 borrowed lexer tokens и 563613 direct model
assignments. Построение операций на worker заняло в среднем 5.261 ms вместо
decode/hash/model около 14 ms в диагностике шага 29. Это разные серии, а не
контролируемое доказательство выигрыша 9 ms. Две пары этой комбинации дали total
1404.890 → 1402.055 ms; она не даёт основания включать MODEL/OVERLAP по умолчанию.
Логи комбинации находятся в `model-validation/`; основной `candidate.exe`
содержит только SOURCE_LEXER_TOKENS, без MODEL/OVERLAP.

Поправка проверки диагностики (шаг 32): старые negative fixtures имели
нефиксированный порядок JSON fields. Основные 13 parser scenarios повторены
после исправления ordered JSON и совпали. Для экспериментального Source Model
Direct обнаружено расхождение source column в scalar_member; утверждение о его
точном совпадении отрицательных диагностик отозвано. MODEL_DIRECT остаётся OFF.
Benchmark и успешные интеграционные fixtures были валидны и не затронуты.
