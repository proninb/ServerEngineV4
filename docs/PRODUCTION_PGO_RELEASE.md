# Контролируемый production PGO release

2026-10-27. Основание: [PRODUCTION-PGO-01](PRODUCTION_PGO_01.md), ACCEPT обученного USE-профиля. Это процедура сборки и проверки; рабочий сервер не переключён. `SERVER_ENGINE_PRODUCTION_PGO=OFF` остаётся default. Для отсутствующей, устаревшей или неподтверждённой PGD использовать обычный OFF release.

## Измеренный кандидат

Доказательства: `build/production-pgo-current-02/`. Точный измеренный USE executable: `ab-01/binaries/pgo/ServerEngineV4.exe`, SHA-256 `98e8c489ff1b6d03f4058151ef6833d3750289e3483656e49d76c2c5a6617bcf`. Контрольный OFF image сохранён рядом для сравнения. PGD: `compiler/production.pgd`; source/toolchain/build evidence зафиксированы в bundle.

Проверка без запуска сервера и без deployment, из корня репозитория:

```powershell
python scripts/verify_production_pgo.py build/production-pgo-current-02 --source build/production-pgo-current-02/source
```

Чтобы сопоставить кандидат с текущим checkout, указать `--source .`. Проверяется полный набор CMakeLists.txt/server_engine/tests и хеши файлов, включая локальные изменения. Один git commit недостаточен. Завершённая current-02 проходит; прерванная current-01 отклоняется. Это integrity gate для точных измеренных images, а не инструмент сертификации любой новой сборки. Проверка не запускает сервер, не восстанавливает карантин, не меняет Norton и не даёт deployment approval. Верификатор и манифесты должны поступать из доверенного release-хранилища: хеши не заменяют подпись/контроль доступа.

## Новый release или новый профиль

1. Зафиксировать исходники и полный упорядоченный input manifest, compile definitions, MSVC/linker/SDK версии, target/ABI, `/O2 /GL /LTCG`, Release и PGO OFF. Разделить профиль production target и benchmark. Сохранить хеш обычного executable до обучения.
2. На той же базе собрать production GENERATE с отдельным абсолютным путём к новой PGD. Не использовать старую benchmark PGD или профиль другого release. Для инструментированной сборки предоставить `pgort140.dll` соответствующего MSVC x64 toolchain.
3. Обучить настоящий executable через поддерживаемые PUBLISH/LOAD/runtime и BUILD/REBUILD сценарии на копиях репрезентативных проектов. Помимо обучающего UnitProXL предусмотреть holdout-проекты, не участвующие в training. Сохранять каждый завершённый PGC и нормальное завершение сервера; сбои не считать завершёнными проходами.
4. Объединить PGC соответствующим `pgomgr`, сохранить toolchain/log/PGD hashes. Собрать USE на той же базе и с той же PGD. Проверить отсутствие instrumentation в timed image. Любая смена исходников, compiler/linker, ABI или ключевых флагов требует нового цикла; CMake проверяет наличие PGD, но само её наличие не доказывает совместимость.
5. Зафиксировать точные OFF/USE images и повторить cold validation: артефакты, live native SHM, LOAD, values/links, RUN/FREEZE, поддерживаемые BUILD/REBUILD, диагностики и отказные сценарии. Исключение изменяемого system header документировать отдельно. Не запускать image, вновь заблокированный Norton, через переименование/смену пути.
6. Измерить production TCP PUBLISH unsandboxed на одном неизменяемом fixture: прогрев, минимум 15 чередующихся парных блоков, полные сырые результаты; startup, hashes и подробные таймеры вне основной границы. Сохранить CPU и memory отдельно. Не объединять прерванные серии. Для новых workloads/длительной жизни процесса нужны собственные проверки, не вывод из UnitProXL.
7. Создать release manifest: source/toolchain/config, PGD, OFF/USE hashes, fixture manifests, validation, полная A/B-статистика и решение. Подписать/архивировать принятой командой release-системы. Продвигать точный проверенный executable; новый link означает новый image и повторную проверку.

Режимы CMake для этого процесса:

```text
-DSERVER_ENGINE_ENABLE_IPO=ON
-DSERVER_ENGINE_PRODUCTION_PGO=OFF|GENERATE|USE
-DSERVER_ENGINE_PRODUCTION_PGO_DATABASE=<absolute-path-to-matching-production.pgd>
```

Процесс не делает USE глобальным default и не подставляет случайную PGD из build-каталога. При невозможности подтвердить профиль безопасный сборочный fallback — OFF, с явной записью того, какая конфигурация выпущена.

## Условия перед рабочим deployment

- Завершить отдельный разбор сообщённых пользователем ошибок TCP login и обработки отказов, сохранить точные reproducer/исправления/регрессионные проверки. В performance-реестре и просмотренном `CODE_ARCHITECTURE_REVIEW_20261025.md` конкретный security-аудит этих ошибок не найден; здесь они не объявляются ни исправленными, ни повторно доказанными. PGO не решает эти замечания.
- Проверить holdout-проекты и длительный lifecycle сервера с повторными PUBLISH/LOAD/UNLOAD, памятью, отказами и восстановлением. Успешные малые validation fixtures текущей серии не являются доказательством ускорения на других проектах или длительной устойчивости.
- Подготовить принятую схему canary/rollback с проверенным OFF image и конфигурацией, не затрагивающую данные проекта. Исполнить deployment отдельным явно разрешённым действием после reliability gates.

Итог текущей стадии: сборочная оптимизация ACCEPT, exact-image integrity gate готов; рабочий deployment остаётся отдельным незавершённым этапом.
