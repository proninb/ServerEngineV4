# ServerEngineV4 — V3-37R, corrective audit experiment

**Контроль:** неизменённый V3-37. **Кандидат:** V3-37R. V3-31 и Header Syntax Scanner тоже неизменны. Только Header Graph, не PUBLISH/LOAD.

## Исправления

1. `begin_direct_record` получает число **собственных деклараций**. Счётчик вычисляется по DFS диапазону с перескоком вложенных поддеревьев, без новых массивов и индексов. Это дополнительный обход; ускорение НЕ гарантируется.
2. `events.reserve()` не резервирует память под все 737k деклараций: предварительно только namespaces + records; typedef-события добавляются по мере обнаружения.
3. `v337r_owner_ranges.hpp` реализует общий путь перескока чужого owner и подсчёта, который вызывается в producer и напрямую тестируется.
4. Два новых Release-safe C++-теста с явными проверками (никаких `assert(scan(...))`); CTest зарегистрирован, PowerShell runner собирает и запускает tests даже при `-SkipBuild`.
5. Инвариант дескрипторов — **единственный генератор `header_syntax_v3_06::scan`**: индекс в диапазоне, в порядке возрастания и не более одного дескриптора на декларацию. Проверено тестом Scanner. На произвольно повреждённые внешние syntax_result V3-37R не рассчитан, чтобы не добавлять ещё один O(N) индекс/валидацию в hot path.

## Команда запуска (PowerShell)

```powershell
$pkg='C:\Boris\ServerEngineV4\build\v337r-package'
Expand-Archive "$env:USERPROFILE\Downloads\header-syntax-v3-37r.zip" -DestinationPath $pkg -Force
& "$pkg\header-syntax-v3-37r\run_v3_37r.ps1" `
  -RepoRoot 'C:\Boris\ServerEngineV4' `
  -ProjectPath 'C:\Boris\ServerEngineV4\build\unitproxl-transfer-check\project.json' `
  -Pairs 21 -WarmupPairs 2
```

Отчёты: `C:\Boris\ServerEngineV4\build\benchmarks\v337r-*.csv` (создавать только если отсутствуют). Сборка: `build\benchmarks\_experimental_v337r`. Контроль и кандидат проверяются независимо против V2 Header Graph по WHO/WHERE. `full_PUBLISH_gate=PARTIAL`.

Для повторного прогона без пересборки: `-SkipBuild -OutputDirectory <новый каталог>`.

**Не перемещать в production по итогам локальных smoke-тестов.** Принятие только после MSVC Release / UnitProXL, парных A/B и parity по каждой паре.
