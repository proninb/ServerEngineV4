# Реестр доказательств PUBLISH UnitProXL

Срез исходного реестра 2026-10-26; дополнен результатом PRODUCTION-PGO-01 от 2026-10-27, production V2, commit `87abe00f7b6abd611c045b20d999649b9152659a`. Цель — менее 1 секунды. Этот реестр объединяет решения и границы доказательств; исходный реестр составлен без новых экспериментов. Дополнение PGO ниже основано на завершённой отдельной серии; production algorithms и default PGO не изменены.

## Точка отсчёта и правила сравнения

Текущий путь: Fast Reader → Header V2 → Source V2 (Ready Tokens, Prehash, String Path Reuse, String Batch) → G в RAM → packed encoder → ABI/Runtime plans → SHM. IPO ON, PGO по умолчанию OFF. C10 OFF; Direct mmap v27 откатан; PIPELINE-01 не внедрён. Обычный PUBLISH не делает synchronous durable flush, SHA-256/USN-проверки; правила BUILD/REBUILD отдельные. На измеренной машине остаются 24 Object workers.

Это состояние подтверждается текущими CMake defaults и профильными отчётами. Локальные незакоммиченные PGO-настройки и resolver audit не являются новым принятым алгоритмом.

Все времена ниже — внутренний native benchmark PUBLISH до resident teardown, если явно не оговорено иначе. Они не измеряют TCP-запрос к production server или полный запуск/завершение процесса.

[Контроль A/B/C](PUBLISH_UNITPROXL_ABC_01.md): unsandboxed, неизменяемый исходный UnitProXL, 15 блоков со всеми перестановками A/B/C. Исторический V2 A: 1354,65 мс; современный metadata-reader B: 1354,13 мс; Fast Reader C: 1313,58 мс. Парная экономия B→C 36,70 мс, 95% CI 34,93–62,29; 13/15 побед. Для A→B интервал включает ноль: крупная регрессия современного V2 не доказана.

Тот же исторический executable давал 1814,18 мс в sandbox и 1269,89 мс вне sandbox. В отдельной серии из семи пар парная разница 550,41 мс (CI 519,06–666,96). Поэтому прежние 1928→1659 мс и 13,9% нельзя переносить на обычный запуск. Точная причина эффекта ОС не установлена; `IsTokenRestricted=false` в обоих режимах.

Хеш исторического A: `841c7848aaf4631fef25c8bf6eb28de7cac072f9b01dd66dcf0cae7b0a1adde5` — см. точный манифест в отчёте A/B/C; сохранённый executable и сегодняшние исходники не гарантируют воспроизводимую историческую сборку.

## Единый бюджет текущего профиля

Источник: [SOURCE-V2-CRITICAL-01](SOURCE_V2_CRITICAL_01.md), `build/source-v2-critical-01/capture/measurements.csv`, только 32 control-запуска без sampling, средние арифметические. Это одна серия; значения не собраны из разных медиан.

| Метрика wall-time | Среднее, мс | Как читать |
|---|---:|---|
| Полный PUBLISH | 1308,796 | Основной контроль серии |
| Lexical | 86,119 | Время фазы |
| Semantic | 754,196 | Содержит Header и Source, не складывать с ними |
| Header | 222,360 | Внутри Semantic |
| Source | 436,832 | Внутри Semantic |
| Persistence | 204,476 | Метаданные, encoding и физические планы; не SSD flush |
| Resident | 206,897 | SHM materialization |
| Transient release | 21,389 | Смотрите границы таймера в runner |

Подфазы и накладные расходы не образуют полную аддитивную декомпозицию. Не нужно вычитать сумму этой таблицы из total и объявлять остаток самостоятельным hot path. Sampling увеличивал среднее total до 1384,990 мс: sampled timings не служат основной оценкой скорости.

| Source CPU-категория | CPU мс / sampled PUBLISH | Ограничение |
|---|---:|---|
| read_endpoint + children | 124,25 | Inclusive |
| Graph commit + helpers | 105,03 | Inclusive |
| Token handling | 93,72 | Inclusive |
| Strings / identity | 77,75 | Inclusive |
| intern_resolved_endpoint_path вне read_endpoint | 69,09 | Отдельная canonicalization |
| intern_prehashed | 49,81 inclusive; 33,91 own | Уже входит в другие категории |

CPU-категории пересекаются, их нельзя складывать или считать экономией wall-time. [Endpoint analysis](SOURCE_V2_ENDPOINT_COST_01.md): внутри read_endpoint token/lexical 50,38 CPU мс, typed traversal 35,44, own/hidden-inline 29,53. Крупное безопасно устранимое дублирование не найдено.

## Принятые изменения и уже работающие оптимизации

| Изменение | Production / доказательство | Повторно обещать как новый выигрыш? |
|---|---|---|
| Fast Reader | ON; текущий native A/B/C: парная экономия 36,70 мс | Нет |
| String Batch | ON; [15 пар](SOURCE_STRING_BATCH_AB_01.md): Scalar 1370,42 → Batch 1331,39; парная экономия 35,09 мс, CI 21,27–39,96, 13/15 | Нет |
| String Path Reuse | ON; [исторический отчёт](SOURCE_V2_STRING_PATH_REUSE_01.md), текущие defaults и endpoint audit | Нет |
| Ready Source tokens / parallel prehash | ON; [Prehash](PUBLISH_SOURCE_PREHASH_20.md), [Ready Tokens](PUBLISH_SOURCE_LEXER_TOKENS_30.md) | Нет; старые эффекты не складывать с новым baseline |
| Member-transition reuse / prepared lookup | Уже присутствует; [Endpoint analysis](SOURCE_V2_ENDPOINT_COST_01.md), [Type prepare](SOURCE_TYPE_PREPARE_01.md) | Новый cache требует доказанного нового miss/work budget |
| No SHA-256 в fresh PUBLISH | Принято; [NO-SHA256](PUBLISH_NO_SHA256_01.md) | Нет |
| No durable flush/USN в PUBLISH | Принято; [POLICY-04](PUBLISH_POLICY_04.md) | Нет; не переносить на BUILD/REBUILD |
| Header V2 evolution, cursor / canonical merge / macro fast path | История принятых шагов — в каталоге ниже; их эффекты относятся к своим checkpoint | Не считать дополнительным резервом текущей версии |

String Batch сокращает Graph API calls 705237→196469, но low-level upserts остаются 705237. Проверены 81 функциональная операция, артефакты, SHM, LOAD и инъекции отказов в пределах тестов. Выигрыш уже включён в baseline. Отдельные эффекты Fast Reader и Batch нельзя просто сложить как независимый новый прогноз.

## Отвергнутые гипотезы и границы решений

| Гипотеза / область | Решение и измерение | Условие возвращения |
|---|---|---|
| Второй Link Target lookup | [NO ADOPTION](SOURCE_V2_CRITICAL_01.md): парная экономия 11,51 мс, CI −8,18…24,44; 11/15 | Новое доказательство полного native выигрыша |
| Endpoint → intern → member duplication | [NO IMPLEMENTATION](SOURCE_V2_ENDPOINT_COST_01.md): разные ключи lookup; повторный intern не обнаружен | Конкретная повторная операция после IPO, а не две строки исходника |
| Whole endpoint caches / path caches | [Cache-37](PUBLISH_ENDPOINT_CACHE_37.md), [Reuse-13](PUBLISH_ENDPOINT_REUSE_13.md): проигрывают | Новая структура нагрузки и бюджет, старую схему не повторять |
| Type prepare / target context / string scratch / intern slot | [Type](SOURCE_TYPE_PREPARE_01.md), [Context](SOURCE_TARGET_CONTEXT_01.md), [Scratch](SOURCE_STRING_SCRATCH_01.md), [Slot](STRING_INTERN_SLOT_01.md): не внедрены; локальная экономия не доказала full PUBLISH выигрыш | Новый достаточный full PUBLISH эффект |
| Resolved G path intern | [Cost-02](RESOLVED_G_02_PATH_INTERN_COST.md): нет внедрения; full gain CI включает ноль, prototype/oracle не бесплатен | Реализуемая схема без oracle и подтверждённая общая экономия |
| C10 вместо V2 | [Unsandboxed recheck](PLS10_C10_UNSANDBOXED_RECHECK_01.md): V2 1353,49; C10 1426,55; парный штраф 83,06 мс, CI 38,86–103,71; проигрыш 15/15 | Новый кандидат с новым доказательством; улучшение C7 не равно победе над V2 |
| Direct G mmap v27 | [A/B](DIRECT_G_MMAP_AB_01.md): откатан; production RAM G + packed encoder | Новая обоснованная гипотеза полного выигрыша; синтетика не заменяет UnitProXL |
| PIPELINE-01 | [Отчёт](PUBLISH_PIPELINE_01.md): не внедрён; отрицательная серия в sandbox | Нельзя считать старую серию unsandboxed доказательством |
| Меньше Object workers | [Lanes](RUNTIME_OBJECT_LANES_01.md): 24→4/8/16 ухудшает; парные штрафы 52,06/32,01/29,01 мс | Не повторять те же caps; среда старого отчёта отдельно от нового baseline |
| Отдельный SHM/Object pretouch | [First touch](RUNTIME_FIRST_TOUCH_01.md): отклонён, total 1302,875→1353,645 мс | Новое доказательство устранения, а не переноса first-touch |
| API range validation reuse | [Ranges](RUNTIME_API_RANGES_01.md): не promoted | Доказанный full PUBLISH выигрыш с allocation/lookup overhead |
| Bulk materialization | [Bulk](BULK_MATERIALIZATION_01.md): NO ADOPTION | Не повторять тот же prototype |

[RUNTIME-SHM-FAULTS-01](RUNTIME_SHM_FAULTS_01.md): около 77,7% inclusive apply_api_one samples — kernel leaf; около 99,6% этих kernel samples проходят KiPageFault. Это CPU sampling старой серии, не счётчик faults или disk latency. Нет fault-events, доказывающих hard/soft распределение или длительность ожиданий. Его рекомендация попробовать worker caps **уже выполнена** в OBJECT-LANES; она больше не следующий шаг.

## Неполные доказательства и отдельные контракты

- [Production PGO](PRODUCTION_PGO_01.md): **завершён 27 октября, ACCEPT для обученного USE-профиля**. Реальный server TCP PUBLISH: OFF mean 1262,370 → USE 1131,122 мс; средняя блочная экономия 131,248 мс / 10,397%, CI 124,291–137,518, 15/15 побед. 60 measured calls, unsandboxed, артефакты и live SHM совпали. Не подменяет старый native baseline 1308,80 мс. PGO default OFF сохранён, deployment не выполнялся; эффект на не участвовавших в training проектах не измерен.
- [Final-cell contract](SOURCE_FINAL_CELL_CONTRACT_01.md) и архитектурные handoff-документы — требования/проекты, не доказательство реализации или ускорения production.
- Исторические профили используют разные корпуса: ранний UnitProXL 11814 типов / 134474 объектов / 85731 links отличается от текущего 24027 / 137584 / 367144. Название проекта не означает идентичность fixture.
- Если отчёт не фиксирует режим запуска, он остаётся «режим не установлен»; численное сходство не доказывает unsandboxed запуск. Отрицательный A/B внутри одной серии остаётся своим результатом, но его абсолютные миллисекунды не новый baseline.
- У исторического A в A/B/C нет прямого live-SHM digest. Паритет через общий LOAD/audit не следует описывать как прямое измерение A live SHM.
- Побайтовое равенство допустимо для одинаковых форматов. Для иных G representations нужна явно определённая семантическая проверка; object-storage hash не равен полному G/SHM oracle.
- Peak memory и page faults не имеют единого свежего межсерийного baseline. Отсутствие улучшения времени не доказывает отсутствие изменения памяти.

## Критерий следующей работы

Нового алгоритмического эксперимента сейчас нет. От текущих ~1,3 секунды нужно убрать примерно 300 мс, около 23% total. Если пытаться получить всё только из Source ~437 мс, потребовалось бы убрать около 70% его wall-time; такой бюджет не доказан.

Возвращаться к реализации следует после обнаружения конкретной устранимой работы с существенным бюджетом, проверки, что гипотеза отсутствует в реестре, и определения semantic/error/SourceMap/LOAD/SHM oracle. Для принятия — неизменяемый fixture, Release /O2 /GL /LTCG, PGO OFF если не предмет опыта, unsandboxed, раздельные артефакты, прогрев и минимум 15 чередующихся native пар без подробных таймеров. CPU sampling и фазовые таймеры — отдельная диагностическая серия. Решение определяется полным PUBLISH и доверительным интервалом, а не одним hot function или лучшим запуском.

## Каталог сохранённых отчётов

Ниже 143 отчёта, найденных по именам performance-серий на дату среза. Это каталог источников, а не 143 принятия в production. Для каждого сохранена начальная строка решения либо исходный lead; старое «next step» не переопределяет решения выше. Отсутствие записи вне перечисленных семейств не доказывает, что эксперимент никогда не проводился. Машиночитаемый снимок хранится рядом: `PUBLISH_EVIDENCE_REGISTER.json`.

| Отчёт | Решение/контекст из источника (цитата, сокращённая) |
|---|---|
| [BULK_MATERIALIZATION_01](BULK_MATERIALIZATION_01.md) | **NO ADOPTION.** Проверенный bulk prototype сохраняет G/Runtime artifacts и diagnostics, но замедляет полный PUBLISH. Пользовательский порог ≥10% ускорения с доверительным интервалом, исключающим ноль, не выполнен. Produ |
| [CANONICALIZER_01](CANONICALIZER_01.md) | identity_space. Capture выключен по умолчанию и включается только для отдельного |
| [CONSTRUCTION_LIFETIME_01](CONSTRUCTION_LIFETIME_01.md) | # CONSTRUCTION-LIFETIME-01 — memory benefit, not promoted for speed |
| [CPU_SAMPLING_CURRENT_V2_20261023](CPU_SAMPLING_CURRENT_V2_20261023.md) | share agrees with the accepted rehash reduction; cross-series shares are not a |
| [DIRECT_G_MMAP_AB_01](DIRECT_G_MMAP_AB_01.md) | Full UnitProXL: **24,027 types, 137,584 objects, 367,144 links**. Windows x64, MSVC 19.51, Release /O2 + IPO /GL + /LTCG, no PGO. Date: 2026-10-26. **Result:** current append/reallocate v27 is slower in total PUBLISH and roughly doubles compiled.bin. It lowers |
| [EVALUATOR_ISOLATION_01](EVALUATOR_ISOLATION_01.md) | и изменений production PUBLISH. Код: experiments/evaluator-isolation-01/. |
| [EVALUATOR_PARALLEL_01](EVALUATOR_PARALLEL_01.md) | двух независимых медиан; отрицательное значение означает ускорение. |
| [EVALUATOR_SCALING_01](EVALUATOR_SCALING_01.md) | Дата: 2026-10-22. Основа — frozen SC03. Эксперимент только диагностический: алгоритмы, структуры semantic model, production PUBLISH и исходники SC03 не менялись. Код: experiments/evaluator-scaling-01/. |
| [FRONTEND_PHYSICAL_11](FRONTEND_PHYSICAL_11.md) | ## Scope and invariant Only the **physical** include-discovery scanner changes. The lexer already records lexical_directive_anchor{word_offset, source_base} for every |
| [FRONTEND_PHYSICAL_12](FRONTEND_PHYSICAL_12.md) | ## Scope prepared_include_builder_v2 owns a **single-use**, single-threaded memo (local candidate path, quoted/angled form) -> file_id. Paths are native |
| [GRAPH_IDENTITY_APPEND_ARCHITECTURE](GRAPH_IDENTITY_APPEND_ARCHITECTURE.md) | Status: **Target architecture decision** Repository baseline: proninb/ServerEngineV4 Decision baseline: c5e73eca19065aec8018b875a6ef9ef76e1c73e3 Date: 2026-10-01 |
| [GRAPH_IDENTITY_APPEND_HANDOFF](GRAPH_IDENTITY_APPEND_HANDOFF.md) | Repository: proninb/ServerEngineV4 Branch: work/ic-catalog Verified decision baseline: c5e73eca19065aec8018b875a6ef9ef76e1c73e3 |
| [HEADER_CHUNK_01](HEADER_CHUNK_01.md) | Первый этап завершён: реализованы и измерены boundary discovery и планирование участков. Chunk Parser не добавлен. Production PUBLISH и SB04 не изменены. ## Основной результат |
| [HEADER_CHUNK_02](HEADER_CHUNK_02.md) | После 8 workers extraction почти не ускоряется, хотя длинный root уже не |
| [HEADER_DIRECT_G_01](HEADER_DIRECT_G_01.md) | Этот baseline не равен production Header-only PUBLISH 409,13 мс из предыдущей |
| [HEADER_GRAPH_INDEX_01](HEADER_GRAPH_INDEX_01.md) | **Принято:** growth factor 4× у существующего transient member-name index в G. |
| [HEADER_MODEL_V2_01](HEADER_MODEL_V2_01.md) | ## Contract This is an **experimental first slice**, NOT a full structured Header Model. It does not replace Header V2 record-member/type validation. Its purpose is |
| [HEADER_MODEL_V2_02B1](HEADER_MODEL_V2_02B1.md) | Status: opt-in A/B research slice; **not** a completed general Header Model. Depends on local HEADER-MODEL-V2-01 and 02A installers, PUBLISH Header V2. ## Actual implementation |
| [HEADER_MODEL_V2_02B2](HEADER_MODEL_V2_02B2.md) | **Status:** opt-in experiment; do not adopt or commit until Release/MSVC tests and UnitProXL byte parity pass. This patch is applied ON TOP OF 02B1; it replaces that experiment's prepass/replay implementation (not a clean-room standalone parser). |
| [HEADER_ONLY_PUBLISH_01](HEADER_ONLY_PUBLISH_01.md) | production header_ms или 409,134 мс полного Header-only PUBLISH. |
| [HEADER_SEMANTIC_COVERAGE_01](HEADER_SEMANTIC_COVERAGE_01.md) | **Статус: полное Header V2 coverage ещё не достигнуто.** Реализован первый изолированный slice: managed constructors с прямыми scalar operands. Reference targets, nested paths и другие категории этим результатом не закрыты. Production PUBLISH остаётся на прежн |
| [HEADER_SEMANTIC_COVERAGE_02](HEADER_SEMANTIC_COVERAGE_02.md) | Production PUBLISH не изменён. TypeRequest, canonicalization, WHO и алгоритм |
| [HEADER_SEMANTIC_COVERAGE_03](HEADER_SEMANTIC_COVERAGE_03.md) | Дата: 2026-10-22. Изолированный эксперимент, production PUBLISH не изменён. Исходники: experiments/header-semantic-coverage-03/. ## Результат корректности |
| [HEADER_SEMANTIC_SPACE](HEADER_SEMANTIC_SPACE.md) | HEADER-SEMANTIC-SPACE-01 makes the Header parser's outer control state explicit. The input boundary is the existing preprocessed semantic_token stream: |
| [HEADER_V2_CURSOR_OPT_17](HEADER_V2_CURSOR_OPT_17.md) | Baseline: accepted PUBLISH-PHYSICAL-01 commit 7478593. |
| [HEADER_V2_CURSOR_VIEWS_01](HEADER_V2_CURSOR_VIEWS_01.md) | **Итог: cursor candidate отклонён и удалён из production.** Остаются production |
| [HEADER_V2_OPT_08](HEADER_V2_OPT_08.md) | Status: accepted as the baseline of **experimental Header Parser V2**. Full ServerEngineV4 PUBLISH, persisted BUILD-lineage and Source/Assign remain separate validation gates. |
| [HEADER_V2_SEMANTIC_14](HEADER_V2_SEMANTIC_14.md) | The accepted mean improvement is modest. It does not justify broader preprocessor changes or a new persistent cache. |
| [HEADER_V2_SEMANTIC_ONCE_01](HEADER_V2_SEMANTIC_ONCE_01.md) | PUBLISH only. Graph and persistence schemas, BUILD, REBUILD and LOAD are unchanged. The pre-existing experimental leaf cache has been replaced on the PUBLISH path by an ordered, construction-local semantic event cache. |
| [HEADER_V2_STATE_LAZY_01](HEADER_V2_STATE_LAZY_01.md) | **Принято для V2.** Узкая оптимизация production Header без новой semantic модели, |
| [INHERITANCE_ARENA_BOUND_01](INHERITANCE_ARENA_BOUND_01.md) | 2026-10-25. Production cache algorithm is unchanged. No arena cap has been selected or enabled. |
| [LEX_SYMBOL_GROWTH_04](LEX_SYMBOL_GROWTH_04.md) | Checkpoint: work/parser-split-v1 at 365b5d9e15546e00cdea06bb72354ab5eb10af5c. ## Cause |
| [PARALLEL_LOCAL_SOURCE_02](PARALLEL_LOCAL_SOURCE_02.md) | Дата: 2026-10-24. **Первый correctness slice реализован.** Production Source Parser, Graph API/ABI, compiled format, SourceMap и Runtime не изменены. Новый путь — standalone C++ executable, а не включённый production mod |
| [PARALLEL_LOCAL_SOURCE_03](PARALLEL_LOCAL_SOURCE_03.md) | ## Scope and method Isolated continuation of PLS02 on the same UnitProXL corpus. No workers, materialization, production Graph or Source V2 changes. Source syntax is still |
| [PARALLEL_LOCAL_SOURCE_04](PARALLEL_LOCAL_SOURCE_04.md) | Production PUBLISH is unchanged. |
| [PARALLEL_LOCAL_SOURCE_05](PARALLEL_LOCAL_SOURCE_05.md) | materialization, production PUBLISH or consumer format is changed. |
| [PARALLEL_LOCAL_SOURCE_06](PARALLEL_LOCAL_SOURCE_06.md) | Production PUBLISH, Graph/ABI, workers and materialization remain unchanged. |
| [PARALLEL_LOCAL_SOURCE_07_A](PARALLEL_LOCAL_SOURCE_07_A.md) | ## Contract PLS07-A introduces an owning semantic result without compacting its format. PLS03 flat canonical interning, PLS04 local-file release and PLS06 dense write |
| [PARALLEL_LOCAL_SOURCE_ARCH_01](PARALLEL_LOCAL_SOURCE_ARCH_01.md) | Первый implementation slice выполнен отдельно в PARALLEL_LOCAL_SOURCE_02.md: реальная extraction, global semantic linking и symbolic alias-aware reduction без повторного разбора Source. Production и materialization по- |
| [PARSER_V2](PARSER_V2.md) | - production routing is unchanged. |
| [PARSER_V2_CURSOR_AB_DIRECTIVE_10](PARSER_V2_CURSOR_AB_DIRECTIVE_10.md) | The normal project executable, tests and production continue to use DECODE-09. |
| [PARSER_V2_CURSOR_DECODE_09](PARSER_V2_CURSOR_DECODE_09.md) | ## Goal and scope Fast decoding of *ordinary nonfirst lexical words*, while preserving compact lexical_token format, extended delta/length layout, canonical identifier and |
| [PARSER_V2_CURSOR_PERF_07](PARSER_V2_CURSOR_PERF_07.md) | ## Scope Benchmark-only cursor/preprocessor instrumentation. Requires PERF-05 and DETAIL-06. The build target ServerEngineV4ParserV2ProjectBenchmark already defines |
| [PARSER_V2_MACRO_FASTPATH_08](PARSER_V2_MACRO_FASTPATH_08.md) | ## Scope Accelerate zero-definition identifier expansion in preprocessor::expand(). This is a direct C++ path used by Semantic Preprocessor V2. It does not change |
| [PARSER_V2_PERF_03](PARSER_V2_PERF_03.md) | mode. The production call sites use the unchanged 5-argument form. |
| [PARSER_V2_SEMANTIC_DETAIL_06](PARSER_V2_SEMANTIC_DETAIL_06.md) | No G, Runtime, SHM, source grammar, CMake, or production ABI modifications. |
| [PARSER_V2_SEMANTIC_PERF_05](PARSER_V2_SEMANTIC_PERF_05.md) | Baseline: cc24108de8bbee53552252afe15d39c1608e321f on work/parser-split-v1. ## Scope |
| [PLS07_B1_FINAL_WRITER_CONTRACT](PLS07_B1_FINAL_WRITER_CONTRACT.md) | archived endpoint ordinals. Do not silently reuse production sparse invalidation |
| [PLS08_A_GRAPH_WRITER_FEASIBILITY](PLS08_A_GRAPH_WRITER_FEASIBILITY.md) | production semantic coverage, resident publication or PLS08-C full PUBLISH |
| [PLS08_B_INTEGRATION_PARITY](PLS08_B_INTEGRATION_PARITY.md) | MSVC Release + IPO. Production sources and BUILD/REBUILD/LOAD are unchanged. |
| [PLS08_C_FULL_PUBLISH_AB](PLS08_C_FULL_PUBLISH_AB.md) | 2026-10-24. Result: **correct on the checked compatible domain; NO ADOPTION for |
| [PLS09_A_ALIAS_COST_BREAKDOWN](PLS09_A_ALIAS_COST_BREAKDOWN.md) | the accepted SHA-256 cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2. |
| [PLS09_B1_INDEXED_REFERENCE_STATE](PLS09_B1_INDEXED_REFERENCE_STATE.md) | 2026-10-25. **Useful experimental candidate; no production integration.** |
| [PLS10_A_FINAL_BINDING_REDUCTION](PLS10_A_FINAL_BINDING_REDUCTION.md) | undeclared object being accepted for later lookup. |
| [PLS10_B_SPARSE_INDIRECT_WRITES](PLS10_B_SPARSE_INDIRECT_WRITES.md) | Date: 2026-10-25. Decision: **NO ADOPTION** of early direct-winner |
| [PLS10_C10_TYPED_ENDPOINT_SCRATCH](PLS10_C10_TYPED_ENDPOINT_SCRATCH.md) | 2026-10-25. **Successful experimental candidate; production V2 unchanged.** |
| [PLS10_C10_UNSANDBOXED_RECHECK_01](PLS10_C10_UNSANDBOXED_RECHECK_01.md) | 2026-10-26. **C10 is slower than historical production V2 outside sandbox.** / Native PUBLISH median / ms / /---/---:/ |
| [PLS10_C11_STRING_ELEMENT_APPLY_ATTRIBUTION](PLS10_C11_STRING_ELEMENT_APPLY_ATTRIBUTION.md) | C10 scratch reuse remains the experimental baseline; C8 remains NO ADOPTION. |
| [PLS10_C12_EXISTING_BATCH_API_FEASIBILITY](PLS10_C12_EXISTING_BATCH_API_FEASIBILITY.md) | 2026-10-25. **NO ADOPTION for using the existing batch API in PLS10.** |
| [PLS10_C13_STRING_PATH_REUSE](PLS10_C13_STRING_PATH_REUSE.md) | 2026-10-25. **NO ADOPTION.** Correct bounded one-entry memo, no demonstrated |
| [PLS10_C14_REMAINING_ORDERED_COMMIT_ATTRIBUTION](PLS10_C14_REMAINING_ORDERED_COMMIT_ATTRIBUTION.md) | overlap and C8 already rejected a simple lookup-count reduction. |
| [PLS10_C15_ENDPOINT_STRUCTURAL_AUDIT](PLS10_C15_ENDPOINT_STRUCTURAL_AUDIT.md) | 2026-10-25. **Audit complete; tested candidate NO ADOPTION.** |
| [PLS10_C1_DIRECT_COMMIT_FEASIBILITY](PLS10_C1_DIRECT_COMMIT_FEASIBILITY.md) | Production V2 and A2 are unchanged. PLS10-C integration is **not completed**. |
| [PLS10_C2_SLOT_BASED_DIRECT_COMMIT](PLS10_C2_SLOT_BASED_DIRECT_COMMIT.md) | production V2 dispatch are unchanged. No data members were added to Graph, |
| [PLS10_C3_ORDERED_SOURCE_COMMIT](PLS10_C3_ORDERED_SOURCE_COMMIT.md) | Production V2 dispatch remains unchanged. Full UnitProXL acceptance/performance |
| [PLS10_C4_C5_PROJECT_VISIBILITY](PLS10_C4_C5_PROJECT_VISIBILITY.md) | the accepted V2 oracle. The parsed model vector capacities total **182,960,089 |
| [PLS10_C6_ORDERED_COMMIT_COST_BREAKDOWN](PLS10_C6_ORDERED_COMMIT_COST_BREAKDOWN.md) | baseline and production V2 remains selected. No optimization is adopted here. |
| [PLS10_C7_ALREADY_TYPED_ENDPOINT_VIEW](PLS10_C7_ALREADY_TYPED_ENDPOINT_VIEW.md) | Production V2 remains faster and is not switched. No additional fast-path |
| [PLS10_C8_SINGLE_BINDING_LOOKUP](PLS10_C8_SINGLE_BINDING_LOOKUP.md) | 2026-10-25. Decision: **NO ADOPTION**. C7 fast remains the experimental baseline; |
| [PLS10_C9_V2_C7_COST_ATTRIBUTION](PLS10_C9_V2_C7_COST_ATTRIBUTION.md) | Source semantics, C7 queue and algorithms are unchanged. C8 remains NO ADOPTION. |
| [PRODUCTION_PGO_01](PRODUCTION_PGO_01.md) | **Дополнение 27 октября: ACCEPT обученного USE; 131,248 мс / 10,397%, 15/15 блоков.** Исторический excerpt: Status on 2026-10-25: validation completed for the tested scope; performance decision pending. Production algorithms and default PGO setting remain unchanged (OFF). |
| [PUBLISH_ARCHITECTURE_REVIEW_22](PUBLISH_ARCHITECTURE_REVIEW_22.md) | принятым Source prehash, Release + IPO без PGO. Это одна серия; вложенные таймеры |
| [PUBLISH_CRITICAL_PATH_02](PUBLISH_CRITICAL_PATH_02.md) | insert_link_target_index 1309, assignment 1210, advance 1133. Rejected slot caching |
| [PUBLISH_DIFFERENTIAL_TRACE_01](PUBLISH_DIFFERENTIAL_TRACE_01.md) | Новый evaluator не интегрирован в production. Исходники production не менялись |
| [PUBLISH_DIRECT_GRAPH_MMAP_01](PUBLISH_DIRECT_GRAPH_MMAP_01.md) | **Historical implementation note — rolled back.** After DIRECT-G-MMAP-AB-01 showed regressions, the v27 code, allocator, feature flag and counters were removed from the active source tree. PUBLISH again constructs RAM G and encodes the packed artifact. The imp |
| [PUBLISH_END_TO_END_PROFILE_01](PUBLISH_END_TO_END_PROFILE_01.md) | а не A/B нового алгоритма и не доказательство ускорения относительно старой серии. |
| [PUBLISH_ENDPOINT_CACHE_37](PUBLISH_ENDPOINT_CACHE_37.md) | # Whole Source endpoint cache: rejected (37) |
| [PUBLISH_ENDPOINT_REUSE_13](PUBLISH_ENDPOINT_REUSE_13.md) | # Full Source endpoint reuse: rejected experiments 13–15 |
| [PUBLISH_FAST_ACQUIRE_02](PUBLISH_FAST_ACQUIRE_02.md) | 2026-10-26. Production fresh PUBLISH now selects byte-only physical file |
| [PUBLISH_FIRST_TOUCH_06](PUBLISH_FIRST_TOUCH_06.md) | the rejected static-path cache. No concurrent builds ran during timing. |
| [PUBLISH_FLUSH_OVERLAP_03](PUBLISH_FLUSH_OVERLAP_03.md) | The later (PUBLISH policy)(PUBLISH_POLICY_04.md) removes explicit flush from PUBLISH. The overlap described here remains active for durable REBUILD; the measurements below document the earlier PUBLISH implementation. |
| [PUBLISH_GRAPH_UPSERT_11](PUBLISH_GRAPH_UPSERT_11.md) | Two variants were implemented and measured against the accepted Source dispatch |
| [PUBLISH_HEADER_LEXER_TOKENS_31](PUBLISH_HEADER_LEXER_TOKENS_31.md) | Реализован SERVER_ENGINE_HEADER_LEXER_TOKENS_V2, по умолчанию OFF. |
| [PUBLISH_HEADER_ONCE_18](PUBLISH_HEADER_ONCE_18.md) | Before every eligible cached Header replay, the preflight copied the current #pragma once state for every file in the project. Most replayed subtrees visit only a small portion of those files. This added a full file-table scan and hash lookup per file to every |
| [PUBLISH_HEADER_SHARED_MODEL_17](PUBLISH_HEADER_SHARED_MODEL_17.md) | The feature is therefore not promoted to the normal PUBLISH path. |
| [PUBLISH_HEADER_SOURCE_OVERLAP_29](PUBLISH_HEADER_SOURCE_OVERLAP_29.md) | Добавлен SERVER_ENGINE_HEADER_SOURCE_OVERLAP_V2 (по умолчанию OFF). |
| [PUBLISH_HEADER_TOKEN_ONLY_32](PUBLISH_HEADER_TOKEN_ONLY_32.md) | Оба остаются OFF по умолчанию: измерения не показали ускорения. Принятый |
| [PUBLISH_IDENTITY_PIPELINE_23](PUBLISH_IDENTITY_PIPELINE_23.md) | на 64 разрешённые операции; по умолчанию commit немедленный. Workers отсутствуют. |
| [PUBLISH_INTEGRATION_GAP_01](PUBLISH_INTEGRATION_GAP_01.md) | поток интернирования и не создаёт production SourceMap/reuse tape. Нулевые constructor |
| [PUBLISH_LEXER_KEYWORDS_19](PUBLISH_LEXER_KEYWORDS_19.md) | # Rejected: skip keyword hashes for long identifiers |
| [PUBLISH_MSVC_PGO_12](PUBLISH_MSVC_PGO_12.md) | An isolated PUBLISH benchmark build with MSVC profile-guided optimization improves complete PUBLISH on the full UnitProXL fixture. Clang was not installed in the checked locations; the user chose to evaluate MSVC only for now. |
| [PUBLISH_NO_SHA256_01](PUBLISH_NO_SHA256_01.md) | # PUBLISH-NO-SHA256-01 — accepted |
| [PUBLISH_OWNED_SNAPSHOTS_02](PUBLISH_OWNED_SNAPSHOTS_02.md) | ## Change PUBLISH enables owned content before configuration composition. Initial source preparation and Header V2 include preparation explicitly call adopt_acquire(): |
| [PUBLISH_PARALLEL_CANONICALIZATION_39](PUBLISH_PARALLEL_CANONICALIZATION_39.md) | Status: architectural proposal and implementation constraints, not an enabled pipeline or a measured speedup. Supersedes the unfinished per-function sampling work. The tested publish-timeline mode from 38 is retained. No Local G is proposed. |
| [PUBLISH_PERFORMANCE](PUBLISH_PERFORMANCE.md) | fixtures may link ordinary integer members and are rejected by the current |
| [PUBLISH_PHYSICAL_01](PUBLISH_PHYSICAL_01.md) | Baseline: 1acc3a6acbed6c89fcb6e3b37aa90adee47e3510. ## Contract |
| [PUBLISH_PIPELINE_01](PUBLISH_PIPELINE_01.md) | # PUBLISH-PIPELINE-01 — experimental, no adoption |
| [PUBLISH_POLICY_04](PUBLISH_POLICY_04.md) | This aligns the implementation with the PROJECT.md invariant that BUILD/REBUILD own durability. PUBLISH creates no reusable BUILD lineage, so it needs neither USN/change tokens nor an explicit durable artifact flush. |
| [PUBLISH_PROFILE](PUBLISH_PROFILE.md) | scan was introduced. Preprocessing directives remain rejected in Source. |
| [PUBLISH_RUNTIME_CHUNKS_21](PUBLISH_RUNTIME_CHUNKS_21.md) | # Rejected: cyclic runtime object blocks |
| [PUBLISH_RUNTIME_IMAGE_REUSE_01](PUBLISH_RUNTIME_IMAGE_REUSE_01.md) | Full construction already prepares a validated shm_runtime_v2 physical plan before laying out runtime.bin. Previously, runtime layout sizing traversed every Graph link and initialization endpoint again to count dereference programs. Full construction now count |
| [PUBLISH_SOURCE_BOUND_RESOLVER_27](PUBLISH_SOURCE_BOUND_RESOLVER_27.md) | шаги последовательно. Окно commit 64 остаётся опциональным и OFF по умолчанию. |
| [PUBLISH_SOURCE_DISPATCH_08](PUBLISH_SOURCE_DISPATCH_08.md) | Source V2 previously resolved an existing object name again as a type and, on a miss, as a namespace before deciding whether the statement was an assignment. The new dispatch peeks at the next token first. ., ( and = unambiguously select assignment syntax and  |
| [PUBLISH_SOURCE_LAYOUTS_33](PUBLISH_SOURCE_LAYOUTS_33.md) | Header остаётся на принятом compact пути. Header/Source models, overlap, |
| [PUBLISH_SOURCE_LEXER_TOKENS_30](PUBLISH_SOURCE_LEXER_TOKENS_30.md) | Принят SERVER_ENGINE_SOURCE_LEXER_TOKENS_V2=ON по умолчанию в CMake. |
| [PUBLISH_SOURCE_MODEL_07](PUBLISH_SOURCE_MODEL_07.md) | The existing experimental SERVER_ENGINE_SOURCE_MODEL_V2 and SERVER_ENGINE_SOURCE_MODEL_DIRECT_V2 options were evaluated together in a separate Release + IPO build. Both remain OFF by default. This experiment does not establish a repeatable improvement in compl |
| [PUBLISH_SOURCE_OWNED_ENDPOINTS_25](PUBLISH_SOURCE_OWNED_ENDPOINTS_25.md) | проверкой изменения и не устанавливают подтверждённого ускорения. |
| [PUBLISH_SOURCE_PARALLEL_DISPATCH_28](PUBLISH_SOURCE_PARALLEL_DISPATCH_28.md) | # Эксперимент 28: диспетчер bound requests — отклонён |
| [PUBLISH_SOURCE_PREHASH_20](PUBLISH_SOURCE_PREHASH_20.md) | Experiment 20 computes identifier FNV-1a hashes during per-file parallel token decoding. Workers read immutable file bytes and lexical words only. The serial Source cursor still interns names in canonical token order, using the existing string_table::intern_pr |
| [PUBLISH_SOURCE_READY_BATCH_36](PUBLISH_SOURCE_READY_BATCH_36.md) | This experiment combines SERVER_ENGINE_SOURCE_TOKEN_ONLY_V2=ON with SERVER_ENGINE_SOURCE_STRING_BATCH_V2=ON. The baseline is experiment 35: batched strings with ready tokens plus compact words. Both use Source lexer tokens and parallel prehash. Header token-on |
| [PUBLISH_SOURCE_RESOLVER_24](PUBLISH_SOURCE_RESOLVER_24.md) | серии не доказывают ни ускорения, ни стабильной регрессии. После них в resolver |
| [PUBLISH_SOURCE_STRING_BATCH_35](PUBLISH_SOURCE_STRING_BATCH_35.md) | Source V2 now collects resolved character initialization records in a reusable parser-owned buffer. Graph validates the scalar type and record keys once per batch and reserves index/vector capacity for the whole batch before applying records in order. Individu |
| [PUBLISH_SOURCE_STRING_PROFILE_34](PUBLISH_SOURCE_STRING_PROFILE_34.md) | The Source V2 string path did not update the existing string statement and element counters. It now records completed statements and expanded array elements, and exposes source_string_assignment_ms in publish-profile. The timer is disabled in publish-stage and |
| [PUBLISH_SOURCE_WINDOW_26](PUBLISH_SOURCE_WINDOW_26.md) | По умолчанию OFF: ёмкость 1 сохраняет немедленное применение тем же commit API. |
| [PUBLISH_TIMELINE_38](PUBLISH_TIMELINE_38.md) | cause was established either. Do not promote those possibilities to findings. |
| [PUBLISH_TYPE_PLAN_09](PUBLISH_TYPE_PLAN_09.md) | # Type-plan investigation: rejected flat program index |
| [PUBLISH_UNITPROXL_ABC_01](PUBLISH_UNITPROXL_ABC_01.md) | No Parser, reader, production source or production settings were changed during |
| [RESOLVED_G_01](RESOLVED_G_01.md) | Date: 2026-10-24. Diagnostic experiment; production PUBLISH remains unchanged. |
| [RESOLVED_G_02_PATH_INTERN_COST](RESOLVED_G_02_PATH_INTERN_COST.md) | Date: 2026-10-24. **No production adoption.** |
| [RUNTIME_API_RANGES_01](RUNTIME_API_RANGES_01.md) | # RUNTIME-API-RANGES-01 — not promoted |
| [RUNTIME_FIRST_TOUCH_01](RUNTIME_FIRST_TOUCH_01.md) | 2026-10-23. Decision: reject separate Object pretouch. Production remains unchanged: native 24 Object workers on this machine, first-touch during real construction. |
| [RUNTIME_OBJECT_LANES_01](RUNTIME_OBJECT_LANES_01.md) | 2026-10-23. Decision: reject reduced worker limits; retain native capacity (24 on the measured machine). Production defaults are unchanged. |
| [RUNTIME_SHM_FAULTS_01](RUNTIME_SHM_FAULTS_01.md) | Проанализирована существующая ETW-серия build/publish-critical-path-02/capture-20261023-144348/. Новый захват, новый benchmark и изменения production не потребовались. |
| [SEMANTIC_BIND_01](SEMANTIC_BIND_01.md) | В первой разведочной версии стенд независимо разворачивал все includes каждого root. Это породило 20 590 702 запроса, около 17,5 ГБ пиковой памяти и многократную повторную работу. Такой прогон не является измерением рабо |
| [SEMANTIC_BIND_02](SEMANTIC_BIND_02.md) | Аудит реализован и выполнен. Новые scheduler, Parser, формат записей и constructor/reference binding не включались. Production PUBLISH не изменён. **Установлены две конкретные проблемы: root-level параллелизм ограничен одним длинным root; промежуточные данные  |
| [SEMANTIC_BIND_03](SEMANTIC_BIND_03.md) | Production PUBLISH, его defaults, identity_ref ABI и файлы проекта не переключались. Полного Header semantics, construction/defaults, Direct G или нового результата PUBLISH <1 с этот эксперимент не доказывает. |
| [SEMANTIC_BIND_04](SEMANTIC_BIND_04.md) | Эксперимент устраняет зависимость bound type от полного массива Parser requests. Он остаётся изолированным: production PUBLISH, единица параллелизма Header root и семантическое покрытие Header Parser не изменены. |
| [SOURCE_COST_BREAKDOWN_01](SOURCE_COST_BREAKDOWN_01.md) | парный полный PUBLISH. Этот этап не обещает и не демонстрирует ускорение production. |
| [SOURCE_CPU_SAMPLING_02](SOURCE_CPU_SAMPLING_02.md) | Direct G, persisted formats и принятые архитектурные ограничения не менялись. |
| [SOURCE_DEFERRED_OBJECT_01](SOURCE_DEFERRED_OBJECT_01.md) | Дата: 2026-10-24. Область: текущий production Source V2, существующий G, SourceMap, persistence и Runtime V2. Оптимизатор и двухфазный builder не реализованы; production-файлы не изменены. Выполнены чтение кода и три мал |
| [SOURCE_FINAL_CELL_CONTRACT_01](SOURCE_FINAL_CELL_CONTRACT_01.md) | Дата: 2026-10-24. Основание — принятое пользователем правило из приложения к задаче: повторная явная инициализация одной конечной runtime-ячейки, в том числе через разные aliases, выдаёт warning; побеждает последняя запи |
| [SOURCE_LINK_INDEX_01](SOURCE_LINK_INDEX_01.md) | # SOURCE-LINK-INDEX-01 — 4x growth rejected |
| [SOURCE_LOCALITY_01](SOURCE_LOCALITY_01.md) | 2026-10-23. The user's object/page locality hypothesis is supported by measurement. No production optimization is enabled in this audit. A small dedicated target context is justified as the next isolated experiment; a pe |
| [SOURCE_RESOLUTION_01](SOURCE_RESOLUTION_01.md) | RESOLVED-G-02 закрыт с решением **NO ADOPTION**. Этот эксперимент исследует Source V2; production Graph и Source не изменены. Реализация находится в experiments/source-resolution-01 и генерирует отдельные translation u |
| [SOURCE_STRING_BATCH_AB_01](SOURCE_STRING_BATCH_AB_01.md) | 2026-10-26. **Existing production batch path confirmed beneficial.** Current |
| [SOURCE_STRING_SCRATCH_01](SOURCE_STRING_SCRATCH_01.md) | 2026-10-23. Implemented and validated as an isolated experiment. **Not enabled in production:** Source-stage savings are measurable in this series, but full-PUBLISH improvement is inconclusive. |
| [SOURCE_TARGET_CONTEXT_01](SOURCE_TARGET_CONTEXT_01.md) | 2026-10-23. Implemented and semantically validated. **Experiment retained, production default unchanged:** the native candidate improves the Source stage in this series, but neither series establishes a reliable full-PUB |
| [SOURCE_TYPE_PREPARE_01](SOURCE_TYPE_PREPARE_01.md) | 2026-10-23. Isolated resolver audit and type-preparation cache experiment completed. **No production adoption:** same-executable measurements show a small positive signal, but the separate production-candidate comparison |
| [SOURCE_V2_CRITICAL_01](SOURCE_V2_CRITICAL_01.md) | 2026-10-26. **Diagnostic completed. One candidate tested; NO ADOPTION.** |
| [SOURCE_V2_ENDPOINT_COST_01](SOURCE_V2_ENDPOINT_COST_01.md) | / Full endpoint caches 13–15 and 37 / Rejected in their original series. Cache preparation/probing/copying outweighed removed traversal; do not repeat by renaming them. / |
| [SOURCE_V2_STRING_PATH_REUSE_01](SOURCE_V2_STRING_PATH_REUSE_01.md) | ## Accepted diagnostic A/B (prior to production integration) |
| [STATE_LAZY_01](STATE_LAZY_01.md) | projection. Production PUBLISH не ускорен этим изолированным экспериментом. |
| [STRING_INTERN_SLOT_01](STRING_INTERN_SLOT_01.md) | # STRING-INTERN-SLOT-01 — not promoted |
