# ServerEngineV4 V3-43 — аудит оптимизации type_spec

Это **проверочный пакет**, не новая оптимизация. Контроль V3-38A и ускоренный V3-42 оставлены побайтно неизменными.

Из папки репозитория, в **Developer PowerShell for Visual Studio** (для `cl /FAcs`):

```powershell
$repo = 'C:\Boris\ServerEngineV4'
$pkg = "$repo\build\v343-package"
Expand-Archive "$env:USERPROFILE\Downloads\header-syntax-v3-43.zip" -DestinationPath $pkg
& "$pkg\header-syntax-v3-43\run_v3_43.ps1" -RepoRoot $repo -ProjectPath "$repo\build\unitproxl-transfer-check\project.json" -Pairs 31 -WarmupPairs 2 -RequireAssembly
```

Отчёты напрямую в `build\benchmarks`: `v343-pairs.csv`, `v343-reverse-pairs.csv`, `v343-allocations.csv`, `v343-codegen.md`, `v343-review.md`, и стандартные `phases/summary/type-samples`. Параметр `-RequireAssembly` требует присутствия `cl.exe` в PATH; без него сборка assembly будет отмечена SKIPPED.

Проверяются границы 0/1/2/31/32/33; отрицательная диагностика 33-го `push`, чтение лишь `[0,length)`, сохранение WHO/WHERE. Сгенерированная assembly относится к изолированной функции, а не к целому Graph producer. Для исследования production-кодогенерации потребуются отдельные MSVC disassembly и source maps.
