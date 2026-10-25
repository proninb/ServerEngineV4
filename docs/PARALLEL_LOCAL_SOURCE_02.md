# PARALLEL-LOCAL-SOURCE-02 — Semantic Event Pipeline

Дата: 2026-10-24. **Первый correctness slice реализован.** Production Source Parser, Graph API/ABI, compiled format, SourceMap и Runtime не изменены. Новый путь — standalone C++ executable, а не включённый production mode. Материализация итогового G/compiled.bin/runtime.bin/SHM в этом slice не реализуется.

## Результат

| UnitProXL | Результат |
|---|---:|
| Source tasks | 3373 |
| Source semantic events | 717 096 |
| Объявленных объектов | 137 584 |
| Endpoint requests, extracted | 946 656 |
| Endpoint requests, typed | 946 656 |
| Explicit write payloads | 705 238 |
| Source BIND operations | 367 144 |
| Final value storage cells | 705 238 |
| Сохранённых declaration/write/binding provenance records | 1 209 966 |
| Source token consumption | 5 889 225 |
| Повторных чтений токенов глобальным этапом | **0** |
| Duplicate warnings на данном корпусе | 0 |

Одна явная инициализация Source declaration дополнительно вошла в event stream. В V2 она хранится в object construction отдельно от 705237 assignment records. Поэтому 705238 здесь не означает расхождение значений. Object construction также независимо сверена.

Сохранены и проверены все **705237** typed assignment payloads и **367144** typed bindings относительно существующего compiled G V2: WHO, typed storage path и value/source. Проверка one-to-one удаляет найденные entries и требует отсутствия остатка, не ограничивается совпадением counts. Для каждого объекта сверены WHO spelling/parent, type, flags и declaration construction.

Forward/reverse extraction дали одинаковые counts и fingerprint всех final values/bindings, warning transitions и declaration/write origins: `13649286433448276567`. Fingerprint — локальный FNV test checksum, не persistence identity или замена semantic oracle. Проверка правильности новых winners основана на ожидаемых значениях fixtures; legacy V2 не используется как oracle новой alias-aware семантики.

## Фактически реализованные границы

```text
Project composition / existing file_id
    → declaration-order DFS task(file_id, temporary source_order)
    → ready lexer tokens once
    → local extraction: flat owned model, local names/scopes/requests/events
    → release Source token arrays
    → global semantic linking: WHO, visibility, types, typed paths, origins
    → release local models
    → final reference linking / canonical storage targets
    → alias-aware final-cell reduction + duplicate diagnostics
    → experimental result/report (materialization follows in a later slice)
```

### Planner

Planner использует существующий configuration loader, но самостоятельно проходит dependencies в declaration-order DFS. Он не использует числовую сортировку `semantic_roots`. `file_id` остаётся неизменным.

Nested-project fixture реально получает tasks `[file_id 3/order 0, file_id 6/order 1, file_id 5/order 2]`. Новая pipeline выбирает `tail.cpp` со значением 2, независимо от forward/reverse extraction. Предыдущая V2 baseline давала 1 по file-ID order.

### Local extraction

Worker model не содержит global WHO или Graph handle. Owned arrays хранят names в character arena, scope indices, qualified-name components, type shapes, flat endpoint selectors, decoded literals/string bytes, events и source ranges. Именованные типы и cross-file symbols остаются явными unresolved requests; builtin/literal shapes известны локально.

Extractor выполняет Source grammar один раз. Это syntax extraction без global type catalog, object lookup, identity_space, string_table или Graph mutations. Он не копирует весь semantic Parser V2. Names и literals принадлежат model, а не заимствуются из Source token stream. Selector занимает 16 bytes, включая offset, точную длину и member/index tag; чрезмерно длинный selector, не помещающийся в 31-битную длину, отклоняется явно.

Локальная ошибка хранится вместе с уже выделенным prefix событий. Global validation рассматривает этот prefix прежде ошибки: более ранняя type/const error не маскируется поздней синтаксической ошибкой, обнаруженной extraction.

### Global semantic linking

API получает только local model records и существующие G/string/identity services. `file_context`, `lexical_generation` и Source buffer ему не передаются. Нет token_kind dispatch или повторного numeric/endpoint parsing. Lexer Source token arrays фактически освобождаются и проверяются на пустоту **до** вызова linker.

Именованные symbols разрешаются с учётом scope, temporal object visibility и extern completion. Необъявленные forward object refs остаются ошибкой; `extern` позволяет предварительно установить тип. Literal enum lookup учитывает object shadowing. Qualifiers/reference/array shapes согласуются через существующий derived type API. Member/array semantic transitions использует `source_endpoint_resolver`, не повторный Parser V2.

В этой первой версии validation выполняется последовательно по task rank. Это сохраняет semantic decision order и позволяет сначала проверить формат. Parallel execution globals/consumers не заявляется.

Объекты регистрируются в существующем G, как было принято ранее; он служит единственным object/type catalog. Source initializations/links/path interning в G здесь не выполняются. Уже типизированные semantic operations помещаются в `bound_model`. После уничтожения local models остаются owned declaration origins (WHO, name/statement origin, event), write target/value origins и BIND target/source origins. Отдельный второй WHO→Type каталог не создаётся.

## Настоящее разрешение aliases

В отличие от ARCH-01 Python reducer, target mapping не задан fixture вручную. C++ linker строит его из реальных bound BIND operations и Header metadata.

Canonical storage target — object WHO и typed path **без dereference**. Физический SHM address и ABI offsets не требуются для проверенной модели. Два paths, проходящие через references, связываются с одним owning storage target после разрешения final bindings.

Реализованы:

* Final Source binding по logical event order; typed source/target compatibility проверяется до alias pass.
* Whole-object reference chains и nested member/array traversal.
* Source BIND targets, сами проходящие через references: direct bindings дают начальный каталог, indirect targets согласуются до устойчивого решения. Неразрешённые зависимости и отсутствие сходимости отклоняются; циклы reference-following обнаруживаются DFS.
* Existing direct Header links как default linking input; native `member_binding` и `object_binding` constructors как fallback. В UnitProXL все 367144 reference follows использовали Source bindings; native defaults проверяются отдельным fixture.
* Выбор scalar/string-byte winners по максимуму `(source_order, local_order)` после linking. История duplicate transitions хранится flat списками индексов; нет vector на каждую value cell.
* Warning содержит предыдущий и текущий origin/event. Warnings выдаются в deterministic logical order. В этом slice string duplicates представлены per-cell transitions; пользовательская агрегация одного string statement — отдельная presentation/integration работа.

Проверены не только новые значения, но и реальная привязка alias target:

```cpp
a.ref = b;
a.ref.x = 1;
b.x = 2;
a.ref.x = 3;
```

Результат — `b.x = 3`, warnings с точными target spans для `(a.ref.x=1 → b.x=2)` и `(b.x=2 → a.ref.x=3)`. В случае последующего `a.ref=c` прежние writes через `a.ref` связываются с final referent `c`, а не исполняются как промежуточные C++ stores.

Это final-state semantics. Смена порядка ready models не меняет winner. В данном slice сравниваются прямой и обратный последовательные порядки extraction; это не доказательство thread safety или multiworker scaling.

## Проверки

CTest прошёл: **46 реальных C++ fixture invocations** (23 сценария, каждый в forward/reverse extraction).

Покрыты triple alias writes, final rebinding, nested binding target alias, reference chains, arrays, native constructor member binding, cross-file extern, namespace scopes, type/object shadowing, enum values/object shadowing, strings; cycle, unbound target, undeclared forward object, reference type mismatch, index bounds, duplicate definition, unresolved extern и const-error-before-later-syntax priority. Отдельно проверены точные origins signed literal с trailing spaces и многозначного array index.

Все retained declaration/write/BIND origins после local-model release проверены на существующий Source file и диапазон внутри его content. Эти проверки читают только file/range bounds в harness; глобальная semantic implementation Source content не читает.

Полные real-corpus correctness runs выполнены дважды: исходный и обратный порядок extraction. Оба прошли строгую projection сверку с независимым `compiled.bin` V2. Source files и baseline image не изменяются; новая pipeline ничего не записывает в `.serverengine`.

## Стоимость — пока ограничение

Это correctness prototype, не performance success.

| Capacity основных retained arrays | Значение |
|---|---:|
| Все local models | 182 960 089 bytes / 174,48 MiB |
| Bound transport + typed paths/provenance | 187 667 640 bytes / 178,97 MiB |
| Final result arrays | 39 195 108 bytes / 37,38 MiB |

Эти числа не являются полным peak RSS: не учтены все unordered_map nodes, allocator metadata, Header preparation, G и lexical storage. Local и bound representations некоторое время перекрываются по lifetime. Local models освобождаются до alias linking; после reduction остаются bound arrays и final result.

Одиночные диагностические stage clocks в финальной серии:

| Stage | Forward | Reverse |
|---|---:|---:|
| Extraction | 161,081 мс | 160,453 мс |
| Semantic linking | 310,987 мс | 306,363 мс |
| Reference linking + final reduction | 1134,13 мс | 1178,73 мс |

Нет per-token/member clocks. Это **не полный PUBLISH и не полный lifecycle**: Header/lexical setup, oracle checks, report emission и часть release находятся за stage windows. Нельзя складывать эти времена и сравнивать их с production PUBLISH как корректный A/B. Тем не менее alias/reduction stage и retained arrays уже показывают, что добавление workers только к extraction не решает стоимость текущей реализации.

## Границы готовности

Первый требуемый критерий выполнен: global этап использует выделенные declarations/typed paths/unresolved requests и не разбирает Source syntax повторно. Реальные UnitProXL Source представлены без gaps этого slice; это не утверждение полного языкового покрытия всех будущих проектов.

Ограничения до production:

* Нет parallel workers, streaming/backpressure, согласованного final Graph writer, SourceMap emission, нового compiled/runtime image или SHM materialization.
* Символический storage resolver поддерживает проверенную typed object/member/base/array/reference модель. Он не доказывает отсутствие произвольных partial byte overlaps для неподдерживаемых union/cast/ABI cases.
* Indirect Header binding targets явно отклоняются. Extended constructor/default patterns и unconnected Runtime placeholder storage не представлены общим алгоритмом; отсутствующий доступный binding/default вызывает fail-closed ошибку, а не выдуманный target. Это не изменение production политики unconnected references.
* Nested target linking использует ограниченный fixed-point prototype с cycle/nonconvergence rejection; общий proof всех допустимых dependency graphs и масштабирование не выполнены.
* Materialization/LOAD/BUILD persistence winners и восстановление предыдущего writer при удалении ещё не реализованы. Временные event ranks не добавлены в постоянную file model.
* Legacy byte parity не является gate новой семантики. Existing G projection сверяется только там, где значения не требуют нового alias-aware правила.

Дальнейший приоритет — оценить/сократить дублирование transport/lifetime и стоимость final-target indexing на стабильной семантике. Не переносить этот тяжёлый prototype в production и не приписывать ему ускорение PUBLISH. После этого можно добавлять parallel extraction, сохраняя установленную no-reparse boundary.

## Артефакты

Код и команды: `experiments/parallel-local-source-02/README.md`.

* `build/parallel-local-source-02/real-validation/{forward.json,reverse.json,summary.json}` — полный corpus, counts, timings и executable hash.
* `build/parallel-local-source-02/fixtures/*/{forward.json,reverse.json}` — конкретные expected winners, warnings, errors и origins.
* `build/parallel-local-source-02-tests.log` и `Testing/Temporary/LastTest.log` — результаты 46 fixture runs.

Ни performance adoption, ни полный новый PUBLISH в этом slice не заявляются.
