# SOURCE-RESOLUTION-01 — 2026-10-24

RESOLVED-G-02 закрыт с решением **NO ADOPTION**. Этот эксперимент исследует Source V2; production Graph и Source не изменены. Реализация находится в `experiments/source-resolution-01` и генерирует отдельные translation units из текущих исходников. Core собран заново, Release + IPO, без RG01/RG02 Graph hooks.

## Диагностическое измерение

UnitProXL, 24 027 типов, 137 584 объекта, 367 144 связи. Нормальное исполнение записано один раз; вход привязан к исходному Header G, строкам, identities и содержимому Source. Все oracle-режимы загружают одинаковую полную запись (483 545 045 байт). Это дополнительный footprint эксперимента, отсутствующий в production.

15 свежих пар на каждую категорию; порядок контроль/эксперимент и порядок категорий чередуется. Контроль выполняет обычный Source с загруженной, но не используемой записью. Внутри вызовов нет часов. Перед измерением отдельный verification run сверяет фактические аргументы; в timed runs эта проверка отключена. Golden SHA вычисляется снаружи после каждого успешного PUBLISH.

| Диагностическая замена | Control, мс | Oracle, мс | Медиана парного выигрыша, мс | Bootstrap 95% CI | Быстрее |
|---|---:|---:|---:|---|---|
| Semantic tokens + повторный identifier interning | 465,32 | 391,74 | 74,26 | [66,72; 94,23] | 15/15 |
| Read-only identity lookup | 468,28 | 445,02 | 22,37 | [7,50; 31,21] | 12/15 |
| Endpoint formation | 471,44 | 394,41 | 77,70 | [69,18; 85,21] | 15/15 |
| Provenance API | 468,30 | 448,50 | 20,42 | [16,28; 46,00] | 14/15 |

Столбцы control/oracle — независимые медианы соответствующей серии; парный выигрыш вычислен отдельно. Интервалы получены resampling медианы 15 пар (10 000 итераций); временной дрейф окружения они полностью не исключают.

### Границы интерпретации

Это **эффекты замены работы**, а не exclusive CPU breakdown production. Их нельзя складывать. Чтение готовых semantic tokens исключает `semantic_input.next` и повторную привязку строк, но сохраняет Parser dispatch/expect. Endpoint oracle также исключает часть синтаксиса и identity lookup, но сохраняет реальные token reads, интернирование и Source dependencies; добавляет восстановление endpoint/path. Identity oracle сохраняет создание новых WHO. Provenance oracle восстанавливает исходную последовательность SourceMap APIs вне окна, до финализации.

Окно `stage_ms` включает создание и уничтожение Parser, parse roots, обычные Graph mutations и extern validation. Оно исключает загрузку/сохранение записи, fingerprint, provenance restoration и внешнюю проверку SHA. Подготовка Source input также находится снаружи. Поэтому обычные `total_ms` oracle-режимов не используются как результат оптимизации PUBLISH.

Записаны 5 892 598 чтений semantic tokens, 1 285 844 `find_visible`, 137 584 `resolve`, 946 656 endpoints, 3 995 766 provenance calls. На данном корпусе **все 137 584 resolve создают новые WHO**; oracle не исключает их. Оснований вводить локальную WHO-базу это измерение не даёт.

## Один кандидат

Выбран специализированный ready-token Source cursor: объединить общий маршрут `next → next_source` для уже подготовленного потока, сохранив реальные `physical_identifier`/string interning, порядок создания IDs, проверки directives, bounds, EOF и diagnostics. Нового кэша, WHO-index, Source IR или Graph API нет. Header, compact-input и detailed-telemetry paths остаются исходными.

Кандидат генерируется в отдельный `semantic_input.cpp`, включается только `CW_SOURCE_READY_CURSOR=1` в экспериментальном executable. Контроль/кандидат сравниваются в одном executable, `CW_SOURCE_RESOLUTION_MODE=0`, без trace. Дополнительно выполняется калибровка неактивного executable относительно production baseline.

### Калибровка наблюдения

Диагностическая сборка даже в mode 0 оказалась дороже production: 7 пар, медиана overhead Source **48,37 мс**, bootstrap 95% CI [38,97; 60,89]; полного PUBLISH **61,53 мс**, CI [27,69; 83,44]. Кроме самих ветвлений прокси, возможны изменения оптимизации/inlining и layout; причины по отдельности не измерены. Поэтому таблица ablations не является точной декомпозицией production CPU.

Предварительный candidate A/B этой сборки дал 14,87 мс полного PUBLISH и 9,68 мс Source, но **не используется для решения о внедрении**. Для окончательной проверки выделена clean-сборка `SOURCE_RESOLUTION_DIAGNOSTICS=OFF`: исходный Parser без прокси, та же одна специализация cursor. Никакой trace, fingerprint, replay/restoration или per-call instrumentation в clean Parser нет.

### Чистый full-PUBLISH A/B

Clean executable использует исходный Parser, полностью заново собранный core, Release + IPO. Все `SERVER_ENGINE_*:BOOL` настройки совпали с `build/enum-v4`. Калибровка неактивной clean-сборки против production baseline: 7 пар; разница полного PUBLISH −7,79 мс, CI [−19,48; 23,18], Source −7,86 мс, CI [−10,71; 4,41] (положительное значение означало бы ускорение). Разница не доказана; это не доказательство точного нулевого overhead.

| Метрика | Результат |
|---|---|
| Full PUBLISH control, медиана | 1362,17 мс |
| Full PUBLISH candidate, медиана | 1344,72 мс |
| Парный выигрыш full PUBLISH | **16,09 мс**, CI **[−27,87; 25,14]**, быстрее 11/15 |
| Парный выигрыш Source | **11,56 мс**, CI **[0,70; 28,03]**, быстрее 12/15 |

Именно парные разницы используются для решения. Разность независимых медиан не заменяет парную статистику. Полное ускорение PUBLISH **не доказано**, несмотря на положительный эффект в Source. Итог кандидата: **NO ADOPTION**. Production Graph, Parser и PUBLISH оставлены без изменений. Перебора дополнительных кандидатов в этой задаче нет.

Все 46 запусков clean-серии (2 warmup, 14 calibration arms, 30 candidate arms) прошли проверку обоих golden digests. Оба frontend suites clean-сборки также прошли. Сырые результаты: `build/source-resolution-01/clean-candidate-measurements/{runs.json,pairs.json,summary.json,executables.json}`; stdout/stderr сохранены по каждому запуску.

## Вывод

Эксперимент локализует перспективную область до работы с токенами/формированием endpoints, но не доказывает их exclusive production CPU cost. Локальная WHO-база не обоснована. Простая специализация готового cursor не превратила диагностические 74 мс в доказанный полный PUBLISH выигрыш. Следующее исследование должно уточнять стоимость повторного identifier interning и endpoint syntax/traversal; это рекомендация, без нового кандидата или production-изменений в текущей задаче.

## Correctness и артефакты

Все три CTest suites проходят: исходный frontend, frontend с кандидатом, oracle fixtures. Fixtures проверяют namespaces, extern, cross-file updates, repeated assignments, arrays, strings, link rebinding и отклонение устаревшей записи. Отрицательные семантические сценарии проверяются frontend suite с обычным и специализированным cursor. Oracle replacement предназначен для успешного исполнения проверенного корпуса.

Все 127 запусков первой серии (warmup, capture, 5 verification, 120 paired arms) сохранили:

* compiled.bin: `a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d`
* runtime.bin: `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`

Сырые данные: `build/source-resolution-01/measurements/{runs.json,pairs.csv,summary.json}`, stdout/stderr каждого запуска, manifest исходного Parser и SHA профилируемого executable. Кандидат: `build/source-resolution-01/candidate-measurements`. Команды воспроизведения — в README эксперимента.
