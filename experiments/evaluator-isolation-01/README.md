# EVALUATOR-ISOLATION-01

Последовательный эксперимент поверх SC03. Никакого parallel evaluation и
STATE-LAZY: шесть unordered containers и completed_nested map каждого record
по-прежнему создаются обычным способом. Production PUBLISH не подключён.

`isolation.hpp` содержит отдельные функции:

1. `prepare`: actions в исходном порядке, canonical record metadata и подготовка
   индексов только для типов, встречающихся в nested paths. На UnitProXL — 160.
2. `evaluate_one`: принимает const batch/types/prepared/canonical, возвращает
   принадлежащий record результат. Не получает глобального binding_result.
3. `evaluate_all`: последовательно выполняет задания прямо, обратно или по
   hash permutation; результаты всегда записываются в original declaration slots.
4. `commit`: принимает только local results и конечный result; не получает batch,
   Parser requests, token strings, schema cache или canonicalizer context.

Prepared schemas читаются через const context. При каждом runtime traversal
проверяется `schema.complete <= decision event`. Preparation не генерирует
diagnostics и не разрешает раннюю видимость будущих definitions.

Local output владеет normalized member names и nested paths, хранит CV, исходные
assignment/decision events и physical origin. Pending references сохраняют
исходный assignment event даже при разрешении на более позднем member event.
Record descriptor хранит owner, definition, begin/complete и origins для commit.
Передача provenance увеличивает временные payloads; это не финальный Direct G
format. Final bound result остаётся прежним и не удерживает local outputs.

Ordered commit обходит original declaration slots. Local diagnostics объединяются
strict-earliest сравнением с прежним Parser/Binding seed issue; равный event не
вытесняет уже выбранное сообщение. Повторные complete definitions сравниваются
по нормализованным direct values; nested defaults объединяются/конфликтуют по
owner/path после direct definition check. Порядок локального вычисления на этот
порядок commit не влияет.

```powershell
cmake -S experiments/evaluator-isolation-01 -B build/evaluator-isolation-01 -G "Visual Studio 18 2026" -A x64
cmake --build build/evaluator-isolation-01 --config Release --target EvaluatorIsolation01 EvaluatorIsolation01Allocations -- /p:BuildProjectReferences=false
ctest --test-dir build/evaluator-isolation-01 -C Release --output-on-failure
python experiments/evaluator-isolation-01/run_matrix.py build/evaluator-isolation-01 build/publish-subsecond-01/fixture/project.json build/evaluator-isolation-01/results-isolation
```

Native performance выполнять вне sandbox, без builds/tests одновременно.
Парный контроль вызывает исходный SC03 resolver в том же executable; nested
coverage включён в обоих режимах. Все validation scans, size accounting и oracle
вне measured lifecycle. Включены preparation, local evaluation, ordered commit,
освобождение local/prepared state и весь прежний semantic lifecycle.

Тесты сравнивают exact types/WHO/events/origins, direct/nested CV и counters с SC03,
а также Header V2 oracle. Есть detached commit после освобождения requests,
path arenas, tokens и source text; отдельные случаи repeated definitions с
длинными owned names, nested merge/conflict и equal-event diagnostic priority.

Поддерживается только прежняя constructor/type projection. Full Header V2/G,
String IDs/SourceMap parity и production PUBLISH не заявляются. Последующие
STATE-LAZY-01 и PARALLEL-01 должны оставаться отдельными экспериментами.
