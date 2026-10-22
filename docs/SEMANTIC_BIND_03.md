# SEMANTIC-BIND-03: Compact Storage & Lifetime

Эксперимент выполнен отдельно от production PUBLISH. Три изменения проверены независимо, затем совместно. Типовой Parser, semantic events, WHO numbering, scheduler и семантическое покрытие не менялись. `TypeRequest` по-прежнему занимает 120 байт; переход к полностью columnar/16-byte записи не смешан с этими измерениями.

**Результат:** sparse diagnostics убирает плотные пустые ошибки; segmented storage устраняет вторую полную материализацию requests; shared shape сокращает записи Binder, но текущий безопасный owner удерживает весь request arena и увеличивает цену последующего индексного чтения. Последнее — существенная граница этого прототипа.

## Что реализовано отдельно

| Вариант | Sparse diagnostics | Shared shape | Segmented requests |
|---|---|---|---|
| baseline | нет | нет | нет |
| sparse | да | нет | нет |
| shared_shape | нет | да | нет |
| segmented | нет | нет | да |
| combined | да | да | да |

Реализации разделены на `diagnostics.hpp`, `bound_storage.hpp`, `request_storage.hpp`; общая semantic wiring — `model.hpp`. Локальный Parser переиспользуется непосредственно из SEMANTIC-BIND-01. Для attribution используется одинаковый экспериментальный baseline в том же executable. Замороженный SEMANTIC-BIND-01 дополнительно измерен как bridge control: он не содержит общей новой arena/range-инфраструктуры.

### Sparse diagnostics

Вместо 751 783 `issue` хранится минимум на Binding task. Каждый task пишет свой слот один раз; reduction сохраняет порядок task/request и прежний выбор при одинаковом event. На 16 workers это 512 байт вместо 24 057 056. Проверки совместимости объявлений продолжают участвовать в выборе первой ошибки.

### Shared immutable shape

Bound record — 8-байтный resolved leaf (entity + intrinsic tag), вместо 32-байтного `bound_type` с собственным vector. Modifier sequences не копируются. Один shared owner на весь результат сохраняет исходную frozen request arena; shared_ptr на каждый запрос отсутствует.

Это обеспечивает lifetime при уничтожении batch, перемещении/копировании результата и освобождении одного из нескольких потребителей. Но owner удерживает **все requests**, а не только compact modifier arena. Внешний `bound_view` имеет обычный borrowed-span контракт. Владелец гарантирует числовые modifier sequences, не владение исходными spelling strings.

### Segmented storage

Combine переводит local scope/owner/definition references, затем перемещает buffers непустых worker vectors в frozen arena. Полного второго request vector нет. Symbol/declaration arrays остаются упорядоченными, как раньше.

Binder находит сегмент один раз на диапазон и дальше читает последовательно. Построение inventory также линейное. Индексный доступ результата может выполнять prefix search; это измерено отдельным downstream consumer. Spare capacity исходных vectors сохраняется, скрытого `shrink_to_fit` нет.

## Основная native-матрица

MSVC x64 Release + IPO, workers 1 и 16, пять свежих процессов на конфигурацию, один исключённый прогрев. Порядок вариантов/конфигураций менялся. Allocation executable запускался отдельно. Сборки/тесты не выполнялись параллельно измерениям.

Всего: 60 обычных измерений (включая legacy bridge), 12 прогревов и 10 allocation diagnostics. В каждом процессе — независимая сверка Header V2. Во всех вариантах: 3 503 734 effective tokens, 24 027 declarations, 751 783 type requests, 727 868 совпавших по типам полей. Исключённые callable/constructor-фрагменты: прежние 29 236.

Медианы времени модели, мс:

| Вариант | 1 worker | 16 workers | Primary retained, MiB на 16 workers |
|---|---:|---:|---:|
| baseline | 158,915 | 104,940 | 145,657 |
| sparse | 156,769 | 101,881 | 122,716 |
| shared_shape | 138,886 | 99,063 | 123,195 |
| segmented | 138,632 | 83,064 | 157,262 |
| combined | **114,156** | **73,799** | **111,858** |
| Frozen SEMANTIC-BIND-01 | 156,924 | 102,355 | 145,657 |

Общая новая arena/range-инфраструктура имеет собственный overhead относительно frozen legacy: поэтому отдельные эффекты не приписываются разнице с другим executable. Combined быстрее legacy bridge в данной серии, но ни один из этих результатов не является полным PUBLISH.

На 16 workers медиана парной экономии combined против baseline — 31,141 мс, диапазон пяти пар 30,729–34,370 мс. Отдельные варианты тоже имели положительную разницу во всех пяти парах; это наблюдение на этом корпусе/машине, не гарантия общего выигрыша и не оценка других проектов. Медианы стадий/общего времени не обязаны складываться или вычитаться как средние.

Стадии на 16 workers, мс:

| Вариант | Local Parser | Combine/prepare | Binding |
|---|---:|---:|---:|
| baseline | 48,001 | 17,085 | 30,647 |
| sparse | 48,635 | 17,046 | 27,313 |
| shared_shape | 48,200 | 17,017 | 25,104 |
| segmented | 48,684 | 4,030 | 29,340 |
| combined | 48,393 | 3,766 | 20,524 |

Parser почти не изменился: крупный root всё ещё одна task. SEMANTIC-BIND-03 не решает установленное в аудите плато параллелизма.

Таймер модели включает local parsing, ordered combine/forest preparation, canonicalization, binding, numbering и обычный cleanup локальных outputs. Подготовка/preprocessing входа, pool startup, oracle, окончательное уничтожение возвращённых данных и downstream consumption исключены. Границы те же, что у SEMANTIC-BIND-01; подробности в README.

## Память и lifetime

Primary retained subset восстановлен из capacity, сопоставимо с прежними 145,7 MiB; arena/segment metadata отдельно. Combined на 16 workers: **117 291 440 байт**, снижение **23,2%**. Дополнительная arena/segment metadata — 1 976 байт, без shared control block. Adjacency, временные maps и allocator metadata не включены в primary subset.

Shared shape сокращает bound-record array с 24 057 056 до 6 014 264 байт и убирает 5 510 832 байта скопированных modifier buffers. Sparse отдельно убирает почти 22,9 MiB diagnostics.

Segmented самостоятельно **увеличивает retained capacity**: worker vectors сохраняют 853 190 мест вместо точно 751 783 в общем массиве. Это добавляет 12 168 840 байт, примерно 11,6 MiB. Это плата за отсутствие второй материализации; противоположные эффекты нельзя называть одним «снижением памяти».

| Варианты | Живых requests после combine | Peak за весь pipeline, 16 workers |
|---|---:|---:|
| baseline / sparse / shared_shape | 1 503 566 | 1 503 566 |
| segmented / combined | **751 783** | **954 941** |

Оставшийся peak 954 941 возникает при росте local vectors во время параллельного Parser. На одном worker segmented/combined peak — 755 635. Поэтому утверждать, что общий максимум стал ровно 751 783, неверно.

Allocation diagnostics: Parser во всех вариантах — прежние 357 480 allocations. Binding с shared shape — **210 125 вместо 553 988**, на 343 863 операции меньше. Sparse в основном меняет объём одной allocation, а не число calls.

Глобальный tracked requested-byte peak на 16 workers: baseline/sparse/shared_shape около 587,4 MiB, segmented около 560,2 MiB, combined около 531,7 MiB. Он включает подготовку/вход и не равен peak модели. Sparse/shared отдельно не снижают глобальный peak, поскольку тот возникает раньше, на combine. У combined после удаления этого пика максимум смещается к Parser.

В обычных native процессах медианный lifetime PeakWorkingSetSize до oracle: baseline около 489,0 MiB, combined около 423,0 MiB. Это не model-only memory. Diagnostic allocator headers, CRT/OS allocations, mapped bytes и heap slack имеют другую границу; allocation-run времена не сравниваются с native.

## Проверка следующего потребителя

Дополнительный executable читает **текущий индексный API bound result**, когда batch, worker outputs, token/input buffers и spelling world уже уничтожены. Перед этим выполнена сверка Header V2. Проверены owner survival, одинаковые checksums и освобождение arena последним потребителем.

Для каждого варианта — три свежих процесса; внутри каждого один исключённый checksum-pass и пять измеряемых обходов каждого вида, с чередованием порядка. Медианы по процессам, мс:

| Вариант | Sequential indexed read | Fixed shuffled read | Retained result-only capacity, MiB |
|---|---:|---:|---:|
| baseline | 2,967 | 15,347 | 51,141 |
| sparse | 2,955 | 15,647 | 28,199 |
| shared_shape | 4,352 | 22,937 | 119,969 |
| segmented | 2,904 | 15,813 | 51,141 |
| combined | 5,823 | 39,940 | 108,633 |

Это single-thread checksum stand-in, **не Direct G**. Нельзя просто сложить эти медианы с основной матрицей и объявить результатом PUBLISH. Но измерение показывает реальную цену текущего интерфейса:

- Sequential shared-shape read делает дополнительный переход к исходным requests.
- Combined indexed read также использует segment-prefix lookup; larger request stride и memory locality имеют значение. Их отдельные вклады здесь не изолированы.
- Safe shared ownership удерживает весь request arena. После раннего уничтожения batch result-only footprint существенно выше owning reference, несмотря на меньший основной pipeline footprint.

**Shared shape как принцип корректен в проверенной модели; текущий владелец не является окончательным архитектурным решением.** Следующая компактная схема должна отделить immutable shape arena от тяжёлых request records и определить линейный consumer API. Иначе раннее освобождение batch не освобождает большинство его памяти, а поздний случайный доступ может съесть значительную часть выигрыша.

## Проверки и воспроизводимость

- Каждый из двух executable: 1350 существующих model checks (270 × 5 политик).
- Дополнительные lifetime/move/copy/churn/frozen-arena/range/diagnostic-tie checks.
- 320 сверок с независимым Header V2 плюс 10 allocation/lifetime fixtures: 330.
- Все 82 наблюдения основной матрицы и 15 процессов downstream consumer прошли parity/checksum/ownership проверки.
- Live tracked allocations вернулись в ноль во всех allocation-вариантах.
- CTest: SEMANTIC-BIND-03 3/3, SEMANTIC-BIND-02 3/3, SEMANTIC-BIND-01 2/2, CANONICALIZER-01 2/2, production 22/22. Старые экспериментальные executable пересобраны с выключенными новыми guards.

Raw данные, snapshots и measured executable hashes: `build/semantic-bind-03/results-final`; downstream — `build/semantic-bind-03/consumer-results`. Команды, структура политик и точные границы метрик: `experiments/semantic-bind-03/README.md`.

Production PUBLISH, его defaults, identity_ref ABI и файлы проекта не переключались. Полного Header semantics, construction/defaults, Direct G или нового результата PUBLISH <1 с этот эксперимент не доказывает.

## Архитектурное решение после эксперимента

Sparse diagnostics имеет прямой выигрыш без изменения поддерживаемого результата. Segmented storage устраняет доказанную вторую материализацию, с явной платой за slack. Shared immutable shapes требует отдельной компактной arena и границы времени жизни перед утверждением в окончательной системе.

HEADER-CHUNK-01 остаётся отдельным следующим экспериментом: эффективные token streams, границы завершённых declarations и scope/event context. Ускорение локального хранения не заменяет разделение критического root, а semantic coverage не следует расширять незаметно вместе с этой работой.
