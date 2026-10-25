# HEADER-V2-CURSOR-VIEWS-01 — 23 октября 2026

**Итог: cursor candidate отклонён и удалён из production.** Остаются production
Header V2 + ранее принятый lazy state, а также новые semantic contract tests.

## Сохранённые проверки семантики

Из SC02/SC03 перенесены 22 representative oracle fixtures непосредственно в
production frontend regression suite, без зависимости от экспериментального
Parser, canonicalizer или evaluator:

- Immediate/forward references, pending destinations и superseded pending writes.
- Stale generations, повторное связывание и обратный порядок появления targets.
- Reference-to-reference и повторные definitions в разных Header roots.
- Forward nested targets, multidimensional arrays и merge disjoint nested defaults.
- Ошибки stale/incompatible targets, unresolved references, конфликт defaults.
- Приоритет pending destination/nested/reference diagnostics и первого root.

Positive fixtures проверяют конечные construction values/bindings, WHO и полный
inventory ожидаемых nested defaults. В восьми negative fixtures зафиксированы
точные kind/detail/file/offset/length первого diagnostic. Проверки прошли на V2
**до изменения cursor**, затем на reference и candidate.

Код: `tests/header_v2_semantic_contract_cases.hpp`, вызов из
`tests/frontend_regression_tests.cpp`. Это 22 целевых кейса, не перенос всех 1080
запусков старого экспериментального worker/target matrix.

## Выбор hotspot

Повторно обработаны реальные восемь ETL из SOURCE-CPU-SAMPLING-02. Учтены только
SampledProfile rows со стеком `header_parser_v2::`, без context-switch events:
9588 recognizable Header samples, по 1176–1224 на блок.
`semantic_cursor_v2::bind_current`: 2602 exclusive samples, **27,14%**.

Это исторический production V2 **до lazy state**. Ограничения IPO/ICF, потерянных
frames и незагруженных OS symbols сохраняются. Не достигнут первоначальный
формальный порог 10 000 samples; ranking использован как exploratory выбор
кандидата, не как точная декомпозиция актуального V2. Raw summary:
`build/header-v2-cpu-audit-01/header-summary.json`.

## Ровно один эксперимент

Candidate сохранял две readonly spans при cursor.start(): identifier occurrences
и local-symbol → string_id resolution. Start уже проверяет file lineage и
completeness; candidate исключал повторный same-file guard и часть pointer
indirections на каждом identifier. Проверки occurrence/local-ID bounds и valid ID
сохранялись. Нового буфера, строк, хеширования, WHO database или G не было.

Lifetime проверен по preprocessor: resolution_record имеет stable unique owner,
symbol vectors подготовлены до replay и не меняются до его завершения.
Дополнительные tests проверяют undersized mapping, wrong file, incomplete IDs,
лишний identifier occurrence и restart cursor с другой mapping. Они проходят и
на прежнем cursor и оставлены как его контракт.

## A/B полного PUBLISH

Один MSVC Release+IPO executable, UnitProXL Headers+Sources, lazy state в обоих
вариантах. Две warmup pairs + двадцать measured pairs; порядок чередуется.
Полные compiled/runtime SHA-256 проверены после всех 44 PUBLISH и совпали с
golden предыдущей серии из того же full fixture.

| Медиана, мс | Reference | Readonly views |
|---|---:|---:|
| Полный PUBLISH | 1318,030 | 1316,010 |
| Header semantic | 208,723 | 210,024 |

Медиана парного выигрыша PUBLISH: **2,270 мс**, быстрее **12/20**.
Средний парный выигрыш: **3,526 мс**, paired bootstrap 95% CI среднего
**[−1,543; +9,411] мс**. Header: медиана парного выигрыша **0,107 мс**,
средний **−0,504 мс**, CI **[−1,809; +0,795] мс**, быстрее **10/20**.
Outliers не исключались. Обе CI пересекают ноль; принятие оптимизации не обосновано.

В CSV/summary generic runner использует имена eager/lazy: здесь eager = reference
cursor, lazy = views. Manifest явно содержит `CW_HEADER_CURSOR_REFERENCE=1/0`.
Это не повторное сравнение lazy containers: они включены в обеих конфигурациях.
Не сравнивать 1316 мс с прежними сериями как доказательство ускорения.

## Состояние после эксперимента

Core cursor и lexical symbol headers восстановлены. Candidate-only CMake option,
getenv switch и benchmark branches удалены. Source snapshot и cursor patch:
`build/header-v2-cursor-views-01/candidate-source/`; frozen executable, logs, CSV,
goldens и CI: `build/header-v2-cursor-views-01/results-20261023/`.
Active tree не предназначено для пересборки этого отклонённого candidate.

Regression suites reference/views прошли 2/2. Финальный production suite после
отката проверяет V2+lazy, все 22 semantic cases и mapping guards без cursor A/B code.
Исходный `bind_current` hotspot не означает, что выбранные guards/indirections
занимали существенную часть его CPU. Этот эксперимент такого выигрыша не показал.
