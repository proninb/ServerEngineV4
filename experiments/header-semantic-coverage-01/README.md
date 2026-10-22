# HEADER-SEMANTIC-COVERAGE-01 — first isolated slice

Полное Header V2 coverage пока не достигнуто. Этот каталог добавляет только
managed constructors с прямыми scalar operands поверх HEADER-CHUNK-02.
Production PUBLISH, HC02 и SB04 не меняются.

Поддержаны constructor declaration, `= default`, bare `noexcept`, initializer
list и body assignments прямым полям; unsigned/signed decimal, binary64 real,
true/false/nullptr, пустой operand. Сохраняются CV kind и bits: numeric `0`
отличается от `false`/`nullptr`, `-0.0` сохраняет IEEE sign bit.

Новые events: constructor begin, scalar assignment, явный coverage gap.
События имеют original root/event/origin и local definition reference.
Ordered assembly HC02 переносит их в исходном порядке и переназначает только
definition index. WHO внутри workers не появляется.

`constructors.hpp` — отдельный evaluator после неизменённого Type Binding.
Порядок внутри record сохраняется: существующее поле проверяется сразу;
будущее поле хранит последнее pending assignment; при появлении поля pending
применяется. Несуществующий target диагностируется при завершении record.
Diagnostic decision event отделён от origin сообщения. Повторные definitions
сравниваются также по sparse constructor defaults. Финальный результат хранит
только sparse `(request index, construction_value)`; Parser operations/requests
освобождаются до consumer.

Reference operands, nested target paths, member initializers и structural
fallback — явные gaps. Любая такая операция исключает всю definition из
constructor-value parity; остальные поля/типы проверяются в прежней проекции.
Gapped definitions не выдаются за успешно разрешённые constructors.

`category_status=partial` означает неполное покрытие. `verified_values` означает
успешную независимую проверку constructor values в fixture без gaps;
`verified_diagnostic` — отрицательный fixture с проверенным первым diagnostic.
`projection/not_enabled` — контроль без новой семантики. `validation=passed`
само по себе не означает full Header parity.

Input adapter сохраняет уже подготовленные number kind/bits, без повторного
разбора literal в Binder. `number_kind` помещается в прежний padding token;
`sizeof(token)==48` проверено. Сравнение со старым HC02 — только bridge:
эффект расширения оценивается по общему projection control этого executable.

## Сборка и тесты

Требуются ранее собранные MSVC Release engine objects SB03.

```powershell
cmake -S experiments/header-semantic-coverage-01 -B build/header-semantic-coverage-01 -G "Visual Studio 18 2026" -A x64
cmake --build build/header-semantic-coverage-01 --config Release --target HeaderSemanticCoverage01 HeaderSemanticCoverage01Allocations -- /p:BuildProjectReferences=false
ctest --test-dir build/header-semantic-coverage-01 -C Release --output-on-failure
python experiments/header-semantic-coverage-01/run_matrix.py build/header-semantic-coverage-01 build/publish-subsecond-01/fixture/project.json build/header-semantic-coverage-01/results-constructors
```

Output directory должен быть новым. Timing native и последовательный.
Тесты: 240 scheduling comparisons в каждой сборке (8 inputs × workers
1/2/4/8/16 × targets 1/32/8192 × прямой/обратный enqueue order); 210 independent
V2 fixtures (35 inputs × workers 1/4/16 × targets 1/32768).
Для constructor errors сверяются kind, точный detail, file/offset/length.
Для type-projection errors остаются прежние category/origin проверки SB04.

## Измерения и границы

Общий immutable input и persistent pool. Projection/constructors чередуются;
3 fresh processes на 1 и 16 workers, по 5 измеренных пар и 1 excluded warmup
pair. Target 32768 фиксирован для аудита категории, не тюнинга scheduler.
Отдельны Type Binding и constructor resolution. Full semantic lifecycle
включает discovery, planning/extraction, assembly, оба resolution этапа,
numbering, consumption и освобождение данных. Capacity accounting/oracle снаружи.

Allocation executable отдельно требует 0 request instances перед consumer
и 0 tracked live bytes после последнего результата. Input/pool startup не
включены; requested peak не равен RSS. Текущее constructor resolution
последовательно обходит records; зависимость обязательна внутри record,
но глобальная последовательность между независимыми records — выбор этого
прототипа, а не доказанная необходимость архитектуры.

Следующие отдельные slices: reference targets, nested constructor paths,
member initializers, callable validation/polymorphism, layout/inheritance,
полный diagnostics contract, String IDs и SourceMap. Их coverage здесь
не подразумевается результатами scalar constructor slice.
