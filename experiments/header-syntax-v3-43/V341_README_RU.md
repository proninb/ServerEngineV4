# ServerEngineV4 — V3-41 (Fields measurement, diagnostic only)

Цель: определить реальную работу обработки полей Header Graph, не изменяя алгоритм.
Контроль: **V3-38A неизменённый**. V3-41 — это исходник V3-40 плюс только условные диагностические блоки.

Запуск `run_v3_41.ps1` из пакета (Windows/MSVC/PowerShell):

```powershell
$repo = "C:\Boris\ServerEngineV4"
& "$repo\build\v341-package\header-syntax-v3-41\run_v3_41.ps1" `
  -RepoRoot $repo `
  -ProjectPath "$repo\build\unitproxl-transfer-check\project.json" `
  -Pairs 31 -WarmupPairs 2 -SampleStride 32
```

По умолчанию отчёты: `C:\Boris\ServerEngineV4\build\benchmarks\v341-*.csv`.
Существующие имена отчётов не перезаписываются. Компиляция и overlay — в `build\benchmarks\_experimental_v341`, production tree не изменяется. Требуется HEAD `6f50a4e2a2963f1dab46b993203cc579d71a3415`.

Особые CSV: `v341-fields.csv` — sampled поля и раздельные операции;
`v341-resolution.csv` — прежняя независимая детальная диагностика разрешения типов;
`v341-pairs.csv` — контрольная парная проверка Graph parity, **не speedup**.

Инструментирование в sampled records выполняет таймеры **внутри очень коротких операций**; абсолютная стоимость и «прочее» имеют смещение из-за таймеров, branch и perturbation. Количество замеров и вложенные длительности не равны полной стоимости по корпусу. Даже 31-парный A/B показывает стоимость инструментирования, а не выигрыш.

Диагностические данные проходят проверки счётчиков, отрицательную проверку gate, WHO/WHERE Header parity для каждой пары и отдельного прогона. `full_PUBLISH_gate=PARTIAL`; не интегрировать в production без полной PUBLISH parity.
