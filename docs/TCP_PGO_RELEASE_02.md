# TCP-PGO-RELEASE-02 — performance PASS, release gate FAIL

2026-10-27. Новая production PGO-сборка с TCP-RELIABILITY-01 ускоряет TCP PUBLISH и проходит correctness/holdout. **Release-кандидат пока не принят:** выбранный пользователем 10-минутный предварительный soak превысил заранее заданный порог drift private bytes. Это отказ memory gate, не доказанная утечка и не ошибка целостности executable. Deployment не выполнен; PGO default OFF.

## Новая версия и provenance

Локальный evidence bundle: `build/tcp-pgo-release-02/`. Base commit `e238ae186121be153503bf66b58b24ae5ae141eb` плюс зафиксированные TCP reliability изменения. 287 файлов CMake/server_engine/tests, включая новый fault header. Snapshot содержит ранее существовавшие неактивные resolver audit guards. `source/` и `freeze.json` — полный источник истины: один HEAD и tracked diff не содержат каждый untracked header/test. Снимок исходников и training/holdout inputs перепроверены после выполнения серий.

MSVC 19.51 x64 Release `/O2 /GL /LTCG /EHsc`, IPO ON. `/MP8` применён одинаково для обеих сборок как параметр компиляции. Все SERVER_ENGINE_* flags одинаковы кроме production PGO OFF/USE и пути PGD. Production fault-injection не включён. Parser V2, G, Persistence, SHM и алгоритмы Runtime дополнительно не менялись.

| Артефакт | SHA-256 |
|---|---|
| Новый OFF executable | `6cd312cbc030765718ad0e2fb847723d97efc0395455052cc8a8522843cc3429` |
| Новый USE executable | `509373908a081d913876445ab4c09f54dd072d4e23c16e6fc158b42d1720b554` |
| Production PGD после USE, сохранённая в A/B manifest | `b3a6fe175e377900959fab72754ee8ffed109b942da09a6fbf93b3feb545a11a` |
| Старый frozen PGO reference | `98e8c489ff1b6d03f4058151ef6833d3750289e3483656e49d76c2c5a6617bcf` |

Точные измеренные новые images: `ab-01/binaries/{ordinary,pgo}/ServerEngineV4.exe`. Старый reference не пересобирался и не обучался; его хеш остался прежним. Новая PGD обучена заново, три завершённых прохода production PUBLISH/LOAD и lifecycle/BUILD/REBUILD на training fixtures; использованы только свежие PGC. Holdout-файлы созданы после завершения обучения и не включены в него. USE image не импортирует pgort runtime.

## Основная UnitProXL A/B

Настоящий ServerEngineV4.exe, localhost TCP PUBLISH, **unsandboxed**, один новый процесс на вызов. Startup/LOGIN и проверки вне request timer. По одному excluded warmup на arm, затем 15 чередующихся ABBA/BAAB блоков: 60 measured calls, 30 на arm. Ни один measured sample не исключён. На каждом PUBLISH проверены compiled.bin/runtime.bin и native SHM; inputs и binary hashes подтверждены до/после. Нет ETW или подробных фазовых таймеров.

| Метрика | OFF | USE |
|---|---:|---:|
| Средний TCP PUBLISH | 1295,784 мс | 1170,279 мс |
| Медиана отдельных вызовов | 1289,171 мс | 1174,994 мс |
| Средний process CPU за запрос | 4095,833 CPU мс | 4004,167 CPU мс |

Средняя блочная экономия **125,505 мс / 9,686%**, 95% percentile bootstrap CI **111,355–139,811 мс**, 15/15 выигранных блоков. Медиана блочного выигрыша 124,842 мс. Bootstrap resamples 15 целых блоков, 20000 повторов, seed 101.

CPU-экономия 91,667 CPU мс, CI −20,313…201,563: достоверный CPU-выигрыш этой серией не установлен. Startup CI также включает ноль. Малое изменение peak WS около 0,612 MB не заменяет проверку private-memory drift при повторном использовании процесса.

Старые 1131,12 мс — другая версия/серия. Разница между 1170,28 и 1131,12 не является контролируемой оценкой стоимости TCP fixes. Старый reference не участвовал в новой timed A/B. Цель <1000 мс не достигнута; в этой серии до неё около 170,28 мс.

## Correctness и TCP gates — PASS

- Шесть suites на замороженных исходниках: server_policy, client_session, communication_protocol, communication_connection_core, communication_control, communication_regressions. Fault target покрывает registration/login-name/accept/receive/output/close-post/poll, возврат gate и восстановление endpoint.
- OFF/USE parity: UnitProXL и simple/mixed/lifecycle, PUBLISH/LOAD, native payload, RUN/FREEZE, значения/ссылки, legacy REBUILD/no-change BUILD и ожидаемое отклонение semantic-changing BUILD. Native payload UnitProXL 2146276728 байт; исключён mutable 72-byte system header.
- TCP integration нового OFF против нового USE: команды до LOGIN отклонены, name-only LOGIN работает по согласованному trusted-LAN контракту; duplicate LOGIN→pipelined SHUTDOWN закрывает peer без выполнения SHUTDOWN, replacement client подключается. Дополнительно 50 bounded lifecycle cycles.

Успешные fault tests не заменяют глобальный OOM-тест ОС. Terminal worker failure по-прежнему закрывает listener и требует restart/stop-start; статус доступен через C++ `tcp_endpoint::failed()`. Новый удалённый health/diagnostic RPC в этой работе не добавлялся. LOGIN не аутентифицирует клиента и не делает открытый TCP endpoint безопасным для недоверенной сети.

## Отдельный post-training holdout — PASS в проверенном объёме

Три новых synthetic case по 2048 элементов: массовые char[64], inherited-member traversal и reference links. На каждом: exact artifact/native parity, значения, LOAD; 15 ABBA/BAAB блоков, 60 measured PUBLISH. Всего 180 calls. Два постоянных server-процесса, после каждого запроса UNLOAD. Это отдельный warm/persistent протокол, не fresh-process основная серия.

| Case | OFF mean мс | USE mean мс | Средний блочный выигрыш мс | 95% CI мс | Победы |
|---|---:|---:|---:|---:|---:|
| strings | 33.416 | 30.509 | 2.907 | 2.400…3.453 | 15/15 |
| inheritance | 10.019 | 9.359 | 0.660 | 0.274…1.020 | 11/15 |
| links | 13.185 | 12.149 | 1.036 | 0.707…1.420 | 14/15 |

Все интервалы положительны в этих трёх отдельных сериях. Это не multiple-workload гарантия и не доказательство ускорения на произвольных пользовательских проектах. Заранее объявленный holdout gate: отрицательный interval плюс >5% замедления — существенная регрессия; таких случаев нет.

## Предварительный soak — functional checks PASS, memory gate FAIL

Пользователь выбрал **10 минут — предварительный прогон**, вместо 30/60 минут. Один новый USE process: 10 warmup cycles, затем **604,925 сек / 1041 measured cycles**. Всего с прогревом 1051 циклов. Паттерн: UnitProXL каждый десятый цикл, между ними три holdout. PUBLISH → проверка initial images/native/values → RUN/FREEZE → UNLOAD → LOAD/native check → UNLOAD; затем один connection error/reconnect scenario. Payload сравнивается до RUN и после LOAD, не во время изменяемого runtime.

Всего, включая warmup: 263 terminal duplicate LOGIN, 263 partial-frame TCP reset, 263 oversized-frame closure, 262 disconnect, 1051 отклонённый pre-login SHUTDOWN. Все повторные подключения, protocol/semantic assertions и oracle checks прошли. Server штатно завершён после прогона. Handles во всех measured checkpoints: **88**, итоговый median drift **0**.

До начала прогона в soak.py и README заданы coarse limits: last-five minus first-five median quiescent private bytes **≤128 MiB**, handles **≤16**, отдельно для каждого проекта. Checkpoint — UNLOADED после reconnect/GET_STATE. Working set сам по себе не используется как leak gate.

| Case | Первая медиана private MiB | Последняя медиана private MiB | Drift MiB | Memory gate |
|---|---:|---:|---:|---|
| UnitProXL | 18,250 | 257,008 | +238,758 | FAIL |
| strings | 15,648 | 257,008 | +241,359 | FAIL |
| inheritance | 15,152 | 257,008 | +241,855 | FAIL |
| links | 15,156 | 257,008 | +241,852 | FAIL |

Measured private range: 12,199–258,656 MiB. Были возвраты к низкому уровню, поэтому эта серия **не устанавливает** монотонную утечку, её источник, связь с PGO или с TCP fixes. Однако объявленный gate не пройден; его порог не изменялся после просмотра данных. Exit code soak=1 отражает именно финальную memory assertion, а не crash сервера или потерю семантики.

Следующее необходимое измерение — OFF control с тем же 600-second lifecycle/connection pattern и теми же checkpoint/threshold, затем при необходимости сравнение со старой версией. Это отделит поведение общей реализации/allocator retention от влияния PGO или версии TCP. Такой контроль в этой работе пока **не выполнен**; root cause и достаточный production memory budget не определены. Изменения Parser/G/Runtime и «исправления» памяти без attribution не вводились.

## Решение и артефакты

**PGO wall-time benefit нового TCP executable подтверждён. Общий release gate — FAIL; новый executable не продвигается как принятый release-кандидат.** TCP-RELIABILITY-01 сохраняется; старый PGO остаётся frozen reference, не заменой исправленной версии.

`completion-status.json` разделяет семь gates: первые шесть PASS, preliminary soak FAIL. Release verifier отклоняет этот bundle по gate, даже при совпадающих source/binary hashes:

```powershell
python scripts/verify_production_pgo.py build/tcp-pgo-release-02 --source build/tcp-pgo-release-02/source
```

Полные local measurements: ab-01, holdout-results, soak-01/samples.jsonl, validation.json, tcp-regressions.log, tcp-integration/result.json; hashes в completion-status. Переносимые summaries/samples/status: `experiments/tcp-pgo-release-02/results/20261027/`. Корпус, source copy, PGD/PGC, executable и license/lease остаются локальными. Процедура: `experiments/tcp-pgo-release-02/README.md`.

PGO default OFF; старый reference неизменён; нового deployment, commit/push и архитектурных изменений здесь нет.


## Follow-up: SOAK-MEMORY-ATTRIBUTION-01

[OFF control и сопоставление рядов](SOAK_MEMORY_ATTRIBUTION_01.md) выполнены отдельно, без пересборки и без повторения performance A/B. OFF также FAIL по UnitProXL (+240,906 MiB); на общем префиксе 1041 measured cycles последние медианы всех fixture — OFF 260,203 против USE 257,008 MiB. Поэтому высокий quiescent memory level не специфичен для PGO. Дополнительные 30 циклов OFF изменили конечное окно малых fixture, но не снимают OFF UnitProXL FAIL или предыдущий USE FAIL. Release по-прежнему заблокирован.

Уточнение provenance: driver-status записал PGD hash `27ae…` после merge, до USE; финальные A/B binary-manifest и completion-status содержат `b3a6…`. Выше указан именно сохранённый финальный PGD hash. OFF control подтвердил совпадение с этими финальными манифестами, PGD не изменялась.
