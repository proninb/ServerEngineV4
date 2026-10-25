# SOURCE-DEFERRED-OBJECT-01 — архитектурный аудит

Обновление контракта 2026-10-24: пользователь принял alias-aware last-write-wins по порядку Source в дереве проекта, независимо от старого first-slot execution order. См. `SOURCE_FINAL_CELL_CONTRACT_01.md`. Измерения и counterexamples ниже описывают прежний V2; требование сохранять именно его результат не переносится на новый semantic contract автоматически.

Дата: 2026-10-24. Область: текущий production Source V2, существующий G, SourceMap, persistence и Runtime V2. Оптимизатор и двухфазный builder не реализованы; production-файлы не изменены. Выполнены чтение кода и три малых PUBLISH для проверки порядка записей в Runtime-плане.

## Решение

**Разделить typed resolution и физическую запись возможно. Произвольную группировку финальных инициализаций по WHO считать семантически эквивалентной нельзя.** В Runtime существуют aliases: разные `(WHO, path)` могут обозначать одну физическую ячейку. Текущий порядок final initialization list участвует в поведении программы.

Изменение числового WHERE при новом полном построении само по себе не является ошибкой. Условие — корректность всех зависимых ссылок, порядка исполнения, provenance и последующего BUILD относительно нового baseline. Изменение byte-parity критерия не отменяет этих условий.

Объекты следует продолжать регистрировать при объявлении через существующий G. Дополнительная таблица `WHO → Type` для предлагаемого разделения не нужна. Resolver может принимать установленный тип аргументом, но перенос того же lookup к вызывающему коду не экономит работу.

## Фактическая граница текущего V2

```text
object(): объявления, extern/type conflicts, WHO, G object registration
assignment(): lookup, typed path, enum normalization, value/reference compatibility
canonicalize_endpoint(): path interning, endpoint_ref
commit_construction(): resolved Graph mutation
commit_queued()/assign_string(): SourceMap и replacement warning
Runtime: construction → link resolution → final initialization list
```

`read_endpoint()` уже выдаёт WHO, исходный и конечный тип, локальные steps и origin до интернирования endpoint path. В `begin_endpoint()` lookup экземпляра нужен для получения root type. Member/array traversal затем читает каталог типов. Source при этом может добавлять derived types для объявлений массивов/references; весь G нельзя безусловно объявить immutable.

Header — не обязательно только типы: он также может оставить internal-static objects, links и constructor defaults. Отложенный Source должен учитывать существующие записи, а не предполагать пустые массивы и индексы.

## Проверки и побочные эффекты

| Участок | Уже выполняемая работа | Что нужно при двух фазах |
|---|---|---|
| `object()` | Тип/форма объявления, массивы, extern state, duplicate definitions, initializer compatibility; создание WHO, объекта и construction; object contribution | Оставить регистрацию и проверку видимости в первой фазе. Состояние extern не подменять будущим каталогом |
| `assignment()` | Порядок токенов, existence/visibility WHO, member/array bounds, reference compatibility, scalar/enum normalization, semicolon | Оставить в первой фазе в исходном semantic order, с прежним diagnostic origin |
| `canonicalize_endpoint()` | Direct member либо canonical path; interning меняет path/step arrays и индекс, назначает handle | Можно сохранить здесь. При переносе нужны локальные shape IDs, позднее преобразование и проверка всех потребителей endpoint_ref |
| `commit_construction()` | Проверяет producer/target WHO, выбирает initialization либо reference binding | Проверку producer оставить до сохранения операции; dispatch можно заменить bulk materialization |
| `add_resolved_initialization()` | Проверяет допустимость scalar target, в том числе const, enum/pointer; вызывает upsert | Сохранить проверку target type в первой фазе. `construction_compatible()` не заменяет весь этот gate |
| `upsert_initialization()` | Проверяет ключ; lookup `(WHO, endpoint_ref)`; при замене обновляет value существующего слота, выдаёт `replaced`; при вставке растит индекс/массив, добавляет запись | При прежней диагностике первой фазе всё равно нужно знать существование ключа. Физический append/patch и построение конечного индекса можно вынести в bulk-фазу |
| `rebind_resolved_link()` | Проверяет ненулевые endpoints; lookup по target; при замене меняет source в старом слоте и выдаёт `replaced` | Сохранить lineage target и duplicate state. Новые слоты можно назначать позднее, но до создания `source_data_ref::link` |
| `add_resolved_link()` | Повторный lookup при новом target, grow/rebuild индекса, append link, назначение handle, вставка target index | Bulk builder потенциально исключает повторный probe и многократный рост. Само отложенное выполнение этого не обеспечивает |
| `sources.add()` | Проверяет active root/finalized/file/data; root-local dedup contribution; type declaration→definition upgrade; рост arrays/index; file counts | SourceMap — не журнал каждого statement. Сохранить producer/root attribution и вклады прежних writers, включая записи, позже переопределённые |
| `sources.add_dependency()/add_initialization()` | Проверки состояния и, если включён BUILD acceleration, dedup/index/append | В PUBLISH без capture эти payload writes уже отключены. Не считать их все полноценными индексными вставками |
| `sources.begin_root()/end_root()/finalize()` | Root boundaries, ranges, dedup generations, file ordering; в capture mode также presence/dependency indices | После отложенной записи root layout должен оставаться согласованным. Группировать вызовы SourceMap только по WHO недостаточно |
| `warn_replaced()/warn_rebound()` | Warning зависит от `replaced`, file/range текущего target, порядка statements; строковый initializer выдаёт один warning по `any_replaced` | Нужны исходные events/origins и точная агрегация строк. Не генерировать warning на каждый byte строки |

Resolved Graph API доверяет предварительному binding: `rebind_resolved_link()` не делает повторный полный type compatibility check. Это уже преимущество текущей архитектуры, а не работа, которую новый builder впервые устранит.

Ресурсные ошибки (capacity overflow, allocation failure) и ошибки целостности индекса отличаются от semantic diagnostics. Bulk phase не может гарантировать отсутствие allocation errors только потому, что операции проверены. Нужно сохранить failure-safe publication и отдельно определить контракт ресурсных ошибок; не обещать прежнюю точку отказа allocator после смены алгоритма.

## Почему перестановка инициализаций может менять значение

Проверен исходный production executable `build/enum-v4/Release/ServerEngineV4PublishBenchmark.exe`, без каких-либо изменений алгоритмов.

```cpp
// Header
struct I { int x; };
struct R { I& ref; };

// Source: declaration order b, a
I b;
R a;
a.ref = b;
a.ref.x = 1;
b.x = 2;
```

Инициализации имеют разные логические keys, но после разрешения ссылки оба назначения ведут в адрес 76 относительно SHM. Исходный Runtime-план записывает `[1, 2]`, результат — `2`.

Стабильная группировка statements по WHO объявления даёт `b.x = 2; a.ref.x = 1;`: план записывает `[2, 1]` в тот же адрес, результат — `1`. Объекты и link source/target offsets в обеих проверках совпали. Отличие не сводится к номеру handle или SHA файла.

Третий случай:

```cpp
a.ref.x = 1;
b.x = 2;
a.ref.x = 3;
```

`upsert_initialization()` обновляет первый слот `a.ref.x` до 3. Итоговый план содержит `[3, 2]`, результат — `2`. Текущий last-write-wins действует для **одинакового логического target**; он не реализует глобальный last-source-event-wins для всех aliases одной физической ячейки. Менять это поведение молча нельзя, даже если новый алгоритм выглядит естественнее.

Практическое следствие: при свёртке нужно различать `first_insert_event` (порядок существующего final list), `last_write_event` (выбор payload) и events/origins диагностик. Один номер последней операции недостаточен для совместимости.

Проверка основана на декодировании реально выпущенных `runtime.bin` и ограниченной интерпретации одной direct static link и двух scalar writes. Это воспроизводимое доказательство различия emitted execution order, **не новый полный Runtime oracle и не чтение resident SHM**. Порядок реальных `memcpy` подтверждён чтением `materialize_initializations()`; оба fixture PUBLISH завершились успешно.

Допустимые варианты будущего алгоритма: сохранить execution precedence; либо доказать непересечение адресов для переставляемой группы; либо создать корректное execution ordering отдельно от сгруппированного хранения. Группы WHO не являются автоматически независимыми.

## WHO, WHERE и сериализация

| Представление | Свойство | Последствие перестановки |
|---|---|---|
| `identity_ref` / `type_ref::named` | Семантическая идентичность Project | Сохранять WHO и его смысл; физическая перестановка не требует новых WHO |
| `object_endpoint.object` | WHO объекта | Endpoint не содержит object_handle |
| Direct member / canonical path | `endpoint_ref` содержит local member index либо path handle | Path handle — физическая ссылка; при перенумерации обновить links, initializations, SourceMap targets и прочих потребителей |
| `object_handle` | Locator через `identity_locations` | Полное построение может назначить другой; runtime object ABI/offset columns и persistence должны согласоваться |
| `link_handle` | Physical link slot, не отдельный `identity_ref` | Он хранится в `source_data_ref::link`, Runtime `links[slot-1]`, pending reference encoding. Переставить только link_records нельзя |
| Object initialization | Плотная запись, индекс хранит position | Отдельного публичного `initialization_handle` в проверенном API нет. Позиция списка задаёт Runtime execution order |
| BUILD delta | Existing type/object/link slots — stable WHERE | Новый PUBLISH layout может стать новым baseline; последующий BUILD должен сохранять **его** lineage WHERE |

Stable-WHERE подтверждён `graph_delta.hpp` и `GRAPH_IDENTITY_APPEND_HANDOFF.md`: existing entries patch/reactivate same slot, новые добавляются. Это не обещание сохранения WHERE между независимыми полными построениями. Оно также не даёт разрешения перемещать опубликованные slots активной incremental lineage.

Для полного PUBLISH можно исследовать отличный физический порядок, если все связанные артефакты создаются согласованно и новый layout детерминирован. Изменение содержимого при том же record ABI не автоматически требует новой format version; но совместимость LOAD, validators, Runtime, source-save, queries и следующего BUILD необходимо проверить отдельно. При изменении layout/schema/значения полей вопрос версии становится обязательным.

Byte parity текущих экспериментов — сильный regression gate, а не фундаментальная семантика WHO. Для нового placement candidate допустим отдельный режим semantic equivalence. Старый SHA нельзя просто обновить и назвать доказательством корректности.

## Можно ли писать массово прямо в существующий G

Да, **как архитектурная возможность**, при следующих условиях:

1. Phase 1 сохраняет объекты и типы в существующем G, выполняет проверки в исходном порядке, выдаёт валидированные typed operations и origins.
2. Существование каждого target и повторные записи классифицируются с учётом Header prefix и предыдущих Source events. Для ранних duplicate warnings нужен seen state. Если это отдельный новый индекс, он дублирует существенную часть нынешней работы Graph; если индекс G, большая часть hashing остаётся в Phase 1.
3. Materialization строит итоговые unique records и индексы bulk-алгоритмом, а не вызывает те же upserts для каждого captured statement.
4. Доступ к массивам остаётся Graph-owned: они private. Нужен ограниченный bulk API или экспериментальный friend-access; Graph ABI менять для этого не обязательно. Прямые несогласованные записи из Parser недопустимы.
5. Порядок final initialization execution сохраняется либо доказана независимость. Для new link/path slots все references и SourceMap должны получить окончательные handles.
6. Весь объём временных operations, shapes, dedup state и provenance входит в memory/lifecycle measurement. Не создавать Local G и не дублировать полный object/type catalog.

Обнаружение первого ошибочного statement не переносить за последующий parse error. Вместе с тем frozen order диагностики allocator не следует смешивать с semantic decision order. Текущий SourceMap/`parser_warning` не содержит полного журнала всех старых и новых значений: расширенный пользовательский контракт истории — отдельная работа, а не бесплатный побочный эффект deferred mode.

## Что этот аудит говорит о возможном выигрыше

Существующий G **уже хранит финальное состояние**: upsert меняет payload старого слота, rebind меняет source существующего link. Новая свёртка не должна повторно решать эту уже решённую задачу.

Согласно принятому RG01 capture UnitProXL, 705 237 input initialization records дали 705 237 final records; 367 144 link calls дали 367 144 links. **Replacement calls отсутствовали.** Поэтому устранение overwritten operations в этом корпусе не сократит число final writes. Повторные операции проверены fixtures, но не являются показанным bottleneck данного corpus.

Потенциальный резерв bulk builder — capacity planning, отсутствие repeated growth, один target lookup/insert, более выгодный проход по индексам и parallel writes в заранее назначенные slots. Их стоимость, стоимость нового временного state и полный PUBLISH выигрыш здесь не измерялись. Ускорение и достижение <1000 мс не заявляются.

## Контракт следующего ограниченного прототипа

Сначала проверять bulk materialization **с прежним execution order** на уже разрешённом RG01 входе, включая существующий Header prefix, без второго WHO/type catalog. Отдельно считать lookup/probes, рост массивов, peak memory и полный lifecycle. Если операции только перекладываются в очередь перед теми же API, кандидат не соответствует цели.

Два независимых gate:

* Совместимый порядок — прежние exact handles/records/indices и golden artifacts.
* Новый placement — сравнение по WHO/структурному пути/типу/финальному значению/связям, provenance по roots, diagnostics с decision order и origins; полноценный Runtime oracle с alias/overlap cases; LOAD и BUILD update/delete/reactivation от нового baseline; детерминизм нового algorithm независимо от scheduling. Сырые runtime addresses при разном layout напрямую не сравнивать.

Semantic G equivalence по одним конечным maps недостаточно: aliases показывают, почему execution precedence также часть gate. Этот аудит не утверждает новый placement contract окончательно и не реализует данный прототип.

## Источники и воспроизведение

Проверены:

* `server_engine/project/parser/source_endpoint_resolver.hpp`: `begin_endpoint`, `object_type`, member/array traversal.
* `server_engine/project/parser/source_parser_v2.cpp`: `object`, `assignment`, `canonicalize_endpoint`, `commit_construction`, `commit_queued`, `assign_string`, warnings, root barriers.
* `server_engine/project/graph/graph.cpp`: `intern_resolved_endpoint_path`, `scalar_initialization_target`, `upsert_initialization`, `add_resolved_link`, `rebind_resolved_link` и target indices.
* `server_engine/project/source/source_map.{hpp,cpp}`: contribution representation, root-local dedup, capture gates, finalize.
* `server_engine/project/shm/shm_runtime_v2.{hpp,cpp}`: stable link slots/pending handles, initialization plan construction и ordered materialization.
* `server_engine/project/persistence/{compiled_project,runtime_project}.cpp`: link slot persistence, runtime section encoding.
* `server_engine/project/graph/graph_delta.hpp`, `docs/GRAPH_IDENTITY_APPEND_HANDOFF.md`, `docs/RESOLVED_G_01.md`.

```powershell
python experiments/source-deferred-object-01/verify_alias_order.py build/source-deferred-object-01/alias-order
```

Три fixtures и stdout/stderr: `build/source-deferred-object-01/alias-order/{original,grouped_by_object,repeated_alias}`. Декодированные writes: `summary.json` в том же каталоге. Производительность этим скриптом не измеряется. Production Graph API/ABI, Parser, persistence и Runtime не изменены.
