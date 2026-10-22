# Связанные запросы Source resolver

Добавлены source_endpoint_request и source_endpoint_result. Вход содержит WHO
объекта и borrowed span селекторов member(string_id)/index(uint64). Имена уже
интернированы, WHO уже разрешён по действующей видимости. Результат владеет
typed path, root/result типом и упорядоченными зависимостями. Ошибка возвращает
категорию и индекс первого неразрешённого селектора; source locations остаются
ответственностью binder/caller. Входные spans должны жить до завершения вызова.

source_endpoint_resolver::resolve не обращается к курсору, string_table,
identity_space или Source Map и не меняет G. Для frozen Graph он может работать
на worker с собственными resolver/cache/result. Один resolver не разделяется
между потоками. Параллельная мутация контейнеров G по-прежнему запрещена.

begin_endpoint, prepare_member, append_member и append_index используются и
полным bound-request API, и последовательным parser. Поэтому новый worker-ready
путь не является вторым алгоритмом разрешения членов/массивов. Parser сохраняет
порядок синтаксических проверок, включая проверку record/array до чтения
соответствующего имени/индекса. Binding, Source Map и commit пока остаются у owner.

Регрессия разрешает запросы с inheritance, array и reference сначала
последовательно, затем на двух native execution lanes с независимыми resolver
контекстами. Сравниваются WHO, типы, path, зависимости, status, ошибка и её индекс.
Дополнительно проверяются out-of-range и сброс старого результата при invalid WHO.
Числа Graph endpoint paths, initializations и links после разрешения неизменны.

## Статус интеграции

Это независимый вход resolver, а не включённый параллельный диспетчер PUBLISH.
Native workers вызывают bound API в регрессии. Рабочий parser применяет те же
шаги последовательно. Окно commit 64 остаётся опциональным и OFF по умолчанию.
Следующая часть — накопление bound requests в parser и сохранение первой ошибки
при взаимодействии binder, resolver и commit; последующий запуск worker batch
должен заменить последовательное разрешение, а не продублировать его.

Обновление: реальный dispatcher проверен в эксперименте 28 и отклонён из-за
регрессии; см. `PUBLISH_SOURCE_PARALLEL_DISPATCH_28.md`. Рабочий статус выше сохранён.

## Проверки

Пройдены frontend regressions, девять интеграционных сценариев и девять точных
сравнений ошибочной диагностики. Все 14 exploratory benchmark/warmup вызовов
сохранили SHA-256 двух файлов и прошли независимый baseline audit.
Три обычные пары: 1399.477 ms baseline против 1402.323 ms candidate. Две отдельные
stage пары: 1408.090 против 1412.360 ms total и 458.152 против 467.504 ms Source.
Малые серии не подтверждают ускорение. Изменение структурное.

Финальная обычная конфигурация прошла 22/22 CTest tests. Отдельная конфигурация
с окном 64 прошла frontend regressions. Полный PUBLISH обеих конфигураций
сравнивается с эталонными SHA-256 и независимым audit. Бинарные файлы и логи:
`build/publish-source-bound-resolver-27/`.
