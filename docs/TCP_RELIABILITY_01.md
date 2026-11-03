# TCP-RELIABILITY-01

2026-10-27. Исправления login/connection lifecycle и TCP exception boundaries выполнены; **PASS в пределах проверок**. PUBLISH Parser, Graph, persistence, Runtime и бинарные форматы не менялись. Новый production executable собран Release + IPO, PGO OFF. Deployment и commit/push в этой задаче не выполнялись.

## Модель доступа — явное решение пользователя

Пользователь подтвердил: **LOGIN без аутентификации, также в доверенной LAN**. LOGIN name — заявленный клиентом идентификатор, не пароль/токен и не доказательство личности. Сохранены прежние адреса bind, включая LAN/wildcard. Loopback-only ограничение, credential validation и новый RBAC не вводились.

До успешного LOGIN серверные команды отклоняются. После LOGIN клиент получает возможности существующей Server Policy/license mode; в full mode имя само по себе не ограничивает PUBLISH/SHUTDOWN. Внешнее ограничение доступа к TCP listener — часть принятого trusted-deployment контракта. Это **не устранение отсутствующей аутентификации для недоверенной сети** и не заявление о безопасном публичном endpoint. Process-level Authentication/Entra configuration сама по себе не превращает name-only TCP LOGIN в token validation.

Так трактуется P0 из предоставленного `SERVER_ENGINE_V4_CODE_AUDIT_20261026.md`: модель явно согласована, проверки pre-login gate добавлены; криптографическая аутентификация намеренно вне выбранной задачи. Исходный аудит выполнен на 87abe00; его замечание про отсутствие production PGO инфраструктуры уже устарело, а TCP failure paths проверены отдельно здесь.

## Исправления P1

- `communication::login_connection()` сначала регистрирует соединение, затем публикует login-state. Ошибка registry allocation, duplicate ID, invalid/repeated LOGIN или allocation имени закрывает соединение; при неуспешном commit регистрация отменяется. Ветка «commit succeeded, register failed, logged_in=true» больше не существует.
- `commit_login()` больше не выпускает исключение из string assignment через noexcept. `logged_in()` учитывает closing. Ошибка enqueue ответа LOGIN также закрывает соединение.
- Request envelope хранит указатель на живую connection, защищённую существующим lifetime token origin. Сервер повторно проверяет login/closing **после dequeue**: иначе команда, поставленная TCP worker в очередь сразу после неудачного повторного LOGIN, могла бы выполниться по устаревшему ingress gate. Нельзя обойти закрытие pipelined SHUTDOWN.
- Accept держит RAII permit от gate acquisition до успешного добавления peer. Ошибки allocation connection, initial receive buffer и peers vector освобождают permit и сокет.
- Receive/send/output исключения изолируют соответствующий peer. Worker entrypoint noexcept с catch-all закрывает peers и listener на терминальном исключении или poll failure; процесс сервера не завершается через std::terminate. `tcp_endpoint::failed()` сообщает терминальный отказ; для восстановления endpoint требуется stop/start или restart. Автоматический retry-loop при глобальном OOM не введён.
- Close cleanup не прерывает освобождение socket/gate из-за ошибки отправки close notification. Закрытые registry entries и subscriptions дополнительно очищаются при следующем register/publish/route либо stop. Если close message не выделилась и новых операций нет, registry lifetime может сохраняться до следующей такой операции/stop; немедленное освобождение всех метаданных в этом случае не обещается.

## Проверки

MSVC 19.51, x64 Release `/O2 /GL /LTCG`, `/EHsc`, PGO OFF, Windows. Тесты исполнялись unsandboxed. `/MP8` — локальный параметр параллельной компиляции, не production algorithm flag. Fault hooks `CW_COMMUNICATION_FAULT_INJECTION` определены PRIVATE только для `ServerEngineV4CommunicationRegressionTests`; отсутствие hooks в production vcxproj проверено.

Шесть CTest suites PASS: server_policy, client_session, communication_protocol, communication_connection_core, communication_control, communication_regressions.

Regression target выполняет существующие проверки poll snapshot, overload, shutdown lifetime/SIGPIPE и новые сценарии:

- отказ registration и allocation login-name, duplicate ID, пустое имя;
- реальный TCP failed LOGIN → попытка SHUTDOWN → закрытие без серверной команды и новое успешное соединение;
- запрет команды до LOGIN;
- bad_alloc в accept connection / buffer / vector insertion; synthetic accept error;
- bad_alloc receive buffer, synthetic receive error, output allocation;
- close-post allocation failure и восстановление gate при лимите 1;
- здоровый peer остаётся доступен при receive failure другого;
- poll descriptor allocation failure: terminal failed state, закрытие, возврат gate, stop/start;
- очистка subscriptions при отсутствующей close notification.

Инъекции локальны перед конкретными allocation sites/ветками native error, это не глобальная проверка всех возможных allocator/syscall отказов ОС. Тест не гарантирует сохранение доступности при непрерывном OOM всей системы.

## Проверка настоящего сервера

`experiments/tcp-reliability-01/integration.py`, финальный результат `build/tcp-reliability-01/integration-03/result.json`:

1. До LOGIN отклонены GET_STATE, RUN, PUBLISH и SHUTDOWN; процесс остаётся доступен. Произвольное допустимое trusted-LAN имя проходит LOGIN согласно согласованному контракту.
2. На UnitProXL и новом малом fixture побайтово совпали compiled.bin/runtime.bin и native SHM после PUBLISH/LOAD у нового OFF executable и неизменённого PGO reference. Для UnitProXL проверено 2146276728 payload bytes, без 72-byte mutable system header.
3. В одном процессе candidate прошли 50 циклов PUBLISH → RUN/FREEZE → UNLOAD → LOAD → UNLOAD с проверкой значения, образов и native payload. Это bounded correctness/lifecycle smoke, **не длительный soak** и не доказательство устойчивой памяти/скорости на произвольном holdout.
4. Duplicate LOGIN и SHUTDOWN отправлены одним TCP packet. Peer закрывается, успешного ответа на эти команды нет; replacement client делает LOGIN/GET_STATE в том же живом сервере.

В промежуточной integration-02 harness ошибочно считал любой оставшийся frame признаком незакрытого peer. Frame мог быть ранее поставленной в очередь асинхронной state action от UNLOAD. В integration-03 harness читает frames до закрытия и различает request_id, не принимая state actions за успешные LOGIN/SHUTDOWN. Первичная неудачная проверка сохранена, не выдана за PASS; после исправления теста production code дополнительно не менялся.

Переносимый результат, hashes и CTest log: `experiments/tcp-reliability-01/results/20261027/`. Источник аудита идентифицирован SHA-256 в completion-status. License/lease, executables и исходный UnitProXL не включены в этот набор.

## PGO reference и release gate

Измеренный PGO executable остался неизменённым:

`build/production-pgo-current-02/ab-01/binaries/pgo/ServerEngineV4.exe`

SHA-256 `98e8c489ff1b6d03f4058151ef6833d3750289e3483656e49d76c2c5a6617bcf`.

Для проверки oracle он запускался на контролируемом loopback, но не пересобирался и не обучался. Проверка исходного frozen bundle проходит. Старые 1131,12 мс относятся к нему, не к новой TCP-версии. Новая версия требует свежего PGO training, correctness/performance release gate. Длительный soak и holdout performance остаются следующими отдельными этапами; улучшение производительности этим исправлением не заявляется.
