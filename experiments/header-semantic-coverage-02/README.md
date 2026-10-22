# HEADER-SEMANTIC-COVERAGE-02: reference operands and targets

Изолированный reference slice поверх SC01. Scalar semantics сохранена;
TypeRequest, canonicalization, WHO и алгоритм ordered assembly не изменены.
Evaluator остаётся последовательным между records. Production PUBLISH не подключён.

Workers извлекают `reference` operation: destination name, referent name,
assignment event и отдельные physical origins destination/referent. Поддержан
Header V2 синтаксис `name` и `this.name`. Никакого глобального reference index,
String Table или WHO внутри workers нет.

Один ordered evaluator обрабатывает scalar и reference events вместе:

- future destination сохраняет последнее pending operation;
- существующий reference destination с ещё отсутствующим referent создаёт
  waiting entry с generation и сбрасывает значение в zero;
- успешный scalar или immediate reference override увеличивает существующую
  generation;
- при появлении referent проверяется точное соответствие типов, и только
  затем generation; актуальный waiter записывает one-based member operand,
  stale waiter удаляется без изменения текущего значения;
- stale waiter с несовместимым типом всё ещё даёт ошибку, а несуществующий
  referent всё ещё диагностируется в конце record, даже после override;
- pending destination errors в конце record имеют приоритет над unresolved
  referent errors, как в Header V2.

Target должен иметь outer lvalue/rvalue-reference modifier. С source снимается
один outer reference modifier; затем требуется точное совпадение leaf/type
shape. Дополнительные C++ qualification conversions не добавлены.

Nested targets, member initializers и structural/grammar gaps сохраняются.
Вся gapped definition исключается из value parity. Извлечённые references
внутри неё классифицируются отдельно и не выдаются за разрешённые.

## Сборка и проверка

Требуются ранее собранные MSVC Release engine objects SB03.

```powershell
cmake -S experiments/header-semantic-coverage-02 -B build/header-semantic-coverage-02 -G "Visual Studio 18 2026" -A x64
cmake --build build/header-semantic-coverage-02 --config Release --target HeaderSemanticCoverage02 HeaderSemanticCoverage02Allocations -- /p:BuildProjectReferences=false
ctest --test-dir build/header-semantic-coverage-02 -C Release --output-on-failure
python experiments/header-semantic-coverage-02/run_matrix.py build/header-semantic-coverage-02 build/publish-subsecond-01/fixture/project.json build/header-semantic-coverage-02/results-references
```

Output directory должен быть новым. Timing native и последовательный.
`scalar01` — common control с constructors enabled и references disabled;
`references` — новое расширение. Оба используют один input, pool и новый IR.
Frozen SC01 executable — отдельный bridge, не парный контроль цены расширения.
Reference name/origin расширили operation record с 64 до 96 байт; это новый
семантический payload, а не изменение TypeRequest.

Lifecycle включает discovery/planning, extraction, assembly, Type Binding,
совместный constructor/reference evaluator, numbering, consumer и teardown.
Capacity accounting и oracle находятся снаружи. Сохраняются source snapshots,
raw CSV/stdout/stderr, executable/engine hashes и frozen bridge.

## Coverage and parity

Reference classification: `extracted_reference_ops == reference_events +
references_in_gap_definitions` для positive real corpus. Глобальное утверждение
проверяется до измерения и в matrix driver. Последняя группа остаётся excluded.

Final defaults содержат CV kind/bits/**operand**; checksum включает operand.
V2 oracle сравнивает полные construction_value. Exact scheduling comparison
также сверяет kind/detail/origin первого diagnostic, WHO/types/events,
destinations и construction output. Результат проверяется после уничтожения
requests, tokens и исходного текста, после move/copy и heap churn; последняя
shape arena освобождается после последнего consumer.

Правила waiting generations проверены отдельными positive/negative fixtures,
включая stale incompatibility и stale unresolved name. Queue/generation paths
не выдаются за измеренные на реальном корпусе, если их runtime counters нулевые.

Не добавлены nested paths, member initializer semantics, остальные callable,
layout/inheritance, полный diagnostics/String IDs/SourceMap contract или Direct G.
`category_status=partial` остаётся корректным статусом UnitProXL, пока есть gaps.
