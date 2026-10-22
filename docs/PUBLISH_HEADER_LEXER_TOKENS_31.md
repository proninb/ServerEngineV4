# Header: готовые физические tokens — эксперимент OFF

Обновление: шаг 32 реализует замену words готовыми 12-byte Header tokens,
а не дублирование. См. `PUBLISH_HEADER_TOKEN_ONLY_32.md`; оба режима остаются
экспериментальными OFF после измерений полного PUBLISH.

Реализован `SERVER_ENGINE_HEADER_LEXER_TOKENS_V2`, по умолчанию OFF.
PUBLISH Header lexer может сохранять ready tokens одновременно с compact words
и локальной symbolization. Prepared physical closure владеет массивами;
каждый macro/include replay получает read-only span одного физического файла.

Semantic cursor читает kind/offset/length напрямую, не декодируя compact words.
Разрешение local_symbol_id → string_id и numeric occurrences остаётся в cursor.
Preprocessing, macro conditions, include traversal, provenance и semantic commit
не объединяются между контекстами. Hash ранее уже сохранялся в lexical symbols;
данный эксперимент убирает decode, а не повторный Header hash.

Однократная подготовка numeric literals также читает ready tokens, если они есть.
Compact words сохранены для остальных физических читателей и fallback. При
allocation failure optional capture очищается и используется прежний compact
путь. Никаких повторных lex или чтений файла для fallback не требуется.

## Проверки

Cursor regression сравнивает ready и compact replay token за token: canonical
IDs, numeric kind/bits, source ranges, offsets, EOF. Ready cursor проверяется
вообще без compact words, включая extended positions/lengths и отказ при
повреждённом byte range. Интеграционная regression подтверждает ненулевой
Header lexer token count. Пройдены frontend regressions с flag ON, девять
macro/include и других интеграционных сценариев, 13 точных сравнений диагностик.

На копии UnitProXL подготовлены **3622698** ready Header tokens; Source продолжает
использовать принятые в шаге 30 **5889225** ready Source tokens. Все 42 полных
benchmark запуска с прогревами (36 ABBA + 6 stage) сохранили эталонные SHA-256
обоих файлов и прошли audit сохранённым baseline executable.

## Нативный MSVC Release IPO

Baseline — принятая сборка шага 30 без данного изменения. До измерений
зафиксированы восемь чередующихся ABBA/BAAB блоков плюс исключённый прогревочный
блок; в каждом блоке по два измерения каждого executable. Во время замеров
сборки/тесты не запускались. Интервал считается по восьми block differences.

| Показатель | Baseline | Header ready tokens |
|---|---:|---:|
| Обычный PUBLISH, 16 измерений каждого | 1413.629 ms | 1423.785 ms |
| Stage total, 2 пары | 1402.685 ms | 1421.910 ms |
| Header physical preparation | 75.096 ms | 86.193 ms |
| Header semantics | 221.666 ms | 223.478 ms |
| Source | 464.931 ms | 462.176 ms |

Среднее изменение полного PUBLISH — **+10.156 ms**. Для saving baseline−candidate
Student-t 95% CI **[-22.738, 2.427] ms**; новый вариант быстрее только в 1/8 блоков.
Ускорение не подтверждено. Stage показывает дополнительную стоимость физической
подготовки без заметного сокращения Header semantics. Дополнительный token buffer
и запись в него — вероятная причина; отдельные затраты на allocation, memory
traffic и compact decode не измерялись. Header уже использует сохранённые хеши,
оптимизированный compact cursor и semantic reuse: перенос решения Source не
означает аналогичного выигрыша для Header.

## Решение

Реализация сохранена как эксперимент, flag остаётся OFF. В OFF-конфигурации
ready cursor branches исключаются препроцессором. Принятый SOURCE_LEXER_TOKENS
остаётся ON; Header/Source overlap и обе syntax model опции не включаются.
Основная конфигурация пересобрана и проходит 22/22 CTest; ON-конфигурация проходит
frontend regressions. Основная конфигурация отдельно проверена относительно
исходного baseline: все шесть контрольных stage запусков с прогревами сохранили
SHA-256 и прошли audit (`control-validation/`). Две измеренные пары дали
1385.370 → 1388.205 ms; это короткая диагностическая серия, не доказательство
изменения скорости OFF-конфигурации.

Код: `prepared_include_v2.cpp/.hpp`, `prepared_frontend_v2.hpp`,
`preprocessor_v2.cpp`, `semantic_cursor_v2.hpp`.
Executables, scripts, CSV и `analysis.json`:
`build/publish-header-lexer-tokens-31/`.
