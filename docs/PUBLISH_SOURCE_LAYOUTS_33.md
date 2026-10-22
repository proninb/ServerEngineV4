# Source: сравнение трёх физических представлений

Реализованы и проверены три режима на одних исходниках и копии UnitProXL.
Header остаётся на принятом compact пути. Header/Source models, overlap,
prebind и construction window во всех трёх режимах выключены.

| Режим | SOURCE_LEXER_TOKENS | SOURCE_PREHASH_PARALLEL | SOURCE_TOKEN_ONLY |
|---|---|---|---|
| Прямой compact cursor | OFF | OFF | OFF |
| Текущие words + ready tokens | ON | ON | OFF |
| Только ready tokens | ON | ON | ON |

Все имена опций имеют префикс `SERVER_ENGINE_` и суффикс `_V2`.
Для прямого compact cursor также SOURCE_PREDECODE_PARALLEL и SOURCE_MODEL должны
быть OFF: иначе после lexer может создаваться промежуточный ready массив.

## Реализация token-only

Добавлен `SERVER_ENGINE_SOURCE_TOKEN_ONLY_V2` (default OFF), требующий
SOURCE_LEXER_TOKENS=ON. Lexer выбирает отдельный Source writer один раз на файл.
Он записывает готовые Source tokens с существующим identifier hash; compact
words и directive anchors Source при этом не создаются. Директивы остаются
токенами и отвергаются Source semantic input в прежнем порядке.

`lexical_generation` имеет отдельную transient availability для token-only
Source. Она не выдаётся за compact lexical record. `semantic_input.start()`
сам подключает обязательный ready input: недоступность optional preparation batch
не может превратить непустой Source в EOF. Это проверено напрямую без batch.
Пустой Source тоже имеет корректную availability и заканчивается EOF без words.

Один physical Source file имеет одного lexical writer; owner выделяет slots до
начала workers. Symbol interning и identity_ref остаются у canonical owner.
На успехе и ошибках Source pass transient buffers/availability освобождаются.
Token-only allocation failure возвращает io_error, а не пытается читать
несуществующие words. BUILD/REBUILD по-прежнему используют compact state;
database serialization уже отвергает token_count > word_count.

## Проверка фактических путей

В benchmark добавлены счётчики storage и input cursor. В каждом stage запуске
script требует точные значения, а не только присутствие флага CMake:

| Режим | Stored words | Lexer ready tokens | Cursor compact tokens | Cursor ready tokens |
|---|---:|---:|---:|---:|
| Compact cursor | 5889225 | 0 | 5889225 | 0 |
| Words + ready | 5889225 | 5889225 | 0 | 5889225 |
| Ready only | 0 | 5889225 | 0 | 5889225 |

Frontend regressions прошли отдельно для всех трёх executable, включая empty
token-only Source, обязательный input без batch, полный поток и его hashes
против compact reference с extended source offset. Для каждого режима пройдены
девять интеграционных сценариев и 13 точных сравнений реальных parser diagnostics.
Отрицательные fixtures используют ordered JSON и отвергают configuration errors
как ложное прохождение parser test.

## Контролируемые измерения

Нативный MSVC Release IPO. Заранее зафиксированы восемь шестипусковых зеркальных
блоков: например ABC/CBA. В каждом блоке у каждого варианта две симметричные
позиции, затем перестановки ротируются. Один такой блок исключён как прогрев.
Итого 16 измерений каждого executable. Отдельно 3 stage rounds по три варианта,
первый round исключён как прогрев. Во время измерений не было сборок/тестов.

Все **63 полных запуска**, включая прогревы, сохранили эталонные SHA-256 обоих
артефактов и прошли независимый audit executable текущего режима words+ready.

| Режим | Обычный PUBLISH | Stage Source, 2 запуска | Stage total, 2 запуска |
|---|---:|---:|---:|
| Прямой compact cursor | 1425.209 ms | 483.762 ms | 1423.915 ms |
| Текущие words + ready | 1402.679 ms | 455.016 ms | 1404.545 ms |
| Только ready tokens | 1398.008 ms | 459.522 ms | 1403.240 ms |

Два заранее выбранных сравнения — относительно текущего words+ready.
Использованы парные разности средних по восьми блокам, df=7 и Bonferroni:
индивидуальные двухсторонние **97.5% CI**, суммарное family confidence не менее
95%; critical t=2.8412442486.

* Compact cursor медленнее на **22.529 ms**, adjusted CI замедления
  **[12.264, 32.795] ms**, 0/8 блоков быстрее текущего режима.
* Ready-only быстрее в среднем на **4.671 ms**, adjusted CI выигрыша
  **[-6.327, 15.670] ms**, 6/8 блоков быстрее. Устойчивое ускорение не подтверждено.

Прямой compact cursor восстанавливает tokens и повторно вычисляет имя/hash при
canonical interning; ready input использует hash lexer. Это различие подтверждено
кодом, но отдельную долю decode/hash в общем времени данная серия не изолирует.
Результат относится к этим реализациям и этому workload.

В диагностической stage серии peak working set ready-only составил
2549047296 bytes против 2573807616 bytes words+ready — примерно на 24 MiB меньше.
Это две stage пары, не отдельная статистическая оценка памяти и не доказательство
ускорения по стадиям. Stage времена не подменяют основной total PUBLISH.

## Итог

Основной режим оставлен **words + ready tokens**: он уверенно быстрее прямого
compact cursor, а отказ от words пока не дал подтверждённого ускорения.
Новый token-only режим доступен для дальнейших экспериментов, default OFF.
Прежние Source оптимизации, Header compact mode и BUILD/REBUILD policy сохранены.

Основная конфигурация с текущими исходниками пересобрана, прошла **22/22 CTest**
и финальный полный PUBLISH с эталонными SHA-256 и независимым audit.
Логи, три executable, scripts, CSV и `analysis.json`:
`build/publish-source-layouts-33/`.
