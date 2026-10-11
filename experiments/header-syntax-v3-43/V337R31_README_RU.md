# ServerEngineV4 — V3-37R31 / независимые 31 пара + Fields

## Цель

Повторить **V3-37 vs V3-37R** на **31 паре** в MSVC Release и изучить фазу `Fields` без переноса изменений в production. Базовый Git SHA: `6f50a4e2a2963f1dab46b993203cc579d71a3415`, ветка проверяется локальным Git, repository `proninb/ServerEngineV4`.

Пакет содержит **замороженные** V3-37 и V3-37R producer, а также исходный `v3_37r_perf_benchmark.cpp`: их SHA256 проверяются до сборки. Старые файлы/отчёты `v337r-*` не изменяются. Анализ `Fields` собирается как **отдельный** C++ exe с другим producer-файлом, не встраивается в A/B.

## Запуск PowerShell

```powershell
$repo = 'C:\Boris\ServerEngineV4'
$dest = Join-Path $repo 'build\v337r31-package'
Expand-Archive "$env:USERPROFILE\Downloads\header-syntax-v3-37r31.zip" -DestinationPath $dest -Force
& "$dest\header-syntax-v3-37r31\run_v3_37r31.ps1" `
    -RepoRoot $repo `
    -ProjectPath "$repo\build\unitproxl-transfer-check\project.json" `
    -Pairs 31 -WarmupPairs 2 -SampleStride 32
```

**Результаты строго в:** `C:\Boris\ServerEngineV4\build\benchmarks\`

```text
v337r31-pairs.csv
v337r31-summary.csv
v337r31-phases.csv
v337r31-type-samples.csv
v337r31-allocations.csv
v337r31-fields.csv
v337r31-review.csv
v337r31-review.md
```

Сборка отдельно от production: `build\benchmarks\_experimental_v337r31\`.

Если отчёт уже существует, скрипт **останавливается и не перезаписывает его**. Повтор без пересборки — `-SkipBuild` с **новым** `-OutputDirectory` И `-ExistingBuildDirectory C:\Boris\ServerEngineV4\build\benchmarks\_experimental_v337r31\build`; при `-SkipBuild` CTest выполняется заново. Если первая сборка была частично прервана, не смешивайте результаты разных билда: используйте новую изолированную директорию. Для этого можно задать другой `-OutputDirectory`.

## Проверки

1. Pinned Git HEAD + чистые отслеживаемые файлы; источник в production не правится.
2. Frozen SHA256 (V3-37, V3-37R, Syntax Scanner, perf harness), собственные regression CTest на Release.
3. 2 warmup + 31 измеренных пары с попеременным порядком, V2 Header Graph WHO/WHERE parity для **каждой** пары.
4. Отдельный allocation count.
5. Отдельное Fields-профилирование в альтернативном exe с собственной V2 parity.
6. Python-валидатор пересчитывает метрики непосредственно из CSV; два порядка A/B, точный двухсторонний sign test, чувствительность к двум самым крайним выбросам.

## Ограничения

- `full_PUBLISH_gate=PARTIAL`; Object/Assign/Source/Links/persistence parity отсутствует.
- Времена `Fields` и таймеров `produce_type`, `initializer` и т.д. — **неодинаковые области измерения**. Детализация записывается только для sample records (`stride=32` по умолчанию); таймеры вложены и включают overhead измерения. **Не умножать sample-ms на 32**, не складывать с `Fields` как независимые слагаемые.
- MSVC Release / UnitProXL в этом окружении не доступны; подтверждение выполнения должно прийти с пользовательской Windows машины.
- Никаких новых кешей, owner indexes или production PUBLISH/LOAD изменений.
