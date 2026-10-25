# HEADER-V2-STATE-LAZY-01 — 23 октября 2026

**Принято для V2.** Узкая оптимизация production Header без новой semantic модели,
Local G, event assembly или deferred writer. WHO, String IDs и Graph формат не меняются.

## Изменение

Пять record-local unordered containers теперь создаются при первом write/reserve:
pending constructor operations, reference waiting, reference generations,
nested waiting, nested completed lookup. `std::optional` хранит container на месте;
дополнительного heap owner нет. Пустые find/empty/range reads ничего не выделяют.
После первой мутации используется прежний std::unordered_map/multimap до конца
record. Порядок вставок/обхода, lookup, reserve, generations, errors и commit прежние.
`header_parser_v2.cpp` не изменён; меняются только типы этих пяти полей и adapter.

Материализованные iterators имеют прежние правила STL. Singular empty iterators
не переносятся через первую мутацию; V2 call sites проверены, этот случай отсутствует.
Первое создание container происходит внутри существующих try/catch mutation paths.

Production default — lazy. A/B macro и getenv доступны исключительно benchmark/
frontend test targets при opt-in SERVER_ENGINE_HEADER_STATE_LAZY_AB=ON. В обычном
ServerEngineV4 нет runtime environment switch этой оптимизации.

## Парный полный PUBLISH

UnitProXL с Headers **и Sources**, тот же изолированный full fixture и executable
MSVC Release /O2 /Ob2 + IPO. Две warmup pairs, двадцать measured pairs; порядок
eager/lazy чередуется. Без ETW/подробных per-call clocks и параллельных builds.

| Медиана | Eager control | Lazy |
|---|---:|---:|
| Полный PUBLISH, мс | 1355,73 | 1345,51 |
| Header semantic, мс | 221,173 | 213,520 |
| Peak WS, bytes | 2 574 057 472 | 2 573 955 072 |

Медиана **парного** выигрыша PUBLISH — **10,29 мс**, быстрее **15/20** пар.
Средний парный выигрыш — **11,025 мс**; paired bootstrap 95% CI среднего
**[2,667; 21,314] мс**. Header: медиана парного выигрыша **8,604 мс**, быстрее
**20/20**; средний выигрыш 8,765 мс, CI **[7,136; 10,518] мс**.
Разность двух медиан и медиана парных разностей — разные статистики.
Outliers не исключались. Значимого снижения peak working set не заявляем.

Eager control использует общий optional adapter, поэтому отдельная проверка
с исходным V2 EXE до изменения обязательна для оценки переноса результата.

## Bridge с исходным V2 executable

Две warmup + двенадцать measured pairs. Original frozen Release+IPO V2 из
SOURCE-CPU-SAMPLING-02 против нового lazy benchmark. Это два executable,
в отличие от основной same-executable серии; результаты не объединяются.

| Медиана, мс | Original V2 | Lazy |
|---|---:|---:|
| Полный PUBLISH | 1348,965 | 1337,050 |
| Header semantic | 220,704 | 213,494 |

Медиана парного выигрыша полного PUBLISH **10,23 мс**, быстрее **11/12**.
Средний выигрыш **10,402 мс**, bootstrap 95% CI **[5,313; 16,473] мс**.
Header быстрее **12/12**, медиана парного выигрыша **6,136 мс**.
Наблюдаемая оценка выигрыша полного PUBLISH — около **0,8%**, а не кратное ускорение.

## Проверка корректности и allocations

Все **72 полных PUBLISH**, включая warmups двух серий, сохранили оба golden SHA:

- compiled.bin: `78c3e0ee6bdcc7652bd2a0623d6addef656964a1d7ecdb79dfd40b87a2b8eea0`;
- runtime.bin: `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`.

Это golden из того же full fixture; compiled.bin зависит от физических путей,
поэтому его hash отличается от fixture в другой директории. Сохранены все
24 027 типов, 727 868 members, объекты/links и production SourceMap.

Отдельный counted allocator test на установленной MSVC STL:
пять пустых eager test indices — **10 allocations / 760 requested bytes**;
lazy empty indices — **0 allocations**, включая empty reads и destruction.
Mapped test types упрощены: bytes нельзя экстраполировать на реальные private
state types или на полный PUBLISH. Это проверка причины, не production allocator audit.

Regression tests проверяют сам adapter и существующую Header/Source семантику:
pending/forward/stale references, nested/default constructors, errors и Graph.
Release A/B suite: **3/3**. Production option-OFF suite: **2/2**;
Debug STL adapter: **1/1**. Результаты находятся в
build/header-v2-state-lazy-production-tests.log и header-v2-state-lazy-debug-tests.log.

## Воспроизведение

Команды и область изменения: `experiments/header-v2-state-lazy-01/README.md`.
Все CSV, stdout/stderr, EXE/source hashes, golden checks и CI сохранены в
`build/header-v2-state-lazy-01/results-20261023/` и `bridge-20261023/`.
Ни старый параллельный Header pipeline, ни Direct G adapter не возвращены.
Эффект проверен на Windows/MSVC/UnitProXL; других платформ/corpora не измеряли.
