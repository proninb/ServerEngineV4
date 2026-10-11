# ServerEngineV4 — V3-37 Direct Ranges (эксперимент)

**Срез:** eliminar `by_owner` + `descriptors` в независимом Header V3 Graph Producer. Frozen control V3-31 полностью сохранён (SHA256 `97d3b813e120486fcd804e21f1d28fba1fab430ab057cee213625ef4c668333d`).

## Алгоритм

- Используются существующие диапазоны `syntax_record_03.declaration_first/count`; проверка `syntax_declaration_03.owner` отфильтровывает вложенные записи; их DFS-диапазоны пропускаются целиком (`nested_end`) без дополнительных структур.
- `syntax_result_03.declarators` уже упорядочен по `declaration`: `std::lower_bound` **один раз на record**, затем монотонный `descriptor_it` во время обработки полей.
- `by_owner` и массив `descriptors` отсутствуют. Нет нового CSR, новых данных в синтаксическом ABI или изменённого Graph.
- `begin_direct_record` получает консервативную верхнюю границу `declaration_count`, которая для родителя может включать вложенные декларации: проверка overhead на UnitProXL обязательна.
- Обработка constructor и method return в прежнем лексическом порядке; проверка nested `owner` в обеих петлях.
- Проверка `invalid/duplicate descriptor` в старом индексирующем цикле убрана. Предпосылка: единственный `header_syntax_v3_06::scan()` эмитирует максимум один дескриптор на декларацию, монотонно. Нужен parity gate; нет поддержки произвольного внешнего/повреждённого `syntax_result_03`.

## PowerShell

Из `C:\Boris\ServerEngineV4`:

```powershell
$dir = 'C:\Boris\ServerEngineV4\build\v337-package'
Expand-Archive "$env:USERPROFILE\Downloads\header-syntax-v3-37.zip" -DestinationPath $dir -Force
& "$dir\header-syntax-v3-37\run_v3_37.ps1" `
    -RepoRoot 'C:\Boris\ServerEngineV4' `
    -ProjectPath 'C:\Boris\ServerEngineV4\build\unitproxl-transfer-check\project.json' `
    -Pairs 21 -WarmupPairs 2
```

**Отчёты:** `C:\Boris\ServerEngineV4\build\benchmarks\v337-*.csv`.

**Повторный запуск:** существующие CSV не перезаписываются. Переместите их либо передайте новый `-OutputDirectory`. `-SkipBuild` допускается только для ранее построенного overlay.

**Статус:** локальные статические/GCC-smoke проверки не являются тестом MSVC/UnitProXL. Пока нет результатов 21-парного AB + WHO/WHERE parity, V3-31 остаётся accepted baseline; full PUBLISH parity `PARTIAL`. Эксперимент не пишет в production.
