# HEADER-SEMANTIC-COVERAGE-03: nested constructor targets

Изолированный эксперимент поверх SC02. Production PUBLISH не подключён.
Evaluator последовательный; TypeRequest, canonicalization, WHO и discovery HC01 сохранены.

Workers извлекают путь из `.field` / `[decimal-index]` в локальную арену.
Operation хранит arena/offset/count; assembly переносит владельца арены и
сохраняет исходные event и physical origin корневого target. WHO в workers нет.
Итоговый nested default владеет строкой пути и CV; Parser path/request arenas
освобождаются до consumption. Shape arenas сохраняют прежний SB04 lifetime.

Evaluator воспроизводит Header V2:

- прямые record fields и bounded arrays; поиск только собственных members;
- schema доступна только после complete event исходного определения;
- полный индекс members создаётся по первому реальному traversal данного типа;
- reference/pointer/cv не разворачиваются при обходе;
- конечный target — intrinsic без modifiers, operand — scalar constant;
- все pending nested operations выполняются по порядку после direct pending
  и waiting references при появлении корневого поля;
- повторный path внутри definition заменяет значение; между definitions
  одинаковые значения объединяются, несовпадающие дают V2 diagnostic;
- first diagnostic проверяется по kind/detail/file/offset/length.

Для структурно некорректного root SC03 запускает тот же extractor целиком:
это сохраняет nested syntax diagnostics даже при отказе discovery от split.
Неподдержанная структурная грамматика остаётся явно отмеченным gap.

Нормальная сборка и allocation-сборка проверяют scheduling и lifetime.
Python oracle suite сравнивает положительные/отрицательные fixtures с Header V2,
включая многомерные массивы, arrays of records, late roots, overwrite,
повторные definitions, inherited-member rejection и depth limit.

```powershell
cmake -S experiments/header-semantic-coverage-03 -B build/header-semantic-coverage-03 -G "Visual Studio 18 2026" -A x64
cmake --build build/header-semantic-coverage-03 --config Release --target HeaderSemanticCoverage03 HeaderSemanticCoverage03Allocations -- /p:BuildProjectReferences=false
ctest --test-dir build/header-semantic-coverage-03 -C Release --output-on-failure
python experiments/header-semantic-coverage-03/run_matrix.py build/header-semantic-coverage-03 build/publish-subsecond-01/fixture/project.json build/header-semantic-coverage-03/results-nested
```

Performance запускать native вне sandbox, без параллельных builds/tests.
run_matrix фиксирует исходники, exe и engine-object SHA256; 3 fresh processes
на 1/16 workers, по 5 измеренных пар + 1 warmup pair. Контроль `references02`
использует тот же operation format и framework, но nested выключен.
Frozen SC02 bridge — отдельная проверка издержек framework, не парный контроль.
В lifecycle включены discovery, planning, extraction, assembly, Binding,
sequential evaluator, numbering, consumption и освобождение данных.
Validation/audit scans выполняются вне таймера.

Полное покрытие constructor projection данного корпуса не означает полную
Header V2 parity: member initializers, остальные callable/layout rules,
String IDs и SourceMap ещё требуют отдельных slices. Следующий эксперимент —
EVALUATOR-SCALING-01 с breakdown индексов/lookup/types/pending/output.
