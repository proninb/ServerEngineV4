# Обзор кода ServerEngineV4 — 25 октября 2026

## Объём проверки

Инвентаризировано дерево `server_engine`: **258 C++ файлов, 161169 строк**.
Машинная инвентаризация: `build/architecture-review-20261025/inventory.json`.
Вручную разобраны активные тракты PUBLISH: acquisition/configuration, lexer,
Header/Source V2, C10 extraction/ordered binder, endpoint resolver, Graph,
compiled/runtime persistence, resident publication и основные Runtime boundaries.
Сопоставлены уже выполненные эксперименты, чтобы не предлагать отклонённые варианты
повторно. Это архитектурно-производительный обзор и проверка конкретных участков,
а не формальная верификация каждой строки или security-аудит всего сервера.

Production не изменён. Единственный новый алгоритмический кандидат — C15 в
`experiments/`: он проверяет доказанное повторение, найденное при разборе binder.
Другие пункты ниже не выдаются за реализованные оптимизации или измеренные выигрыши.

## 1. Конкретный риск памяти: inheritance transition arena

`server_engine/project/parser/source_endpoint_resolver.hpp:294, 357–390`.

Таблица member transitions имеет **65536** slots. При успешном inherited lookup
`remember_transition()` добавляет `base_steps` в `transition_bases`, затем
перезаписывает cache slot новым диапазоном. Старый диапазон в arena сохраняется.
При коллизиях и повторных промахах одинаковые base paths могут добавляться снова.
До уничтожения resolver арена не очищается/не переиспользует старые диапазоны.

Это не постоянная утечка: память освобождается с resolver. Но ограниченное число
cache slots не ограничивает объём backing arena; защита uint32 предотвращает
переполнение, а не большой пик памяти. На inheritance-heavy входе рост зависит
от числа повторных lookup, а не только от количества живых entries. Влияние на
UnitProXL RSS/время этой проверкой не измерено.

Что улучшить: отдельно проверить ограничение arena с отказом от запоминания при
достижении лимита либо безопасное переиспользование диапазонов. Обычный lookup
должен оставаться fallback. Нужны collision/churn fixtures с наследованием и
проверка полного typed path/первой ошибки; затем память и полный PUBLISH.
Не добавлять ещё один cache. Это приоритет устойчивости памяти, не обещание скорости.

## 2. Для скорости: PGO реального production executable

`CMakeLists.txt:470–500`, `docs/PUBLISH_MSVC_PGO_12.md`.

В коде есть измеренная инфраструктура MSVC PGO, но она намеренно применяется
только к `ServerEngineV4PublishBenchmark`. Для исторической сборки benchmark
показал **114.58 мс** среднего выигрыша, CI **99.66–129.51 мс**, 8/8 блоков.
Это не результат нынешнего сервера или C10, и старую PGD нельзя переносить между
executables/версиями исходников.

Что улучшить: отдельный production-target GENERATE/USE цикл с обучением на
актуальном executable и проверкой нескольких проектов. Замерять реальный полный
PUBLISH и semantic parity; хранить профиль/версию compiler отдельно от исходников.
Это основанное на прежнем положительном результате направление без нового
семантического индекса. Текущий выигрыш неизвестен; <1000 мс не обещается.

## 3. Для памяти: раннее уничтожение construction context уже доказано

`project_full_construction.cpp:1011–1018, 1590–1597`,
`docs/CONSTRUCTION_LIFETIME_01.md`.

По умолчанию context с G/identities/strings/исходными данными живёт при создании
resident SHM. Экспериментальный ранний reset есть только под
`CW_CONSTRUCTION_EARLY_RELEASE_AB`; он не включён в production.

Исторический контролируемый эксперимент снизил peak WS на **326.30 MiB / 13.29%**,
но ускорение не подтвердил: paired median gain **-2.93 мс**, 8/20 побед.

Что улучшить: если важен пик памяти, рассматривать этот существующий вариант как
отдельный memory/time tradeoff с актуальными lifetime/error-path проверками.
Не включать его под видом доказанного ускорения и не суммировать память с
другими экспериментами. REBUILD/BUILD lifetime и trace-владельцы требуют своих
сохранённых ограничений.

## 4. Для C10: покрытие важнее новых fast paths

`parallel-local-source-10-c3/publish_source.hpp:19`,
`parallel-local-source-02/extract.hpp:129–166`.

C10 — ограниченный Source pipeline, а не полная замена production. Assign inputs
отвергаются явно; extraction читает physical tokens и ограниченную грамматику,
а не выполняет весь Source preprocessor/contextual input. Проектная видимость
без обязательного extern и temporal bindings — отдельный принятый контракт;
его нельзя везде проверять только V2 byte parity. Forward visibility произвольных
будущих объявлений не реализована этим bounded pipeline.

Что улучшить перед интеграцией: зафиксировать поддерживаемые формы/отказы и
проверить реальные проекты кроме UnitProXL, preprocessing/configuration cases,
Assign inputs и namespace/extern combinations. Unsupported input должен иметь
определённый диагностический контракт. Корректность UnitProXL и hundreds of
fixtures не равна всей production grammar. Production V2 остаётся baseline.

## 5. Extraction повторно хеширует уже хешированные имена

`parallel-local-source-02/extract.hpp:18, 24–29`,
`frontend/source_token.hpp:22–34`.

При включённом PREHASH lexer token содержит `lexical_hash`. Local extractor
строит string_view и использует обычный `unordered_map<string_view,id>`, не читая
это поле. Следовательно, существующая локальная таблица вычисляет свой hash
по spelling, хотя lexer уже выполнил prehash. Это конкретное повторное чтение
байтов, но оно происходит на extraction workers, а не в measured name helper
ordered binder. Нельзя переносить сюда C14's 100 мс inclusive name estimate.

Что можно отдельно проверить: использование lexer hash в **существующей**
локальной таблице с обязательным full-spelling equality и корректным fallback
для отсутствующего hash. Это не предложение второго name index. Индекс hash
должен быть согласован для всех операций; нельзя подставить lexer hash в
std::unordered_map со старым std::hash и ожидать корректности.
Приоритет ниже ordered critical path: текущие измерения не обещают заметный
полный PUBLISH gain и число worker allocations само по себе не доказывает его.

## 6. Воспроизводимость и сопровождение экспериментов

Генераторы C7–C15 каскадно создают parser/binder копии через текстовые замены.
`once()` полезно ловит изменение anchors, но рост цепочки усложняет проверку
ODR/ABI и происхождения исполняемого кода. Импортируемый `PLS10_CORE_LIBRARY`
может не соответствовать текущему checkout, если библиотека была оставлена
от другой сборки. Совпадение семантических результатов не доказывает совпадение
всех compiler switches или внутренней реализации core.

Что улучшить: общий экспериментальный build-manifest с commit/dirty-source
fingerprint, compiler/linker version, полными defines/IPO, hash core library и
fixture manifest, плюс hashes frozen executables. Всё это вне production timing.
Постепенно уменьшать каскад генераторов и отделять observer adapters от semantic
изменений; не создавать ещё один производственный Parser для обслуживания probes.

Крупные файлы (`fixed_direct_materializer.cpp` 9013 строк,
`compiled_project.cpp` 8388 строк, `parser.cpp` 6283 строки) затрудняют локальный
review. Разделение по prepare/validate/execute и форматным секциям полезно для
сопровождения, но не объявляется performance optimization; для hot кодов после
рефакторинга также нужна проверка layout/IPO и полного PUBLISH.

## C15: найденное повторение проверено и не внедряется

Успешный declare() уже знает WHO из identities.resolve(). Затем apply() повторно
вызывал symbol_entity() для того же объекта ради provenance. Один кандидат
возвращает этот WHO, не меняя scope/type/member/error semantics и не вводя cache.

Удалены **137584 finds**; число identity resolves, все Graph операции и allocations
сохранились. **816 oracle runs PASS**, включая namespace shadowing, reopening,
extern completion, diagnostics/origins; full UnitProXL native storage совпадает.

Однако 15-block uninstrumented A/B не подтвердил скорость:
off → direct paired gain **-3.72 мс**, CI **-28.76…+13.06 мс**, **7/15** побед.
Сравнение с C10 также не установило выигрыша. **C15 = NO ADOPTION**.
Подробности: `docs/PLS10_C15_ENDPOINT_STRUCTURAL_AUDIT.md`.

Это проверенное устранение повторения, а не причина добавить WHO cache или
переписать endpoint resolver. Очевидная избыточность снова не равна PUBLISH gain.

## Что не следует оптимизировать заново без новых данных

* SHA-256 уже выключен на обычном PUBLISH acquisition (`file_context.cpp:318–325`).
* Structural bind действительно вызывается после encoder validation и при remap,
  но прежний профиль показывает ~0.001 мс structural bind, а не полный semantic
  scan. Удаление проверок не является подтверждённым крупным выигрышем.
* Parallel compiled first-touch уже принят; отдельный Runtime object pretouch
  и снижение Object workers были измерены и отклонены.
* Runtime API range memo был NO ADOPTION. Проверки адресов нельзя удалять по одному
  CPU symbol name; большую часть прежних inclusive samples составлял kernel work.
* C8, C12, C13 и теперь C15 не внедряются. C10 сохраняет подтверждённый выигрыш;
  новый cache/Graph format из этих результатов не следует.

## Практический порядок

1. Сохранить C10 и production V2; не продвигать C15.
2. Для скорости — подготовить актуальный production PGO A/B, не переиспользуя
   benchmark PGD и не обещая исторические 115 мс текущему коду.
3. Для памяти/устойчивости — отдельно проверить inheritance arena bound; ранний
   context release рассматривать по подтверждённому memory/time tradeoff.
4. До интеграции C10 завершать compatibility/coverage gates на нескольких проектах.
5. Улучшить build provenance экспериментов, чтобы дальнейшие сравнения были
   воспроизводимы на чистом checkout.

В этой работе не запускался заново весь production CTest matrix. Выполнены
C15-specific 816 runs, full native/artifact parity и 15 полных A/B блоков.
Новых production исправлений, интеграции, профилей реального сервера или обещанного
пути к <1000 мс этот обзор не создаёт.
