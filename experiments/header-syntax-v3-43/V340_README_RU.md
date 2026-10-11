# ServerEngineV4 — Header Graph V3-40: глубокий профиль разрешения типов

**Тип эксперимента:** только диагностика, не заявленная оптимизация.

- Контроль: неизменённый V3-38A (V3-37 + исправленный `events.reserve`).
- V3-40: тот же алгоритм + диагностические счётчики и таймеры, активные **только** в отдельном профилированном проходе.
- Парный прогон V3-38A vs V3-40 (31 пара) показывает только **стоимость/возмущение инструментирования**. Его нельзя трактовать как выигрыш нового алгоритма.
- После A/B отдельно выполняется профилированный проход V3-40 и независимое сравнение с Header V2 oracle, включая WHO/WHERE и constructors.
- Измеряются: попадания/miss именованных типов, отклонения простого имени грамматикой, категории fallback (scope/`__int64`/modifiers/прочее), число слоёв derived, глубина лексического scope, стадии named resolver. Отчёт: `v340-resolution.csv`.
- Никаких новых индексов, кешей, изменений семантики или алгоритма Graph; production working tree не затрагивается.
- Full PUBLISH = PARTIAL.

## Запуск (PowerShell)

```powershell
$repo = 'C:\Boris\ServerEngineV4'
$pkg = Join-Path $repo 'build\v340-package'
Expand-Archive "$env:USERPROFILE\Downloads\header-syntax-v3-40.zip" -DestinationPath $pkg -Force
& "$pkg\header-syntax-v3-40\run_v3_40.ps1" `
  -RepoRoot $repo `
  -ProjectPath "$repo\build\unitproxl-transfer-check\project.json" `
  -Pairs 31 -WarmupPairs 2 -SampleStride 32
```

Отчёты в `C:\Boris\ServerEngineV4\build\benchmarks\`, включая `v340-resolution.csv`, `v340-type-samples.csv`, `v340-phases.csv`, `v340-review.md`. Старые файлы не перезаписываются. Overlay в `build\benchmarks\_experimental_v340`.

**Ограничения:** времена измеряются на выборке и включают накладные расходы таймеров. Вложенные измерения (`named_ms`, `scope_lookup_ms`, `cache_probe_ms`) нельзя суммировать с `base_ms`. Учитывайте резкую разницу частот CPU между прогонами. MSVC/UnitProXL ещё не запущены при создании пакета.
