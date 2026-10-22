# CANONICALIZER-01: модель идентичности и стоимость согласования

Изолированный эксперимент реализован в experiments/canonicalizer-01/.
Рабочий PUBLISH использует прежние Parser, identity_space, String Table и G.
Новая архитектура и алгоритм для неё этим экспериментом не утверждаются.

## Контракты и проверка модели

Вход: локальный parent, kind, ссылка на неизменяемые байты имени, producer и
событие семантического порядка. Ожидаемые legacy ID находятся в тестовом
оракуле; kernels не используют их для объединения. Импорты контекста родителей
не являются объявлениями и сами не создают numbered entity.

Реализованы независимый последовательный reference canonicalizer и три kernels:

1. Sort + unique: параллельная сортировка и слияние ключей каждого уровня.
2. Exact-key index: распределение полных ключей по partitions и независимые
   индексы с полной проверкой строкового равенства.
3. Hierarchical merge: сортировка локальных child runs, затем их слияние по
   уже согласованному родителю.

Во всех случаях ключ — (CanonicalParent, Kind, Name), а не LocalParent.
Прототип использует барьеры по глубине для доказательства корректности; это
не требование глобального барьера в будущей поточной архитектуре.

Все четыре варианта используют одну реализацию numbering. Она выделяет классы,
находит первое реальное событие создания и назначает номера в его порядке.
String ID нумеруются отдельно по первому intern, включая неиспользованные имена.
Лексикографический порядок не влияет на WHO: Z перед A сохраняется.

Проверки включают reopened namespaces, разные родители одинакового имени,
forward declaration/definition, kind, ancestor lookup, shadowing во времени,
обращения до объявления, локальные aliases родителей, принудительные коллизии,
одинаковые длинные префиксы с разными последними байтами, перестановки записей,
циклы, неизвестных родителей, import без объявления и неоднозначные event IDs.
Два набора tests выполняют 480 algorithm/model checks на 1/2/4/8/16 workers,
а также отрицательные проверки.

## Реальные данные

Диагностическая сборка захватывает события только своего String Table и
identity_space. Capture выключен по умолчанию и включается только для отдельного
PUBLISH benchmark target. Production server и обычные test targets не получают
этих hooks. Время capture PUBLISH не используется для оценки ускорения.

UnitProXL:

| Величина | Значение |
| --- | ---: |
| Размер trace | 194 071 917 bytes |
| intern events | 2 724 481 |
| resolve events | 161 611 |
| exact-scope find queries | 1 579 453 |
| Итоговые strings | 188 630 |
| Итоговые identities, включая root | 161 612 |
| Локальные string symbols при 16 producers, включая roots | 366 100 |
| Локальные identity symbols, включая roots | 161 627 |
| Cross-worker string duplicates | 177 454 |
| Cross-worker identity declaration duplicates | 0 |
| Глубина identity-иерархии | 1 |

Здесь identity-дерево широкое и плоское. Оно само по себе не проверяет цену
повторных namespaces. Поэтому есть второй реальный Header/Source corpus:
N::A forward/definition, отдельный root A, reopened N, extern/definition a.
У него 5 strings, 6 identities, 15 lookup queries и 5 cross-worker identity
duplicates. Синтетика дополняет эти данные: 10 000 типов с повторами в 16
producers под одним root либо в 256 namespaces.

Захват после финальной доработки diagnostic hooks воспроизвёл исходный trace SHA256.
Capture PUBLISH и обычный PUBLISH сохраняют оба полных golden SHA артефактов.

## Методика

16 логических producer streams фиксированы для всех запусков. Меняется только
число исполнительных workers. Корпус, число символов, queries и duplicates при
масштабировании неизменны; анализ это проверяет.

Каждый запуск — новый native Release/IPO process на AMD Ryzen 9 9900X (24 logical CPUs).
Для UnitProXL: 7 измеренных повторов каждой из 15 конфигураций. Для остальных
трёх datasets: по 5 повторов. Один warmup round на dataset исключён. Порядок
конфигураций вращается и разворачивается. Во время замеров нет сборок/тестов.

Итоговая матрица: 330 измеренных запусков, 60 исключённых прогревов и 64
отдельных diagnostic runs, включая reference. Все проверяют numbering и queries.
Allocation/comparison counters выключены в timing runs.
Индексирование получает сырые имена: вычисление name hashes, выделение и
освобождение fingerprint workspace включены в canonical_ms. Бесплатного
предварительного хеширования для индекса в окончательной серии нет.

## Масштабирование: UnitProXL

Медиана canonicalization + общего numbering, ms:

| Алгоритм | 1 worker | 2 | 4 | 8 | 16 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Sort + unique | 182.326 | 120.643 | 90.931 | 74.983 | 69.666 |
| Exact-key index | 155.139 | 92.494 | 52.132 | 35.166 | 28.028 |
| Hierarchical merge | 206.306 | 159.519 | 130.810 | 118.437 | 119.652 |

Отдельные фазы при 16 workers, **средние одних и тех же 7 запусков**, ms:

| Алгоритм | Canonicalization | Общий numbering | Сумма |
| --- | ---: | ---: | ---: |
| Sort + unique | 63.379 | 6.758 | 70.137 |
| Exact-key index | 21.434 | 6.784 | 28.218 |
| Hierarchical merge | 112.074 | 6.849 | 118.923 |

Медиана суммы не обязана равняться сумме медиан; для разбивки выше использованы
согласованные средние. Startup worker pool учитывается отдельно: около 0.4 ms
при 16 workers. kernel_ms исключает startup, trace decoding, validation и G.

## Память и строковые сравнения

Отдельные диагностические запуски UnitProXL при 16 workers:

| Алгоритм | Peak tracked temporary bytes | Full-spelling comparison calls |
| --- | ---: | ---: |
| Sort + unique | 9 826 180 | 13 934 580 |
| Exact-key index | 11 610 992 | 902 291 |
| Hierarchical merge | 9 826 180 | 15 972 710 |

Peak tracked — максимальная сумма запрошенных через PMR bytes для kernel,
remap/numbering и merge/index workspaces. Она исключает input, thread stacks,
allocator overhead и небольшие control objects. Native process peak working
set записан отдельно в CSV. Его максимум может определяться подготовкой input.
comparison_span_bytes — сумма логических spans сравнений, а не hardware bytes
фактически прочитанных SIMD. Диагностические времена не ранжируют алгоритмы.

Первый index имел корреляцию partition bits и внутренних buckets MSVC hash table:
13 287 011 сравнений вместо 902 291 при 16 workers. Хеши разделены; финальная
матрица полностью повторена. Старая серия и executable сохранены отдельно,
их результаты не смешиваются с исправленной реализацией.
Предварительная серия с уже подготовленными name hashes сохранена в
results-prehash/; окончательная results-final/ включает их вычисление.

## Зависимость от данных

На маленьком реальном scope-corpus все три алгоритма в одном потоке занимают
примерно 0.006–0.011 ms, а при 16 workers — 1.15–1.32 ms даже без pool startup.
Мелкую работу невыгодно раздавать этому pool.

На nested-синтетике при 16 workers: index 4.045 ms, hierarchical 4.741 ms,
sort 6.218 ms. Разница с UnitProXL показывает влияние структуры и доли повторов.
У hierarchical merge широкая canonical root ограничивает параллельность merge.
Полные таблицы, min/max/MAD и раздельные фазы сохранены в summary.csv/json.

## Что доказано и что остаётся открытым

Доказана эквивалентность классов и общая детерминированная нумерация на проверенных
входах, включая реальный transcript. Каждый запуск побайтно сравнивает отдельную
проекцию strings + WHO/parent/name-ID с независимым legacy oracle. Все захваченные
find queries проверяются с учётом событий видимости.

Это **identity/string projection byte parity**, а не результат нового Parser,
построившего весь compiled.bin. Семантические event numbers тоже получены
из legacy transcript; их построение новым parallel Parser ещё не реализовано. Trace loader предоставляет известный исходному
Parser контекст родителей. Он не доказывает, что новый parallel Parser способен
получить этот контекст, корректно разобрать всю грамматику или проверить все
совместимости определений. Source Map, WHERE, ошибки и direct G ещё не перенесены.

Input replay/localization занимает около 394–396 ms в этих процессах: чтение
194 MB trace, построение локальных symbols, локальное объединение строк,
imports и queries. Этот код не предлагается включать в PUBLISH. Его стоимость
не скрывается внутри kernel и не считается бесплатно выполненной Parser.
Проверка локального дерева, вычисление depth и child adjacency также находятся
в input_ms. Здесь измеряется agreement kernel на нормализованном локальном
дереве; это не вся стадия обработки сырых результатов будущего Parser.
Из 28 ms kernel нельзя вывести время новой полной системы или экономию 400 ms.

На данном kernel/corpus индексирование — лучший кандидат для следующего
интеграционного эксперимента. Оно не утверждается как окончательная архитектура.
Поточная нумерация по завершённым префиксам остаётся следующим отдельным опытом.

## Артефакты и проверка рабочего пути

- Код, CMake и scripts: experiments/canonicalizer-01/.
- Итоговая серия: build/canonicalizer-01/results-final/.
- Первая серия до исправления hash correlation: build/canonicalizer-01/results/.
- Реальные corpora: build/canonicalizer-01/unitproxl.trace и scopes.trace.
- Capture SHA256 и benchmark executable SHA256: results-final/manifest.json.
- Production defaults не изменены. Рабочие 22 CTest tests, golden compiled/runtime
  SHA и независимый audit прошли; standalone CTest проверяет модель отдельно.
