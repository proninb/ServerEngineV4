# HEADER-CHUNK-02 — parallel extraction и ordered semantic assembly

Эксперимент реализован. Для проверяемой SB04 type-binding projection один
длинный root действительно превращается в параллельные задачи без копирования
накопленного каталога типов. Семантический порядок, WHO, типы, origins и первое
diagnostic совпадают с whole-root SB04; отдельно выполнено сравнение с Header V2.

Целевой порог **75 мс при 16 workers, включая discovery**, пройден в пяти
измерениях target 8192. Target 32768 в этой серии быстрее: **65,85 мс** против
парного контроля SB04 **85,14 мс**. Production PUBLISH не изменён.

## Архитектура

Discovery из HEADER-CHUNK-01 используется без изменений. Workers читают
ограниченный диапазон исходных effective tokens и выдают local symbols,
declarations, requests и immutable shapes. Original root ordinals, token/event
coordinates и physical origins сохраняются. WHO и Local G внутри workers нет.

Extraction использует грамматику SB04: identifier типа остаётся именем и
событием до temporal Binding. Накопленный semantic environment не копируется.
Это доказательство для этой проекции, не для произвольного C++ Parser с
symbol-dependent grammar или полной constructor/reference семантикой.

Ordered assembly восстанавливает тот же порядок symbols/declarations, что у
whole-root Parser, включая один root symbol на исходный root и namespace
events из descriptors. Затем переназначает локальные symbol/definition ссылки
в индексы assembled model. Request buffers перемещаются сегментами; modifier
payload не копируется. После этого работают прежние canonicalization, temporal
Binding и deterministic numbering SB04.

Если extraction встретил синтаксическую ошибку, результаты более поздних
chunks того же root отбрасываются, как при остановке whole-root Parser.
Если discovery отменил разбиение root, он разбирается прежним whole-root Parser
в одной task. Неподдержанная семантика не объявляется поддержанной этим fallback.

## Методика

MSVC Release + IPO, native процессы, исходная тестовая копия UnitProXL.
Вход одинаковый и уже прошёл preprocessing; input preparation не входит в
semantic lifecycle. Whole-root/chunk runs чередуются внутри одного процесса
и используют один persistent worker pool.

1/2/4/8/16 workers × targets 1024/8192/32768, по пять измеренных пар и одной
исключённой прогревочной паре: **150 измеренных и 30 прогревочных lifecycle**
в 15 fresh processes. Дополнительно шесть allocation processes для 1 и 16
workers. Сборки и тесты одновременно с performance measurements не выполнялись.

Chunk lifecycle включает discovery, planning, dispatch/extraction, ordered
assembly, release worker outputs, canonicalization, Binding, numbering,
освобождение batch/requests, один sequential checksum consumer, освобождение
результата и numbering maps. Oracle и memory capacity accounting вне таймера.
Оставшиеся временные структуры функции также уничтожаются до завершения
lifecycle. Таким образом discovery не вынесен из итогового результата.

## Target 8192: масштабирование

Медианы пяти пар на конфигурацию:

| Workers | SB04 Parser, мс | Extraction wall, мс | Assembly, мс | Critical chunk, мс | SB04 lifecycle, мс | Chunk lifecycle, мс |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 74,61 | 70,12 | 5,67 | 0,48 | 124,59 | 135,26 |
| 2 | 40,07 | 37,07 | 5,23 | 0,55 | 84,92 | 93,57 |
| 4 | 40,50 | 20,54 | 5,16 | 0,60 | 84,84 | 76,36 |
| 8 | 42,86 | 17,02 | 4,93 | 1,14 | 84,86 | 71,95 |
| 16 | 45,36 | 18,17 | 5,10 | 1,71 | 87,90 | **70,88** |

При 16 workers: discovery **5,22 мс**, planning **0,062 мс**,
canonicalization **0,84 мс**, numbering **0,28 мс**. Binding — **28,72 мс**.
Общий выигрыш lifecycle относительно парного SB04 — **19,4%**.
Все пять chunk lifecycle находятся в **69,19–73,20 мс**, ниже целевых 75 мс.
Парные разности SB04−chunk положительны во всех пяти раундах: 13,74–26,66 мс,
медиана парной разности **17,19 мс**.

При одном и двух workers дополнительная работа discovery/assembly/teardown
не окупилась. Поэтому это не универсальная замена whole-root execution policy.
После 8 workers extraction почти не ускоряется, хотя длинный root уже не
остаётся единственной задачей. Причину нового плато нельзя выводить только
из количества chunks; нужны отдельные measurements/profiling при продолжении.

`Critical chunk` — медиана максимальной длительности одной task в каждом
измерении. Это execution wall time с созданием/передачей output, без queue wait;
он может включать preemption/allocator contention. Уменьшение critical chunk
само по себе не гарантирует ускорения всего пути.

## Размер chunks при 16 workers

| Target | Tasks | Extraction, мс | Assembly, мс | Critical chunk, мс | SB04 lifecycle, мс | Chunk lifecycle, мс |
|---|---:|---:|---:|---:|---:|---:|
| 1024 | 3909 | 22,04 | 9,16 | 0,89 | 88,49 | 88,43 |
| 8192 | 473 | 18,17 | 5,10 | 1,71 | 87,90 | 70,88 |
| 32768 | 126 | 14,41 | 4,67 | 3,25 | 85,14 | **65,85** |

Target 1024 практически не дал общего выигрыша; парная разность меняет знак,
медиана SB04−chunk **−0,83 мс**. Assembly и количество allocations выросли.
Это подтверждает необходимость выбирать по полному пути, а не по самой короткой
задаче. Максимальная неделимая declaration остаётся 2515 токенов.

Target 32768: discovery **4,79 мс**, planning **0,039 мс**, canonicalization
**0,87 мс**, Binding **27,93 мс**, numbering **0,27 мс**, model **53,38 мс**.
Lifecycle — **64,62–66,67 мс** во всех пяти измерениях; выигрыш относительно
своего SB04 контроля — **22,7%**, медиана парной разности **19,55 мс**.

При 8 workers/32768 медиана **65,63 мс**. Разница с 16 workers мала и сама по
себе не устанавливает оптимальное число workers. Target 32768 — лучший
измеренный кандидат этой серии, не окончательный production scheduler.
Контрольные времена различаются между конфигурациями, поэтому сравнения
SB04/chunk делаются внутри соответствующей пары, а не с историческими 90,4 мс.

## Память

16 workers; основные массивы и result-only capacity:

| Показатель | SB04 | Chunk 8192 | Chunk 32768 |
|---|---:|---:|---:|
| Primary arrays, MiB | 135,11 | 143,85 | 141,82 |
| Retained result capacity, MiB | 40,75 | 40,85 | 41,05 |
| Peak tracked requested bytes | 154 789 394 | 164 500 178 | 162 239 516 |
| Allocation calls | 598 171 | 616 179 | 597 323 |
| Total requested bytes | 395 166 005 | 430 480 538 | 422 837 762 |
| Peak request instances | 954 941 | 753 272 | 755 635 |

Количество одновременно живых requests уменьшилось, но peak requested memory
вырос. Поэтому сокращение request instances не выдаётся за сокращение всей
памяти. Target 1024 имеет ещё 728 728 allocations и peak 168 095 755 байт.

Для всех diagnostic configurations на границе consumer — **0 request instances**,
после последнего результата — **0 tracked live bytes**. Shape arenas сохраняют
независимый от Parser requests lifetime SB04. Retained capacity исключает
numbering maps и allocator metadata; allocation peak включает временные
структуры semantic model, но исключает input/pool startup и OS heap metadata.
Это не RSS всего процесса. Instrumented timing не используется.

## Parity и ограничения

Три CTest проходят:

- 480 exact chunk/SB04 cases в каждой из timing/allocation сборок:
  16 входов × 5 worker counts × 3 targets × прямой/обратный порядок задач.
  Дополнительно прежние 270 scheduling/model checks и пустые roots.
- 198 independent Header V2 fixture cases: 33 входа × workers 1/4 × targets
  1/32/8192. Включены положительные и отрицательные temporal lookup случаи,
  namespaces, incomplete types, aliases, duplicate definitions и include reuse.
- В каждом real-corpus процессе exact comparison с SB04 при прямом и обратном
  task order, затем независимый V2 oracle. Совпали symbols и порядок их создания,
  declarations, requests, begin/open/complete/events, physical origins,
  scope/owner/definition, modifiers, destinations, exclusions, types, WHO и
  первое diagnostic. V2 проверяет инвентарь **727 868 полей**; во всех timed
  runs совпадает checksum **751 783 type requests**. Fallback на реальном
  корпусе не понадобился.

В реальной проекции UnitProXL все declarations глобальные. Namespace context
assembly и temporal visibility подтверждены на synthetic/V2 fixtures, а не на
реальной сложной namespace-иерархии. Constructor/callable и initializer gaps
остаются прежними; их совпадающее число не означает полную Header-семантику.
Полный production PUBLISH, Direct G, String ID byte parity и SourceMap этим
экспериментом не проверяются.

## Результат архитектурного эксперимента

Для проверенной грамматики независимое local extraction возможно. Global WHO
и накопленный каталог типов worker не требуются; original semantic order
восстанавливается до canonicalization и temporal Binding. Длинный root перестал
быть единственной критической задачей, и выигрыш сохранился после discovery,
assembly и teardown.

При этом границу применимости нельзя переносить автоматически на полный
Header Parser. Следующие содержательные вопросы — полное семантическое
покрытие и стоимость формирования/освобождения данных; Binding около 28 мс
теперь занимает большую часть модели. Увеличение числа workers само по себе
нового выигрыша в этой серии не показало.

Исходники, контракты и команды: [README](../experiments/header-chunk-02/README.md).
Raw CSV/stdout/stderr, summary, source snapshots, executable hashes и SHA256
engine objects: `build/header-chunk-02/results-final/`.
