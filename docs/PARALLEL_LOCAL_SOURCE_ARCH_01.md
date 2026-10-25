# PARALLEL-LOCAL-SOURCE-ARCH-01 — audit slice

Первый implementation slice выполнен отдельно в `PARALLEL_LOCAL_SOURCE_02.md`: реальная extraction, global semantic linking и symbolic alias-aware reduction без повторного разбора Source. Production и materialization по-прежнему не изменены. Ниже сохранён исходный audit baseline.

Дата: 2026-10-24. Выполнен аудит текущих зависимостей Source V2 и исполняемая проверка нового final-cell контракта. Production не изменён; parallel parser/merger и новый persistence format не реализованы. Масштабирование, доля CPU независимого local parse и стоимость merge пока не измерены.

Целевой контракт: `SOURCE_FINAL_CELL_CONTRACT_01.md`. Он изменяет alias-aware winner selection относительно V2. Прежние SOURCE-DEFERRED-OBJECT-01/BULK-MATERIALIZATION-01 остаются результатами экспериментов со старой семантикой.

## Что подтверждено кодом

| Участок | Возможно без изменяемого global G? | Необходимый контекст |
|---|---|---|
| Ready lexical token consumption, spelling/literal syntax | Да | Неизменяемый Source buffer/token view; правильный lifetime |
| Namespace/declaration shape extraction | Да, с local symbol refs | Global WHO/string IDs выдавать позже; не писать в shared identity_space/string_table |
| Итоговая классификация declaration/assignment | Не всегда | V2 использует видимость object/type/namespace, включая spelling shadowing; синтаксическая extraction не равна полной semantic validation |
| Literal parsing | Частично | Numeric/string decoding локальны; enum constant lookup зависит от scope и shadowing Source objects |
| Object type establishment | Частично | Header type catalog read-only; qualifiers/arrays/references могут сейчас вызывать `G.derive_resolved`, их нужны local type shapes либо согласованное global numbering |
| Локальный endpoint после локального объявления | Условно | Установленный root type и immutable type/member metadata; final WHO/paths можно назначить позже |
| Endpoint к объекту другого Source | Не из одного local file | Нужен тип внешнего объекта и temporal visibility; повторный синтаксический разбор нельзя выдавать за однократный merge |
| `G.intern_resolved_endpoint_path` / object registration / init / link commits | Нет для общего G из workers | Единоличный builder либо подготовленные disjoint slots/индексы; существующие mutable vectors не thread-safe |
| Final-cell reduction после linking | Да для независимых групп | Последний logical event и все origins; разрешённые aliases, disjointness |

`source_endpoint_resolver::object_type` сейчас читает `G.find(G.find_object(who))->type`. Остальная typed traversal читает type catalog. Передача типа аргументом позволяет отделить dependency, но не решает получение типа cross-file объекта.

`scope()` в Source V2 выбирает ветку с учётом видимых object/type/namespace. `scalar()` учитывает Source-object shadowing enum names. Поэтому reuse существующего V2 как независимого worker без shared semantic state не доказан. Безопасная первая граница — local extraction + unresolved semantic requests; точная доля fully typed work зависит от доступности type context и требует отдельного измерения.

## file identity и временный порядок

Production `parse_source_roots_v2` обходит `file_id{i+1}`. Full construction берёт `context.files.size()` после composition и не получает явную tree-order sequence.

Composer сначала разрешает IDs непосредственных dependencies, затем посещает вложенные Projects. Кроме того, optional `semantic_roots` сортируется по file_id перед возвратом. Такой output нельзя просто переименовать в tree-order tasks. Требуется отдельный construction-local order, собранный в установленном tree traversal, без изменения существующих IDs и BUILD lineage.

Проверенный fixture:

```text
Project
 ├─ types.h
 ├─ defs.cpp            int value;
 ├─ Nested Project
 │   └─ child.cpp       value=1;
 └─ tail.cpp            value=2;
```

V2 Runtime содержит value=1; новый declaration-order DFS contract требует value=2. Это отдельное намеренное изменение порядка winner selection, помимо alias-aware reduction.

## Global validation не исчезает

Нужно согласовать namespaces, declarations/extern completions, object types, duplicate definitions, bindings и temporal visibility. Source 2 может быть syntactically ready, но ещё не globally valid. Сохраняем текущий declared-before-use/extern контракт, пока пользователь не изменил его отдельно. Финальный каталог не должен задним числом легализовать reference без предварительного объявления.

Обработка global validation может быть incremental по готовым зависимостям; материализация не обязана ждать общего barrier только ради file order. Но deterministic semantic decision order и diagnostics всё равно определяются logical events. Независимость materialization не делает semantic validation автоматически unordered.

Отдельный глобальный счетчик WHO в workers не нужен. Local symbol indices и final `identity_ref` — разные типы refs; их mapping должен выполняться один раз для сущности, с дальнейшим применением compact translation к ссылкам, без повторного name/member parsing.

## Новый выбор winners требует новых данных

G уже сворачивает repeated logical target, сохраняя первый slot и последнее value; event/origin последней записи не хранится. SourceMap хранит contributions, не полный журнал всех assignments. `parser_warning` содержит одну source location. Следовательно, новый LWW нельзя корректно получить перестановкой текущего final initialization array.

Минимальный semantic event input для explicit writes включает temporary source rank/local rank, origin, root WHO/local symbol, typed target/path, value и необходимые diagnostics references. Для symbolic alias solver не обязательно хранить actual SHM addresses; они появляются после ABI layout. Не следует превращать семантический G в зависимую от одного ABI таблицу адресов.

Повторные BIND к одному reference должны сначала дать final binding. Nested BIND target сам может проходить через alias: для него необходим явный dependency/cycle handling, а не предположение, что все references разрешаются одним независимым проходом. Использование только existing logical rebind index не доказывает отсутствие двух разных paths к одной physical reference slot.

Final-cell comparison нельзя ограничить прямыми records G: требуется согласованность compiled/query state, runtime execution image и следующего BUILD. Удаление winner и повторное связывание alias требуют восстановления/повторной проверки затронутых writers. Постоянный `file.order` для этого не обязателен; достаточный provenance/revalidation input — обязателен.

## Parallel pipeline: что остаётся гипотезой

```text
task(file_id, temporary source_order)
    → parallel local extraction / local refs
    → global declaration/identity/type validation
    → final reference linking / target canonicalization
    → alias-aware duplicate diagnostics + winner selection
    → materialization of disjoint final cells / consistent G indices
    → compiled.bin + runtime.bin + SHM
```

Глобальный ordered Graph consumer не является требованием нового final-cell контракта. Возможность построить эффективный параллельный replacement существующих Graph writes ещё не доказана: BULK-MATERIALIZATION-01 показал, что дополнительный buffer/target index может стоить дороже incremental API.

Streaming local results возможен. Для ограничения памяти нужен bounded in-flight window/backpressure и передача владения; «не ждать всех workers» само по себе не ограничивает число готовых моделей. Prefix admission способен ввести head-of-line blocking; unordered scheduling validation-ready groups может уменьшить его, но требует готового dependency model. Нельзя одновременно обещать отсутствие barrier, минимальный memory footprint и отсутствие blocking без измерений.

Не все final targets готовы при первом local result. Если binding может быть переопределён позже, раннее применение значения через него ещё не является окончательным winner. Можно продолжать extraction/validation, но irreversible publication итогового значения требует завершённости нужных bindings/events либо последующего исправления. Схема не обязана блокировать весь Project, но dependency-specific readiness должна быть определена.

## Исполняемое свидетельство и ограничения

`verify_contract.py` проверяет independent scalar final-target reducer (без Parser/Linker), 6 перестановок готовности, local ordering, file ID independence, pairwise warning origins и final referent mapping. Production nested-project fixture демонстрирует несовпадение ID order и tree order. Проверки прошли.

Это не доказательство полного parallel Source Parser, не benchmark и не полная Runtime parity нового контракта. Реальные task durations/head-of-line wait, allocation bytes, local-to-global mapping cost, worker scaling и end-to-end PUBLISH должны измеряться на будущем изолированном prototype. Ни скорость <1000 мс, ни ≥10% improvement не заявляются.

Следующий ограниченный implementation slice: task/event order + typed unresolved requests и provenance, затем final-target winner oracle с реальным reference linker. Только после этого проверять parallel extraction и materialization на полном UnitProXL; не начинать с ещё одного буфера перед прежними Graph APIs.

## Проверенные участки

* `project_configuration_manifest.cpp`: `process_dependencies`, recursion и сортировка `semantic_roots`.
* `project_full_construction.cpp`: composition/root_count и порядок semantic compiled image → physical Runtime plan.
* `source_parser_v2.cpp`: root loop, `scope`, `scalar`, `object`, `assignment`, `canonicalize_endpoint`, commits.
* `source_endpoint_resolver.hpp`: root object type lookup и type/member traversal.
* `graph.cpp`: `upsert_initialization`, сохранение slot при replacement.
* `source_map.hpp`, `parser.hpp`: фактический объём origin/event metadata.
* `shm_runtime_v2.cpp`: построение/применение final initialization list.

Команда: `python experiments/parallel-local-source-arch-01/verify_contract.py build/parallel-local-source-arch-01`. Результат — `summary.json` и nested Project artifacts в `build/parallel-local-source-arch-01`.
