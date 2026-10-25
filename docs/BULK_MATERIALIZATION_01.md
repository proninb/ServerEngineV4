# BULK-MATERIALIZATION-01 — 2026-10-24

## Решение

**NO ADOPTION.** Проверенный bulk prototype сохраняет G/Runtime artifacts и diagnostics, но замедляет полный PUBLISH. Пользовательский порог ≥10% ускорения с доверительным интервалом, исключающим ноль, не выполнен. Production Graph API/ABI, Source Parser, persistence и Runtime в этом эксперименте не изменены.

## Полный PUBLISH — свежие парные результаты

Один UnitProXL fixture, Release + IPO, один standalone executable для control и candidate. 15 пар, порядок AB/BA чередуется. Все `SERVER_ENGINE_*:BOOL` CMake options совпали с production baseline. Timed runs без audit counters/index verification, без per-call clocks, replay или предварительного выполнения Source mutations. Golden SHA вычисляется после каждого процесса, вне PUBLISH.

| Метрика | Control | Bulk candidate | Медиана парной регрессии |
|---|---:|---:|---:|
| Полный PUBLISH | 1306,78 мс | 1340,81 мс | **+34,73 мс** |
| Source stage | 419,739 мс | 458,611 мс | **+39,361 мс** |

95% bootstrap interval регрессии полного PUBLISH: **[20,33; 41,75] мс**. Относительная парная регрессия: **2,658%**, CI **[1,554%; 3,165%]**. Только 3/15 полных PUBLISH pairs быстрее; Source медленнее в **15/15**.

Source regression interval: **[35,673; 42,863] мс**. Это сравнение полных Source lifecycles, включая заполнение/освобождение буфера и batch materialization. Отдельные медианы control/candidate не вычитались для получения парной разницы. Интервалы — resampling медианы 15 paired differences, 10 000 итераций; они не устраняют все возможные систематические эффекты окружения.

Принятый порог — медианное парное ускорение полного PUBLISH не менее 10% и нижняя граница CI выигрыша >0. Получено замедление; прототип не внедрён.

## Что именно реализовано

```text
Existing Source V2 resolution/validation
    ├── object registration → existing G immediately
    ├── endpoint path interning → existing G immediately
    └── scalar/reference mutation → bounded pending records
          ├── semantic target checks now
          ├── existing G target lookup + block-local target index
          ├── assign original first-insertion slot
          └── emit original replaced/origin/provenance
                       ↓ block limit / Source finish
             reserve/resize existing final arrays
             append/patch payloads in assigned slots
             construct/extend target indices as a batch
```

Mode 0 вызывает исходные `add_resolved_initialization`, `add_resolved_initializations`, `rebind_resolved_link`. Mode 1 их **не вызывает** для отложенных operations: они заменены direct append/patch и index insertion через ограниченный experiment-only friend access. Phase 1 не строит обычный G mutations результат, чтобы потом повторно построить его во второй фазе. Нет второго WHO/type catalog, Local G, captured token tape или Source AST.

Объекты, Source-derived types, WHO binding, array/member traversal, canonical paths, строки и их deterministic IDs остаются исходными. Один pending initialization содержит уже проверенный record и назначенную позицию; pending link — record и назначенную позицию. Origins не дублируются на миллион операций: прежние diagnostics/SourceMap выдаются в момент проверки. Allocation errors по-прежнему приводят к неуспеху публикации; эквивалентность конкретной точки allocator failure отдельно не заявляется.

Для проверки repetitions первая фаза использует два block-local flat indices: initialization target и link target. Они хранят ссылки на unique pending records. При первом обращении target дополнительно ищется в существующем G, что учитывает Header prefix и уже завершённые blocks. При replacement сохраняется назначенный старый slot. Это новый transient indexing cost; он включён в измерение.

Batch preparation вызывает существующие capacity helpers один раз на категорию/блок с числом новых unique records, резервирует final vectors геометрически и расширяет их один раз. Затем записывает payloads и вставляет новые keys; существующие indices при достаточной capacity не перестраиваются. При росте helpers перестраивают старый index до вставки новых keys. Это **блочная достройка индекса**, не новый глобальный sort/index algorithm и не полное устранение hashing. Повторное полное построение всех индексов на каждом блоке не выполняется.

## Семантика исполнения и SourceMap

Сохранён существующий final-state контракт, установленный предыдущим аудитом:

* Первый insertion event определяет slot, последняя запись того же `(WHO, endpoint_ref)` — payload.
* Link rebinding сохраняет target lineage/handle.
* Final initialization array не группируется по WHO, в том числе для aliasing writes.
* Runtime продолжает object construction → final link resolution → ordered final initialization list. Нового imperative statement interpreter нет. Пример `bind; write; write` не обобщается на исполнение каждого промежуточного rebinding как C++ statement.
* SourceMap получает те же roots, contributions и provisional handles, которые становятся действительными до успешного завершения Source. `sources.end_root()` не читает pending G records; `finalize` выполняется после materialization. Logical contributions прежних writers не отбрасываются.
* Проверка scalar target (включая const), reference compatibility и диагностика повторов остаются до следующего semantic statement. Строковый initializer сохраняет один warning по `any_replaced`, а не warning на каждый byte.

В успешном пути гарантирован явный flush до возврата из Source. На semantic error prefix проверенных операций материализуется при уничтожении локального batch. Отрицательные regression fixtures подтверждают прежние сообщения и origins; это не моделирование всех возможных allocation failures.

## Память и объём работы

Отдельный audit+verification run, исключённый из timed pairs:

| Метрика | Значение |
|---|---:|
| Проверенных initialization payloads | 705 237 |
| Проверенных link operations | 367 144 |
| Block limit | 65 536 pending unique targets |
| Bulk commits | 17 |
| Максимум pending records | 65 536 |
| Максимальная учтённая retained capacity буферов/flat indices | 5 412 444 bytes / **5,16 MiB** |
| Replacements | 0 |

Capacity accounting относится к двум payload vectors и двум pending indices; это не полный process peak и не peak старого+нового буфера во время reallocations. Проверки 1/7/65536 проходят, но performance серия выполнена для одного заранее выбранного 65536. Полный поиск лучшего размера блока здесь не проводился.

Медиана process peak working set: control 2 574 495 744 bytes, candidate 2 573 971 456 bytes. Парное уменьшение — 24 576 bytes, CI [−913 408; 737 280], различие не доказано. Отсутствие заметного роста глобального peak не означает отсутствия transient buffer: максимумы фаз могут не совпадать.

## Контроль наблюдения

7 calibration pairs: frozen production executable `build/enum-v4/Release/ServerEngineV4PublishBenchmark.exe` против mode 0 standalone. Неактивный standalone быстрее по полному PUBLISH на 6,53 мс, CI [1,67; 10,13]; Source — на 4,025 мс, CI [−0,786; 5,973]. В этой короткой серии есть небольшое различие полного времени; точный нулевой overhead не утверждается. Оно не вычиталось из результата candidate A/B.

Основное сравнение — mode 0 против mode 1 **в одном executable**. RG01 Graph observers полностью удалены из сгенерированного Graph/Source кода; его макрос используется только для существующего friend declaration без дополнительных полей. Счётчики и проверка каждого final index lookup включены только для отдельных verify runs. Нет per-call clocks и diagnostic replay.

## Проверка корректности

8/8 CTest suites:

* Исходный frontend control и полный frontend с bulk blocks 1, 7 и 65536.
* Existing Header-prefix initializations/links: replacement, ordered new append, stable reused link handle, invalid target для каждого из трёх block sizes.
* 32 positive/negative control/candidate invocations: extern completion, cross-file overwrites, scalar/array/string operations, reference rebinding, alias paths; forward object rejection, incompatible target, invalid index, duplicate definition, unresolved extern, later syntax и const-before-syntax priority. На успехе compiled/runtime digests совпадают, на ошибке stderr совпадает с control. Проверены final construction indices.

46 полных UnitProXL PUBLISH calls (2 audit/verification, 14 calibration arms, 30 measured candidate arms) сохранили:

* compiled.bin: `a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d`
* runtime.bin: `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`

Это строже logical equivalence: физический порядок record/handle и выпущенный Runtime-план сохранены. Изменённого placement format, новой семантики aliases или нового BUILD writer нет.

## Вывод по алгоритму

Функциональная граница Resolve/Validate → Bulk Materialize подтверждена без второго каталога объектов. Она сама по себе не сокращает достаточно работы. В данном варианте обычные incremental inserts заменены pending-buffer writes, дополнительным target index, проверкой существующего G и последующим final-array/index materialization. Сокращение количества capacity preparation calls не окупило новый путь в целом.

Из результата нельзя приписать все 39,361 мс конкретно hashing или копированию: отдельного CPU breakdown этих операций не проводилось. Но production adoption не требует такого ранжирования — полный кандидат уже не достиг порога и устойчиво ухудшил Source.

RG01 показал отсутствие repeated targets в данном UnitProXL. Поэтому свёртка overwritten writes не уменьшила final record count. Этот результат не доказывает невозможность любого bulk builder; он отвергает **данный bounded-buffer + block-local dedup + batch index extension** вариант. Увеличение буфера или ещё одна очередь перед теми же API не обоснованы этим измерением.

## Воспроизведение

Код/команды: `experiments/bulk-materialization-01/README.md`.

Сырые данные: `build/bulk-materialization-01/measurements/{runs.json,pairs.csv,summary.json,executables.json,source-manifest.json}` и stdout/stderr каждого процесса. CTest: `build/bulk-materialization-01-tests.log`. Source SHA фиксирует исходные Graph/Source translation units; executable SHA фиксирует реально измеренные binaries. Производственные файлы и настройки CPU/power не изменялись.
