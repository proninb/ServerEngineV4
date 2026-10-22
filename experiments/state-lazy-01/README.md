# STATE-LAZY-01

Узкий эксперимент поверх EVALUATOR-ISOLATION-01. CONTROL вызывает исходный
изолированный evaluator; LAZY меняет только семь record-local container declarations
на `lazy_container<T>`. `prepare`, schema indices, `commit`, record_result,
provenance и local-output format непосредственно переиспользуются из ISOLATION-01.
Алгоритм evaluate_one и порядок задач сохранены.

Adapter хранит `std::optional<T>` внутри local state: пустые read operations
возвращают empty/missing без создания T. Первая запись вызывает обычный T(),
затем тот же стандартный emplace/operator[]/try_emplace. Никаких reserve,
других hash functions, bucket policies, allocator или concurrent structures.
После erase последнего элемента T остаётся созданным до конца record.
Итераторы незаполненного adapter не должны переживать первую запись; существующий
evaluator этому контракту соответствует. Это adapter для данного local state,
не универсальная замена стандартным контейнерам.

MSVC STL default unordered construction выделяет sentinel списка и bucket vector
(8 buckets, 16 iterator slots). Default map construction выделяет tree sentinel.
Итого шесть unordered containers + map дают 13 initial allocations на record.
`StateLazy01Allocations --container-probe` проверяет это на фактических семи типах,
zero allocations пустых LAZY reads и равенство allocation calls/bytes при первом
write сумме обычного construction + того же write в CONTROL.

```powershell
cmake -S experiments/state-lazy-01 -B build/state-lazy-01 -G "Visual Studio 18 2026" -A x64
cmake --build build/state-lazy-01 --config Release --target StateLazy01 StateLazy01Allocations -- /p:BuildProjectReferences=false
ctest --test-dir build/state-lazy-01 -C Release --output-on-failure
python experiments/state-lazy-01/run_matrix.py build/state-lazy-01 build/publish-subsecond-01/fixture/project.json build/state-lazy-01/results-lazy
```

Performance выполнять native вне sandbox, без builds/tests одновременно.
run_matrix сохраняет frozen sources/exe/engine-object SHA256; 3 fresh processes
при 1/16 extraction workers, по 5 measured pairs и 1 warmup pair. Evaluator
остаётся последовательным. В обоих режимах одинаковы 160 immutable schema indices,
fixed declaration slots и deterministic ordered commit. Размеры output и oracle
проверяются вне timed lifecycle. Allocation audit выполняется отдельным executable.

636 независимых V2 oracle checks, 1 980 scheduling/lifetime checks на каждый
executable и probe для всех семи container types. Сохраняются direct/nested values,
WHO/types, diagnostics и counters; проверены reverse/permuted tasks, pending,
stale generations, repeated definitions и commit после release requests/source.

Production PUBLISH не подключён. STATE-LAZY не расширяет Header grammar/coverage,
не вводит parallel evaluation и не меняет конечный G. Следующий отдельный этап —
EVALUATOR-PARALLEL-01 с фиксированными 16 extraction workers и отдельным sweep
1/2/4/8/16 evaluator workers.
