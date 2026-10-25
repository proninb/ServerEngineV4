# RUNTIME-SHM-FAULTS-01 — kernel attribution established

Проанализирована существующая ETW-серия
`build/publish-critical-path-02/capture-20261023-144348/`.
Новый захват, новый benchmark и изменения production не потребовались.

## Результат

| CPU samples под apply_api_one | Количество |
| --- | ---: |
| Все inclusive | 120711 |
| Leaf в ntoskrnl | 93821 |
| Стек включает KiPageFault | 93430 |
| Стек включает MmAccessFault | 87779 |
| Стек включает MiResolveProtoPteFault | 64010 |
| Стек включает MiResolveDemandZeroFault | 63232 |
| Exclusive leaf самого apply_api_one | 26431 |

Inclusive строки пересекаются: суммировать их нельзя. Около **77,7% CPU samples
под apply_api_one** имеют kernel leaf. Примерно **99,6% этих kernel-leaf samples**
проходят через KiPageFault. Это подтверждает значительную работу обработчика
ошибок страниц при materialization, включая demand-zero и prototype-PTE paths.
Это не число page faults и не время отдельного fault.

Наиболее дорогие exclusive kernel leaves:

| Leaf | Samples |
| --- | ---: |
| MiAddWorkingSetEntries | 27412 |
| ExpAcquireSpinLockSharedAtDpcLevelInstrumented | 13636 |
| KiPageFault | 5562 |
| MiUserFault | 3709 |
| MiGetPageChain | 3568 |
| MiCheckProtoAccess | 3489 |

Для dominant shared spin-lock leaf ближайшие различающиеся callers —
MiAddWorkingSetEntries (7000), MiCheckProtoAccess (3558), MiUserFault (2653).
Это обосновывает гипотезу конкуренции в Memory Manager/working-set paths;
профиль не измеряет длительность ожидания конкретной блокировки.
Небольшое число samples связано с прерываниями/драйверами; не вся kernel-работа
в стеке приложения является его собственной работой.

В коде `fixed_shared_memory.cpp` SHM создаётся через CreateFileMappingW с
INVALID_HANDLE_VALUE/PAGE_READWRITE и MapViewOfFileEx. Object materialization
выполняет первый доступ к страницам без отдельного pretouch; это согласуется
с наблюдаемыми demand-zero/working-set paths. SHA-256 и семантические WHO здесь
не участвуют.

## Качество и пределы доказательства

- Сохранены все 284572 исходных CPU samples и все 305 missing-stack samples.
- После загрузки PDB **нет неразрешённых kernel-фреймов** в целевой выборке.
- Нулевые event losses, исходный 1 ms interval, непрерывность выборки сохранена.
- Для каждого события сверены block/PID/TID/time/count/weight, длина стека и
  все фреймы вне ntoskrnl/ntdll с прежним экспортом. Проверка прошла во всех 8 блоках.
- Событий page faults в этой CPU-трассе нет: event inventory всех блоков не
  содержит fault events. Отсутствие событий не означает отсутствия faults.
- Не делаем вывод о числе faults, hard/soft fault rate, disk I/O или latency.
- Не подменяем wall time суммой CPU потоков. Контрольный resident этап был
  около 190,7 ms; этот профиль не обещает экономию 77,7% его wall time.
- Исходная фильтрация block/PID и IPO/ICF ограничения приложения остаются.

## Соответствие символов

Загружены только публичные ntkrnlmp.pdb и x64 ntdll.pdb с Microsoft Symbol Server.
Трасса, исходники и приложение никуда не отправлялись. URL содержит имя PDB,
GUID и image age. SHA-256 загруженных PDB записан в локальный manifest.

Ядро: GUID b661c6fe-7299-1d16-0fe3-7c48b8bc5470, image age 1,
PDB Info age 6, DBI age 1. ntdll: GUID c031d45b-0da1-8a6e-74d0-6ed6ab2dd9e1,
image age 1, Info age 4, DBI age 1. Различие Info age само по себе не означает
неверный public PDB: применена проверка GUID exact, Info age >= image age,
DBI age == image age (legacy DBI age 0 также допустимая ветвь формата).
Правило взято из Microsoft [PDB OpenValidate4](https://github.com/microsoft/microsoft-pdb/blob/master/PDB/dbi/pdb.cpp#L783).

Старый строгий verifier application PDB не ослаблялся. Отдельный OS verifier
прошёл 3/3 теста: реальный public PDB, неправильный GUID, неправильный DBI age.
OS имена разрешены DIA по RVA из проверенного модуля; Python verifier проверяет
MSF identity, C# reader дополнительно сверяет SHA файла и native PDB GUID.

## Следующий эксперимент

**RUNTIME-OBJECT-LANES-01**: ограничить параллелизм только Object materialization.
Сравнить 4/8/16 workers с текущими 24, сохраняя непрерывные object ranges,
canonical/link-marking порядок, физический план и остальные pools.

Гипотеза: уменьшение одновременных page-fault/working-set updates может снизить
kernel contention больше, чем увеличит пользовательское время исполнения.
Это нужно проверить: меньше workers может оказаться и медленнее.

Первый вариант не должен добавлять pretouch, large pages, новый scheduler,
range-validation cache, менять G/runtime.bin или подключать early context release.
Измерять полный PUBLISH парным A/B, resident/Object wall и память.
Golden артефакты обязательны, но они сами не доказывают равенство resident SHM:
нужны существующие runtime fixtures и отдельная проверка построенного Runtime.

## Воспроизведение

```powershell
dotnet build experiments/runtime-shm-faults-01/reader/KernelReader.csproj -c Release -o build/runtime-shm-faults-01/reader -p:BaseIntermediateOutputPath=C:/Boris/ServerEngineV4/build/runtime-shm-faults-01/reader-obj/ -p:UseSharedCompilation=false
dotnet build/runtime-shm-faults-01/reader/KernelReader.dll build/publish-critical-path-02/capture-20261023-144348/b0.etl build/runtime-shm-faults-01/inspection-b0.json
python experiments/runtime-shm-faults-01/fetch_symbols.py
python experiments/runtime-shm-faults-01/export.py
python experiments/runtime-shm-faults-01/analyze.py
python experiments/runtime-shm-faults-01/test_symbols.py
```

Reader использует установленный TraceEvent 3.1.27 и DIA; msdia140.dll должен быть
рядом с KernelReader.dll и в amd64/, как у предыдущего CPU reader.
Результаты: `build/runtime-shm-faults-01/analysis/summary.json`, quality.json,
новые per-block JSONL/metadata; символы/identity — `symbols/manifest.json`.
Исходные captures и frozen production binary не изменены.
