# Source: собственные endpoint paths и отложенная канонизация

Следующий срез после read-only resolver: нормальный Source путь сохраняет typed
endpoint steps внутри результата endpoint. Target и source независимы от общего
scratch buffer. Два результата переиспользуются между statements: reset очищает
содержимое, сохраняя capacity. Аллокация на каждый endpoint не вводится.

read_endpoint возвращает WHO, object/result type, source location и локальный
путь. canonicalize_endpoint отдельно создаёт канонический endpoint_ref.
Для scalar/reference assignment канонизация выполняется после проверки всего
statement. Связь сохраняет порядок target path, source path, Graph commit.
Успешный вход сохраняет прежние path handles и сериализованный порядок G.
Ошибочный statement может оставить меньше временных path records, чем раньше;
неуспешный PUBLISH по-прежнему не публикует артефакты.

Строковые записи используют сохранённый target.path, включая ключ существующего
string-path cache. Их target канонизируется до разбора/разворачивания строки,
как раньше. Last-write-wins, Source Map и предупреждения сохраняют порядок.
Экспериментальный Source Model пока использует прежнюю непосредственную
канонизацию в своём отдельном пути.

Новая регрессия связывает разные array/member paths двух WHO, затем выполняет
scalar assignment и повторный binding. Проверяются независимость operands,
target-before-source порядок path IDs, использование одного typed path разными
WHO, итоговое значение и предупреждение о замене.

Это подготовка данных для bounded windows, а не включение параллельного Source.
Binding имён, зависимости и commit ещё последовательны. Окна, worker-контексты
и выбор первой ошибки из результатов окна остаются следующей частью.
Ускорение данным структурным изменением не заявляется.

Логи и baseline: `build/publish-source-owned-endpoints-25/`.

Frontend regressions, девять интеграционных сценариев и семь точных сравнений
диагностики прошли. Все 14 full-fixture вызовов с warmup сохранили SHA-256 двух
файлов и прошли независимый baseline audit. Три обычные пары дали 1421.117 ms
baseline против 1401.867 ms candidate; отдельные две stage пары — 1401.535 против
1381.245 ms total и 447.758 против 441.109 ms Source. Эти малые серии служат
проверкой изменения и не устанавливают подтверждённого ускорения.

Финальная сборка прошла 22/22 CTest tests. В каталоге PUBLISH остались только
compiled.bin/runtime.bin с эталонными SHA-256.
