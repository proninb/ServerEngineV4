# PRODUCTION-PGO-01 — выигрыш production server подтверждён

2026-10-27. **ACCEPT для обученного Release + IPO + PGO USE профиля на UnitProXL.** Алгоритмы Parser/G/Runtime не изменены. PGO по умолчанию остаётся OFF: USE требует соответствующей обученной PGD. Сервер в эксплуатацию автоматически не развёрнут.

## Результат настоящего server executable

| Метрика | Release + IPO, PGO OFF | Release + IPO, PGO USE |
|---|---:|---:|
| TCP PUBLISH, среднее | 1262,370 мс | 1131,122 мс |
| TCP PUBLISH, медиана отдельных вызовов | 1261,443 мс | 1124,745 мс |
| Process CPU за запрос, среднее | 4169,792 CPU мс | 3877,083 CPU мс |
| Peak working set, среднее | 2573,024 MB | 2572,419 MB |

Средняя парная экономия по блокам: **131,248 мс / 10,397%**. 95% percentile bootstrap CI среднего блочного выигрыша: **124,291–137,518 мс**. Выигрыш 15/15 блоков. Медиана блочной экономии: 134,635 мс (это другая статистика, чем разность медиан отдельных вызовов).

CPU-экономия: 292,708 CPU мс, CI 174,479–403,646; CPU суммируется по потокам и не является wall-time. Изменение peak WS около 0,605 MB на фоне 2,57 GB практически мало. Это наблюдаемый process peak после запроса, не отдельное измерение peak transient G. Достоверного улучшения startup не обнаружено: CI изменения включает ноль.

Цель <1000 мс пока не достигнута: PGO mean 1131,122 мс. Старые 1308,80 мс относятся к внутреннему native benchmark другой серии. Нельзя вычитать из них 131,248 мс TCP-экономии и выдавать результат за новый измеренный native baseline. Отдельный свежий внутренний benchmark OFF/USE здесь не запускался: текущая серия непосредственно проверяет production executable.

## Метод и границы

- Evidence root: `build/production-pgo-current-02/`; исходная база commit `87abe00f7b6abd611c045b20d999649b9152659a`. Исходники и fixture зафиксированы `freeze.json` и `source.patch`. Snapshot включает существующие локальные CMake PGO настройки и неактивные resolver audit guards; ON/OFF используют один snapshot.
- Настоящий `ServerEngineV4.exe`, MSVC 19.51 x64 Release /O2 + IPO (/GL /LTCG), production V2 с Fast Reader, Ready Source tokens, Prehash, String Path Reuse и String Batch. CMake SERVER_ENGINE_* настройки одинаковы, кроме production PGO OFF/USE и пути к PGD. Benchmark main или подмена parser backend не используются.
- Только unsandboxed execution. По одному отдельному серверному процессу на вызов, одинаковый неизменяемый fixture и пути, уникальное имя SHM и отдельные session-каталоги. Startup/LOGIN/GET_STATE выполняются до измерения PUBLISH. Граница — отправка TCP PUBLISH до получения соответствующего ответа по localhost. Это не полное время запуска/завершения процесса.
- Исключён один warmup на arm. Затем 15 блоков ABBA/BAAB поочерёдно: 60 измеряемых PUBLISH, 30 на arm. Никакие measured samples не удалялись; незавершённые старые серии не объединялись с текущей.
- Основная оценка — среднее от 15 парных разностей средних внутри блока. Bootstrap resamples целые блоки, 20000 повторов, фиксированный seed 101, percentile 95% CI. Это защищает парность и не трактует 60 коррелированных вызовов как 60 независимых пар.
- Без ETW и подробных фазовых таймеров во время основной серии. CPU GetProcessTimes и memory query выполняются вне request timer; artifact/live SHM hashes — после него. Полный input manifest проверен до и после серии. На каждом из 62 вызовов, включая warmup, проверены compiled/runtime hashes, LOADED state и native SHM payload; проверки не входят в request time. Они могут влиять на cache/thermal conditions следующего вызова одинаковым протоколом для обоих arm, поэтому вывод относится к этому прогретому протоколу.

## Корректность и provenance

Повторная validation восстановленных executable завершена 27 октября перед A/B: UnitProXL, simple, mixed, lifecycle; PUBLISH/LOAD, повторный native SHM audit, RUN/FREEZE, values/links, legacy REBUILD/no-change BUILD и ожидаемое отклонение semantic-changing BUILD с сохранённым LOAD. Оба arm полностью совпали в пределах этих проверок. Это не доказательство корректности на всех возможных проектах.

UnitProXL native payload: **2146276728 байт**, исключены только 72 байта изменяемого runtime system header.

| Файл / payload | SHA-256 |
|---|---|
| OFF executable | `354205b58ec799f280e8d48ad88d3f8fa43325a6a49e10ce1b5c631a2572af66` |
| PGO USE executable | `98e8c489ff1b6d03f4058151ef6833d3750289e3483656e49d76c2c5a6617bcf` |
| compiled.bin | `16a81fc1b08522803a6b71db97187fbae5babf144703fe30b306a845b6d0921e` |
| runtime.bin | `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6` |
| Native SHM после system header | `3864b27bbaa8e54e30eb4a2e2c806ecf77b78434b10e49b70a26659fe74f0743` |

PGD обучена заново на этом же production executable, три завершённых прохода UnitProXL и малых fixture с lifecycle-командами. Два прохода находятся в train.log, ещё один в train-retry.log; прерванный startup третьей исходной попытки сохранён отдельно и не объявляется завершённым проходом. Три PGC объединены без overflow losses; USE не использует старую benchmark PGD. Результат workload-specific: UnitProXL участвовал в training, поэтому выигрыш на незнакомых проектах этой серией не установлен.

Основные файлы:

- `ab-01/samples.json`: все 60 measured calls; warmup в run.log.
- `ab-01/summary.json`: статистика, binary и artifact hashes.
- `ab-01/binaries/ordinary/ServerEngineV4.exe` и `ab-01/binaries/pgo/ServerEngineV4.exe`: точные измеренные images; хеши подтверждены после серии.
- `validation.json`, `validate-recheck.log`: полная повторная validation.
- `completion-status.json`: итог, PGD/source/evidence hashes и отсутствие автоматического deployment.
- `experiments/production-pgo-01/run_current.py`: воспроизводимый runner; `train_retry.py` — отдельный training retry.

## Решение по использованию

PGO даёт существенный устойчивый выигрыш полного PUBLISH настоящего сервера на данном UnitProXL: принять **обученную сборочную конфигурацию** как подтверждённый способ ускорения без изменения алгоритмов. Режим USE доступен через `SERVER_ENGINE_PRODUCTION_PGO=USE` с абсолютным `SERVER_ENGINE_PRODUCTION_PGO_DATABASE` соответствующей обученной PGD. Генерирующая сборка использует GENERATE; обычная остаётся OFF.

Не включать USE универсальным default: без PGD такая сборка не воспроизводима, а при изменении исходников/toolchain профиль нужно заново обучить и проверить. Артефактный deployment, автоматическое включение PGO у пользователя и переключение рабочего сервера не выполнялись. Цель <1 секунды остаётся открытой.

В ходе подготовки Norton неоднократно блокировал старые и новые images. После пользовательского разрешения записи current-02 исчезли из индекса; обе restored images имели ожидаемые хеши. В последней полной validation и A/B блокировок/ошибок не было. Настройки защиты, исключения и восстановление антивирусом агент не изменял. Предыдущая попытка продолжения была не исполнена из-за лимита автоматического approval review; это не антивирусное событие и не measured sample.

## Release-процесс

[Контролируемый PGO release](PRODUCTION_PGO_RELEASE.md) фиксирует OFF default, правила нового обучения, проверку exact-image provenance и отдельные reliability/deployment gates. Read-only проверка: `scripts/verify_production_pgo.py`.

## История незавершённых попыток

Ниже сохранён прежний отчёт как исторический контекст. Его статусы pending/blocking описывают состояние соответствующей попытки; итоговое решение выше относится только к полной серии 27 октября.

# PRODUCTION-PGO-01 — incomplete measurement

Status on 2026-10-25: validation completed for the tested scope; performance decision pending. Production algorithms and default PGO setting remain unchanged (OFF).

## Completed

- Frozen production sources at base commit `2ed70d627e3650a00f8d697745b053986a8f40f2`, with the production-only CMake PGO configuration recorded in `build/production-pgo-01/source.patch`.
- MSVC Release + IPO OFF / GENERATE / fresh training / merge / USE builds of the real server executable.
- Training on UnitProXL and synthetic simple, mixed and lifecycle fixtures; no reuse of the historical benchmark PGD.
- Separate PUBLISH/LOAD, runtime RUN/FREEZE, legacy REBUILD and no-change BUILD checks. Semantic-changing BUILD is rejected by both versions under the existing deferred-support contract.
- Cold artifact and native SHM parity for the tested projects, including 2,146,276,728 UnitProXL payload bytes. The mutable 72-byte runtime system header is excluded. Evidence: `build/production-pgo-01/validation.json`.

## Interrupted A/B

`ab-04` retained 16 of 60 planned timed calls. Norton reported `IDP.Generic` through Behavioral Protection for the ordinary executable and process. The server connection was reset; executable copies disappeared. The supplied Norton screenshots document quarantine action. This does not establish whether the detection is a false positive.

No partial series is accepted as a performance result. Do not combine incomplete runs or report a PGO speedup from them. Earlier incomplete attempts are retained as diagnostic evidence.

## Binary provenance recheck

The compiler output still matches the PGO image recorded before `ab-04`:

- Ordinary: `071269d3194def0ab557dffe7717f740de7d097b1f65bcca48deadf2a14a372d`
- PGO compiler output: `25c65898f8ce52dc789977d3fe0a5613add439679a857e77074393eaa54a3bf4`
- PGD recorded before timing: `609ef8a5fecb73b11d33bb7414cfc88eec470097d28a462e9aa58dcada58ba17`

The separately staged `bin/pgo/ServerEngineV4.exe` was observed with a different hash (`90270ee442609ad78946f26719cf6435159e325eb719346f427f61fc8e8d5123`). Its provenance has not been established; it must not be substituted for the frozen measured image. Both `ab-04/binaries` executable paths were missing at recheck. No server was running.

A subsequent read-only PE section comparison found identical `.text`, `.data`, `.pdata`, `.rsrc` and `.reloc` sections; `.rdata` differs. Evidence: `build/production-pgo-01/pgo-image-comparison.json`. This narrows the difference but does not establish complete behavioral equivalence or replace exact binary provenance.

## Next gate

Resolve the Norton detection before another execution series; no protection settings or exclusions have been changed. Re-freeze exact binaries, repeat cold validation on those binaries, then run a new complete uninstrumented paired series. The measured boundary is external TCP PUBLISH request latency; startup, hashes and cold validation stay outside it. Record CPU and memory separately. No final PGO adoption decision yet.

Inheritance-arena instrumentation and C10 compatibility work remain separate pending tasks. No arena cap or C10 production integration has been introduced by this experiment.


## Current V2 retry — 2026-10-26

Status: correctness PASS; performance still pending. New isolated source/fixture freeze at commit `87abe00f7b6abd611c045b20d999649b9152659a` includes Fast Reader. Existing local CMake PGO settings and inactive resolver-audit guards are captured in `build/production-pgo-current-01/source.patch`; both arms use that same snapshot. No production default changed.

Evidence root: `build/production-pgo-current-01/`.

- Fresh MSVC 19.51 x64 Release /O2 + IPO OFF and GENERATE builds, then three real-server training rounds on UnitProXL, simple, mixed and lifecycle projects. Training also exercises LOAD, RUN/FREEZE and legacy BUILD/REBUILD. A first training attempt failed before server startup because the fresh directory lacked pgort140.dll; the successful retry uses the matching MSVC runtime DLL. Instrumented timings are not performance evidence.
- Three fresh PGC files merged with zero overflow losses; USE build reports 100% of its reported instructions optimized using profile data. No old benchmark PGD reused.
- `validation.json` confirms exact compiled.bin/runtime.bin and native SHM payload equality between ordinary and USE, repeated LOAD, RUN/FREEZE, value/link checks, legacy REBUILD/no-change BUILD and the expected deferred semantic-changing BUILD rejection. Mutable 72-byte runtime system header remains excluded from payload comparison.
- A new runner `experiments/production-pgo-01/run_current.py` adds full input-manifest checks before/after the series and live SHM comparison after every PUBLISH, outside the request timer. Planned series: 15 alternating ABBA/BAAB blocks (60 measured calls) plus warmups, unsandboxed, real server TCP request latency.
- **Zero measured calls completed.** At staging, ordinary executable was absent. Read-only Norton quarantine-index check confirms entry `00000035`, `IDP.Generic`, original folder `build/production-pgo-current-01/bin/ordinary`. Evidence: `norton-status.json`. Both correctness-validation arms had completed before this removal. `ab-01` is a staging failure, not a timing series.
- No restoration, exclusion, protection change, renaming workaround or rebuild to evade the detection was attempted. The earlier successful launch of an old PGO executable did not establish that a new ordinary build would be permitted.

`completion-status.json` records source/PGD/USE/validation hashes and the incomplete state. PGO remains OFF by default. No speedup, regression or adoption conclusion follows from this attempt; resolve the new detection before a complete paired series, and revalidate any changed binaries.


## User-confirmed retry in current-02 — 2026-10-26

After the user reported the Norton-resolution step DONE, work resumed in the requested separate directory `build/production-pgo-current-02`. `readiness.json` proves source and fixture manifests match current-01. Fresh OFF/GENERATE/USE builds used the same snapshot and new production PGD.

Two training rounds completed in `train.log`; an original third startup reset at LOGIN without a matching quarantine entry at that check. The remaining process from that failed startup was stopped, and one separate `train-retry.log` round completed. Three completed PGC files merged without overflow losses. No training timings are used as performance results.

Ordinary validation passed UnitProXL/simple/mixed/lifecycle PUBLISH/LOAD/RUN/FREEZE/native checks, legacy REBUILD/no-change BUILD and expected semantic-changing BUILD rejection. PGO USE validation then reset at LOGIN and its staged executable disappeared. Read-only quarantine inspection confirms **Norton entry 00000036, IDP.Generic, original folder current-02/bin/pgo**. `norton-status.json` records the evidence. No server process remained at the final inspection.

No timed series began (0 measured calls). No cross-arm parity conclusion is claimed for current-02; the prior current-01 parity remains its own completed result. No restore, exclusions or protection settings were automated. Further execution stopped after this new confirmed detection. `completion-status.json` records available evidence hashes; `ordinary-hash.json` preserves the initial OFF executable hash. Performance remains pending and production PGO remains OFF. Resolving an earlier ordinary-image detection did not establish clearance of this new PGO USE image.
