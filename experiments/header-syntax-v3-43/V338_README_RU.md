# ServerEngineV4 — V3-38 (A/B декомпозиция V3-37R)

## Назначение

Проверить независимо две модификации, ранее включённые одновременно в V3-37R:

- **V3-38A — только `events.reserve()`**: уменьшение запроса от `namespaces + records + all declarations` до `namespaces + records`. Сохранены push/replay/sort событий, включая typedef; контейнер вправе динамически расти. Никакой другой логики графа не меняется.
- **V3-38B — только Graph reservation**: подсчёт `owned_declaration_count()` при `begin_direct_record()` вместо `rec.declaration_count`. `events.reserve()` остаётся исходным. Дополнительный проход по декларациям может замедлить producer; аллокации не обязательно сократятся.

Контроль **V3-37**, байты `v3_37_graph_producer.inc` сохранены и проверяются SHA-256. В пакете сохранён V3-37R как историческая контрольная точка, но он не участвует в парных измерениях V3-38.

**Новые owner-index/CSR/descriptor-index не вводятся.** Проверяется независимый Native Header Graph V3 против Header V2 oracle с WHO/WHERE и constructor/nested semantics. Full PUBLISH Gate = **PARTIAL**; не интегрировать production PUBLISH/LOAD.

## PowerShell / MSVC Release

```powershell
$repo = 'C:\Boris\ServerEngineV4'
$pkg  = "$repo\build\v338-package"
Expand-Archive "$env:USERPROFILE\Downloads\header-syntax-v3-38.zip" -DestinationPath $pkg -Force

& "$pkg\header-syntax-v3-38\run_v3_38.ps1" `
    -RepoRoot $repo `
    -ProjectPath "$repo\build\unitproxl-transfer-check\project.json" `
    -Pairs 31 `
    -WarmupPairs 2 `
    -SampleStride 32
```

Все CSV находятся **непосредственно** в `C:\Boris\ServerEngineV4\build\benchmarks\` (не в папке с датой):

- `v338a-pairs.csv`, `v338a-summary.csv`, `v338a-phases.csv`, `v338a-type-samples.csv`, `v338a-allocations.csv`
- `v338b-pairs.csv`, `v338b-summary.csv`, `v338b-phases.csv`, `v338b-type-samples.csv`, `v338b-allocations.csv`
- `v338-review.csv`, `v338-review.md` — независимая проверка и агрегирование результатов.

Изолированная сборка внутри `build\benchmarks\_experimental_v338\`.

Отчёты **никогда не перезаписываются**. Переиспользование уже собранного overlay и новых отчётов: `-SkipBuild` (не сносит и не изменяет Git tree). Можно выбрать один вариант с `-Mode A` / `-Mode B`; такой запуск не создаёт объединённый review. Для повторных измерений используйте новый `-OutputDirectory` либо сохраните и освободите имена соответствующих отчётов; при смене каталога нужна отдельная сборка. Для нового ZIP-директория, если target уже распакован, `-Force` подходит лишь для пакета, но runner не перезаписывает результаты.

## Проверки

Перед сборкой: `python tests/test_v338_source_contract.py` (запрещены дополнительные изменения producer). Затем два Release-safe CTest теста (`V338RangeTests`, `V338ScannerTests`). Для каждого кандидата 31 парное A/B, 2 warmup, чередование порядка, WHO/WHERE parity на каждой паре и отдельный allocation-проход. Внутренний sampled profiler запускается **после** чистого A/B и не должен использоваться для сравнения абсолютных времен с непрофилированными итерациями.

`tools/analyze_v338.py` независимо читает CSV, проверяет парную арифметику, метрики в summary, parity-строки, метрики аллокаций, порядок запуска, sign test и создаёт `v338-review.md`. Исключительно контрольный sign test не заменяет повторные аппаратные измерения. Медиану **парных разностей** используем как основную метрику.

## Что пока НЕ подтверждено

Здесь есть компиляция GCC + mock Graph, Release-safe тесты и короткие mock-прогоны. **MSVC Release/UnitProXL, число аллокаций и ускорение на большом реальном проекте ещё требуют вашего запуска.** PUBLISH parity остаётся PARTIAL. Производственный репозиторий в пакете не изменяется.
