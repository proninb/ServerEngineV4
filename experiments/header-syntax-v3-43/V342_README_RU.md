# ServerEngineV4 V3-42

Проверяем только стоимость обнуления массива `type_spec.layers[32]`. Базовый V3-38A побайтно сохранён; копии namespace нужны для честного A/B без совместного изменения type_spec. Используются только слоты `[0,length)`, записываемые через `push()`. Лимит 32 и ошибка `too_deep` при 33-м сохранены.

Запуск из PowerShell:

```powershell
$repo = 'C:\Boris\ServerEngineV4'
$dest = "$repo\build\v342-package"
Expand-Archive "$env:USERPROFILE\Downloads\header-syntax-v3-42.zip" -DestinationPath $dest
& "$dest\header-syntax-v3-42\run_v3_42.ps1" -RepoRoot $repo -ProjectPath "$repo\build\unitproxl-transfer-check\project.json" -Pairs 31 -WarmupPairs 2
```

Отчёты: `build\benchmarks\v342-{pairs,phases,type-samples,summary,allocations}.csv`, `v342-review.md`. Результаты существующих запусков не перезаписываются. Изменений production нет.
