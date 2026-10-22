# Header: готовые токены вместо compact words

Реализована настоящая замена физического представления Header, без второго
массива words. Включается двумя экспериментальными флагами:

```cmake
SERVER_ENGINE_HEADER_LEXER_TOKENS_V2=ON
SERVER_ENGINE_HEADER_TOKEN_ONLY_V2=ON
```

Оба остаются OFF по умолчанию: измерения не показали ускорения. Принятый
SOURCE_LEXER_TOKENS шага 30 остаётся ON.

## Как работает

```text
Header lexer → header_lexical_token[12 bytes]
             → literals / include discovery / contextual replay / optional models
```

Lexer выбирает тип writer один раз на файл, перед основным циклом. Header writer
записывает только kind, offset и length. Он не вызывает compact encoder, не
выделяет word arena и не вычисляет для потока готовых токенов новый hash. Хеши
идентификаторов уже сохраняются в отдельном существующем local symbol stream.
В reused lexical_stream старый word arena освобождается при token-only reset.

Sparse directive anchors в этом представлении содержат token index. Include
discovery, literal preparation, semantic cursor, raw Header Model и effective
Header Model умеют читать ready stream. Macro/include контекст и semantic IDs
разрешаются прежним replay; готовые физические токены не являются результатом
препроцессора. Graph, identity_ref, Source Map и форматы артефактов не меняются.

Compact lexical_generation отвергает публикацию потока с tokens, но без words:
новое представление нельзя незаметно принять за сохранённый BUILD lexical state.
BUILD/REBUILD используют прежний writer. Source writer также сохранён отдельным;
в его token append не добавлена проверка режима Header.

В отличие от дублирующего варианта шага 31, при обязательном token-only output
ошибка allocation возвращает io_error. Нельзя продолжать через несуществующие
compact words или публиковать частичный ready stream.

## Проверки

Unit regression сравнивает каждый токен с compact reference, включая directive
anchors, числа, комментарий длиной 70000 байт и extended offsets. Она проверяет
нулевой word_count и запрет публикации token-only в compact generation.
Pipeline regression подтверждает одновременно ненулевой ready token count и
нулевой compact word count. На UnitProXL:

```text
header_v2_lexer_token_count = 3622698
header_v2_compact_word_count = 0
source_lexer_token_count = 5889225
```

Frontend regressions, девять интеграционных сценариев и 13 реальных отрицательных
parser scenarios прошли. Проверены отдельно raw/effective Header Model:
оба прошли frontend tests, девять сценариев, 13 сравнений диагностики и полный
UnitProXL с эталонными SHA-256 и baseline audit. На подходящем Header без
directives raw model действительно dispatched одну декларацию, с совпадением
обоих файлов с baseline. На UnitProXL effective model действительно распознал
24027 records и применил 334424 fields, при нулевом числе compact words.

Основная конфигурация пересобрана и прошла 22/22 CTest. Её полный PUBLISH отдельно
проверен относительно принятого baseline: все шесть контрольных запусков с
прогревами сохранили эталонные SHA и прошли audit (`control-validation/`).
Эта короткая контрольная серия не используется для вывода об ускорении OFF-пути.

## Измерения

Первая версия тоже не писала words, но использовала 16-byte Source token и
проверяла writer mode в каждом append. Фиксированные восемь ABBA/BAAB блоков
дали 1417.604 → 1433.448 ms. Она сохранена в `first-version/` и заменена
12-byte Header token и выбором writer вне token loop.

Итоговая версия: MSVC Release IPO, baseline — принятая сборка шага 30.
До измерений зафиксированы восемь ABBA/BAAB блоков плюс исключённый прогревочный
блок. По два запуска каждого executable на блок; без сборок/тестов одновременно
с замерами. Все 42 полных запуска с прогревами сохранили эталонные SHA-256
compiled.bin/runtime.bin и прошли независимый baseline audit.

| Показатель | Baseline | Token-only Header |
|---|---:|---:|
| Обычный PUBLISH, 16 измерений каждого | 1389.039 ms | 1409.422 ms |
| Stage total, 2 пары | 1402.690 ms | 1412.770 ms |
| Header physical preparation | 76.735 ms | 78.202 ms |
| Header semantics | 220.871 ms | 223.167 ms |
| Source | 459.493 ms | 469.167 ms |

Среднее замедление полного PUBLISH — **20.382 ms**; Student-t 95% CI для
замедления **[6.647, 34.118] ms**, 0/8 блоков в пользу token-only.
Перенос и удаление pack/unpack выполнены, но на этом workload не дали выигрыша.
Нельзя приписать всё изменение только decode или только памяти: отдельные
вклады allocation, cache traffic, code generation и взаимодействия стадий
не измерялись. Включать этот режим как ускорение оснований нет.

## Исправление отрицательных fixtures

При проверке raw model найден дефект старых PowerShell scripts: обычный
hashtable не сохранял обязательный порядок полей JSON, а preprocessor field
мог отсутствовать. Часть старых отрицательных сравнений поэтому останавливалась
на конфигурации проекта и не проверяла задуманный parser error.

Scripts исправлены на ordered JSON с preprocessor.predefines. Добавлена проверка,
запрещающая считать configuration error успешным отрицательным parser test.
Сравнения повторены для текущего token-only, raw model, effective model и основных
кандидатов шагов 30 и 31: все 13 parser diagnostics совпали. Это исправление не
затрагивает benchmark/успешные интеграционные fixtures: они имели валидный JSON.

У архивного Source Model Direct шага 30 корректные fixtures обнаружили отдельное
расхождение позиции ошибки: `a.value.x` указывает на начало endpoint вместо
неверного member access. Его прежнее утверждение о точной отрицательной
диагностике отозвано; этот экспериментальный режим остаётся OFF. Header Model
raw/effective текущего шага таких расхождений в проверенных сценариях не дали.

Код: `lexer.cpp`, `lexical_stream.cpp/.hpp`, `source_token.hpp`,
`prepared_include_v2.cpp/.hpp`, оба Header Model readers и telemetry.
Scripts, executables, CSV, `analysis.json` и логи:
`build/publish-header-token-only-32/`.
