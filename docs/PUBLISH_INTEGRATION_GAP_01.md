# PUBLISH-INTEGRATION-GAP-01

Read-only аудит после EVALUATOR-PARALLEL-01. Production и экспериментальный код
не изменены. Экспериментальный baseline UnitProXL: frontend/Type Binding 16 workers,
evaluator 8 workers, target 32768, semantic lifecycle 144,63 мс.

## Вывод

Параллельная архитектура пригодна как основа, но текущий bound result нельзя
подключить вместо Header V2: он не содержит всех данных G, не воспроизводит полный
поток интернирования и не создаёт production SourceMap/reuse tape. Нулевые constructor
gaps на UnitProXL означают покрытие данной value projection, а не полную совместимость.

Следующий безопасный шаг — расширять oracle на полный наблюдаемый контракт Header
и создать изолированный writer adapter. В production переключатель пока не добавлять.
Оптимизацию Type Binding выполнять отдельным экспериментом с тем же входом и контролем.

## Что на самом деле измерено

`experiments/evaluator-parallel-01/main.cpp:10` (`load_numbers`) сначала выполняет
compose, physical include closure preparation и effective preprocessing. Для этого
используется настоящая `semantic_preprocessor_v2` со string_table. После завершения
всего интернирования token string_id заменяются spelling views; промежуточные legacy
IDs не передаются в Parser. Эти операции выполняются до измеренных lifecycle trials.

Таймер trials в `main.cpp` охватывает discovery → extraction → assembly → canonicalization/
Binding/numbering → evaluation/commit → synthetic consumption → release. Он не
включает подготовку effective tokens, создание production G, SourceMap, Source parsing,
persistence или runtime publication. Материализованный effective stream и его строки
живут вне этого lifecycle. Снижение retained bound result не означает, что вся память
input/preprocessor включена в allocation audit.

Type Binding 27,46 мс также нельзя целиком называть последовательной стадией:
`header-semantic-coverage-03/model.hpp::bind_impl` строит catalog последовательно,
затем разрешает requests через `workers.run`, после чего проверяет повторные definitions.
Нужен отдельный breakdown перед выбором следующей оптимизации.

## Матрица несовместимостей

Это статически подтверждённые различия исходников, не новые результаты выполнения
fixtures. Полное множество расхождений вне проверенного корпуса ещё не установлено.

| Контракт | Production Header V2 | Текущая projection | Необходимая проверка/доработка |
| --- | --- | --- | --- |
| Declaration member initializers | `parse_member_initializer`, immediate/pending application, reference generations | `extractor::field` пропускает initializer и создаёт member_initializer gap | Отдельные events, порядок относительно constructor assignments и появления members, exact diagnostics |
| Access | `default_access`, access labels, member/base access сохраняются в G | Labels и base access потребляются без сохранения; request имеет только base flag | Добавить access в immutable semantic records; сверять member_record/base_record |
| Callables | `parse_method_tail`, destructor/conversion/named operators проверяют grammar, qualifiers, default/delete/pure, override/final | `callable()` пропускает сбалансированные tokens, включая body | Воспроизвести validation events и declares_virtual; пропуск не считать coverage |
| Polymorphism | Проверяется base polymorphism и распространяется graph_type_polymorphic | В declaration/request нет declares_virtual/polymorphic результата | Проверить inherited flags, повторные definitions, ошибки override/final/pure |
| Inheritance | Complete non-union base, union cannot derive, duplicate direct base, access; virtual bases unsupported | Есть base type requests и temporal completeness, но нет полного base metadata | Проверить запреты и duplicate bases; сохранить исходный порядок bases |
| Record family | G различает class/struct/union и сверяет metadata | Локальная declaration хранит family, catalog часть проверок сводит к union/non-union | Полная parity declare/define/duplicate transitions, не только types |
| Declarators и intrinsic grammar | Production задаёт собственный поддержанный subset | Самостоятельная реализация type()/pointers()/arrays, не общий grammar engine | Differential negative fixtures; не переносить правила общего C++ вместо правил V2 |
| Multiple data declarators | `parse_member_declarator` явно отвергает comma после поля | `extractor::field` допускает цикл через comma | Отрицательный fixture `struct A { int x,y; };`, exact rejection и origin |
| Typedef | Production сохраняет только допустимый intrinsic alias, с semantic validation | Scope extractor также ограничен intrinsic, но реализует проверки самостоятельно | Негативные combinations/void/cv/derived и порядок alias conflict |
| Duplicate/first diagnostic | Parser останавливается на первом решении, G может отвергнуть define | Evaluator exact parity доказана для своей projection и fixtures | Добавить ошибки grammar, Graph validation, preprocessing и commit в общий decision order |
| Final G | Type/member/base domains, construction slots, derived types, flags, nested defaults | Bound types и sparse values с локальными indices/owning paths | Direct writer adapter и независимое сравнение всех domains/IDs |
| String IDs | Preprocessor и Parser создают строки в чередующемся legacy порядке | Строки input уже подготовлены; nested paths остаются owning strings | Ordered string events, общий numbering и downstream Source ID parity |
| SourceMap/reuse | Root/file contributions, semantic dependencies, include replay, finalize | Physical origin и root events есть, production provenance не формируется | Воспроизвести root-contiguous ownership и reuse tape, сравнить persisted data |

Примеры для будущих differential fixtures: `struct A { int x=1; };`,
`class A { int x; public: int y; };`, `struct A { virtual void f(); };`,
`union U : B {};`, `struct D : B,B {};`, `struct A { int x,y; };`.
Они иллюстрируют разные контракты, а не утверждение, что все они сейчас молча
принимаются: часть extractor отвергает или помечает gap. Нужны execution results.

Важное ограничение цели: method parameter types сейчас намеренно не persist/resolve
даже в production V2 (`parse_method_tail`). Интеграция должна повторить этот контракт,
а не незаметно добавить полный C++ callable semantics. Virtual inheritance production
также отвергает: требуется совпадение ошибки, а не реализация virtual ABI.

## String IDs и WHO

WHO и string_id — разные порядки. Canonicalizer numbering доказывает порядок first
creation для записанных событий, но текущая projection не содержит всех production
creation events и seed/import состояния. Упакованный 4-byte identity_ref сохраняется.

Production `header_parser_v2.cpp:2096` интернирует нормализованный nested path во
время обработки constructor operation; последующие identifiers/predefines/includes
могут интернироваться позже. Если сначала preprocessing интернирует все identifiers,
а writer потом добавит paths, string_id будут другими даже при равенстве строк.
Это конкретный риск byte parity, отсутствующий в сравнении normalized values.

Предлагаемый контракт, ещё не реализованный:

1. Immutable spelling arena и локальные spelling references, без глобального intern
   в extraction workers.
2. Сохранить фактические события preprocessing intern, Parser-created strings и
   начальное состояние таблицы. Effective-token ordinal недостаточен: нужны события,
   не создавшие effective token, и порядок относительно advance/include completion.
3. Объединить эти события с созданием nested path; назначить string_id по первому
   production encounter. Не сортировать строки лексикографически и не собирать
   только имена финальных сущностей.
4. Применить mapping при записи identity names, members и constructor paths. Передать
   Source Parser ту же таблицу, с теми же IDs и состоянием append.

Сначала нужен capture/oracle для этого порядка. Полный новый event journal может
стоить памяти; не утверждаем его финальным representation до измерения. Если byte
parity не является требованием будущего формата, это отдельное явное решение;
текущий аудит не отменяет установленный контракт.

## SourceMap и reuse

Production интеграционный узел — `parser.cpp:5889` и далее: для каждого Header root
выполняются `sources.begin_root`, `input.start`, Header Parser с `set_provenance`,
`sources.end_root`. После Header тот же G используется Source Parser V2
(`parser.cpp:6025`), затем `sources.finalize(files.size(), identities, G)`.

`header_parser_v2.cpp:109`/`:115` одновременно записывают dependency/contribution
в semantic reuse tape и source_map. Contributions включают declaration/definition
и physical file; dependencies возникают при разрешении named types и bases.
Одного origin на запрос недостаточно для восстановления всех этих действий,
особенно при includes и повторном использовании уже обработанных files.

Предлагаемый адаптер собирает локальные semantic observations с original root,
physical file, decision event и local/canonical identity; coordinator разрешает WHO
и воспроизводит contributions/dependencies в production порядке. Отдельно сохраняются
preprocessor begin/end/include/once events. Семантический commit записи должен
предшествовать соответствующему file-completion boundary: production код явно
требует это перед consume после definition. Достаточность offline replay для
reuse-cache protocol ещё не доказана и требует отдельного теста.

SourceMap — root-contiguous persisted ownership в compiled.bin. Semantic dependency
observations — отдельное BUILD acceleration state в source.bin, не G и не physical
File Context DAG (`source/source_map.hpp`). Их следует сравнивать раздельно, не
добавлять в G ради удобства интеграции и не удалять из-за PUBLISH-only сценария.

## Где подключать Direct G

Точка входа — после canonicalization/temporal binding и изолированного evaluation,
до Source Parser, на coordinator. Отдельный writer потребляет compact immutable
records, shape arenas, local outputs и mappings; Local G не создаётся.

Минимальный первый adapter может использовать существующие `G.declare_record`,
`G.derive_resolved`, `G.define_resolved_record`, `G.define_intrinsic_alias` и
`G.constructor_defaults.add`. Это запись в один окончательный G, хотя ещё не
оптимизированное прямое заполнение внутренних columns. Повторное применение
валидации существующих API измеряется отдельно; обходить её до parity рано.

Нужно сохранить отдельно порядок WHO, string_id и graph-local derived type_ref.
Production создаёт derived types во время parse_type/array/grouped declarator;
запись только окончательного shape по порядку fields может пропустить intermediate
derive events и изменить номера. Сначала trace/сравнение derived domain, затем
решение об event replay или детерминированном remapping всех потребителей.

Writer формирует полные member_record/base_record, record kind/flags,
construction slots и nested defaults, сверяет repeated definitions и выдаёт
ошибки с прежними decision/origin. Финальный result обязан жить независимо от
Parser requests. Не следует разрушать prepared catalog до последнего потребителя
writer; границу lifetime нужно проверять заново, synthetic consume её не доказывает.

Layout: здесь отсутствуют часть входных semantics (access/bases/polymorphism),
но сам runtime ABI/layout pipeline не следует переносить в Parser. После построения
G существующий SHM/runtime layout должен получить эквивалентный вход. Проверять
нужно и G metadata, и итоговые runtime artifacts.

## Стоимость и план принятия

Числовая стоимость интеграции пока не измерена: нет реализации writer/journal и
полного покрытия. Оценка сложности — высокая; это не замена одного вызова Parser.
Основной объём работы — порядок побочных semantic observations, а не worker pool.

| Этап | Изолированный результат | Условие перехода |
| --- | --- | --- |
| COVERAGE-CONTRACT | Differential fixtures для таблицы выше, полный output comparator | Нет скрыто пропускаемых categories; negative cases имеют exact first diagnostic |
| ORDER-TRACE | События string/WHO/derived types, preprocessing/reuse/provenance | Покрыты seed state, include completion, repeated roots, macros/predefines |
| DIRECT-G adapter | Один final G из локальных records, вне production | Полные domains + SourceMap совпадают с независимым V2; requests освобождаются до consumer |
| Shadow integration | Параллельная архитектура на копии context, результат не публикуется | Source Parser получает тот же Header state, конечные artifacts имеют byte parity |
| Paired PUBLISH | Один executable с CONTROL/candidate, одинаковый полный pipeline | Ускорение outer PUBLISH, корректность compiled/runtime/source artifacts, memory lifecycle |

Не запускать оба полных Parser в одном performance timer и не выдавать shadow cost
за цену production candidate. Сначала correctness, затем отдельные paired runs.
Проверять 1/2/4/8/16, original/reverse/permuted schedules, multi-root/include/macro
fixtures и mixed negative cases; 8 evaluator workers остаются baseline этого корпуса.

## Что это означает для цели менее 1000 мс

Исторический `docs/PUBLISH_TIMELINE_38.md` даёт candidate total 1372,530 мс,
Header semantic 220,518 мс и Header physical preparation 78,274 мс. Это другая серия,
не новый контроль для EP01. Arithmetic sensitivity при фиксированных остальных
фазах: даже нулевая цена Header semantic оставила бы 1152,012 мс. Следовательно,
одна замена этой фазы в той конфигурации недостаточна для <1000 мс.

Вычитание 144,63 из 220,518 было бы некорректно: измерения разновременные, границы
различаются, preprocessing и publication effects отсутствуют в experimental timer.
Реальный выигрыш может установить только новый paired outer PUBLISH после integration.
Затем отдельными задачами рассматривать Source, input preparation, persistence и
runtime publication по новому timeline; не складывать независимые исторические выигрыши.

## Проверка аудита

Проведено сопоставление исходников, существующих отчётов и границ таймеров.
Новые performance runs и semantic tests не запускались: executable не изменялся.
Это карта интеграции с подтверждёнными местами расхождений, а не заявление о полной
coverage или прогноз времени production. Production PUBLISH остаётся без изменений.
