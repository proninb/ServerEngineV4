# HEADER-ONLY-PUBLISH-01 — 23 октября 2026

Source inputs отключены в отдельной копии manifest UnitProXL. Исходный проект,
production Parser и настройки PUBLISH не изменены. Полная контрольная копия и
Header-only копия используют один frozen Release+IPO executable из
SOURCE-CPU-SAMPLING-02. В обоих случаях включён production Header V2.

Воспроизведение из корня репозитория:

```powershell
python experiments/header-only-publish-01/run.py build/publish-subsecond-01/fixture build/source-cpu-sampling-02/frozen/ServerEngineV4PublishBenchmark.exe build/header-only-publish-01/new-run
```

Замеры выполнены вне sandbox: одна warmup pair и восемь measured pairs,
чередование full/headers; отдельный процесс на каждый вызов. Без ETW и подробных
per-call таймеров. Данные: `build/header-only-publish-01/run-20261023/`.
CSV, stdout/stderr каждого вызова, executable/input hashes и summary сохранены.

| Медиана, мс | Полный проект | Только Headers |
|---|---:|---:|
| Полный PUBLISH | 1340,410 | 409,134 |
| Configuration | 25,427 | 9,493 |
| Lexical | 169,683 | 50,623 |
| Header V2 physical preparation | 76,035 | 74,628 |
| Header V2 semantic parse / G | 220,406 | 218,835 |
| Source phase | 419,774 | 0,032 |
| Persistence | 194,994 | 25,067 |
| Resident | 196,788 | 8,196 |

Source phase оставляет только пустой orchestration overhead: Source roots отсутствуют.
Медианы отдельных стадий не являются точным аддитивным разложением медианы total.
Разница медиан полного PUBLISH — 931,276 мс (69,5%). Она включает не только
Source Parser: исключены чтение Source inputs, source-derived G, его persistence
и runtime materialization. Поэтому нельзя объявлять её стоимостью Source Parser.

## Проверка результата

Обе конфигурации сохраняют 24 027 типов и 727 868 members. Header-only имеет
0 objects, 0 links, 51 048 strings, 24 028 identities. Полная конфигурация:
137 584 objects, 367 144 links, 188 630 strings, 161 612 identities.

На каждом повторе compiled/runtime SHA-256 совпали с предыдущим результатом
той же конфигурации. Header-only прошёл benchmark `audit`: LOAD и semantic/CRC
verification завершились успешно, все типы и поля доступны в конечном G.
Это проверка корректного Header-only artifact, а не byte parity с полным проектом.
compiled.bin полной копии также не сравнивается с историческим golden в другой
директории: физические пути входят в provenance. runtime.bin полной копии совпал
с прежним golden `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`.

## Новая экспериментальная архитектура на том же входе

После завершения production серии отдельно запущен EvaluatorParallel01:
16 frontend workers, 8 evaluator workers, target 32768, warmup + пять пар.
Все измеренные строки имеют validation=passed; исходный V2 oracle включён.

| Semantic projection lifecycle | Медиана, мс |
|---|---:|
| Sequential LAZY control | 183,443 |
| Parallel evaluator | 149,435 |
| Parallel local evaluation | 12,905 |

Это повторная проверка экспериментальной constructor/type projection без Sources.
Она не включает весь путь от файлов до конечного G, String IDs/SourceMap parity,
persistence и resident materialization. Её нельзя непосредственно вычитать из
production header_ms или 409,134 мс полного Header-only PUBLISH.

Вывод: production Header-only путь до конечных артефактов работает примерно за
409 мс. Удаление Sources не ускоряет собственно Header V2 существенно. Для
сквозного сравнения новой Header архитектуры по-прежнему необходимы недостающее
семантическое покрытие и Direct G integration; текущие 149 мс — результат projection.

Последующая реализация: `docs/HEADER_DIRECT_G_01.md`. Новый pipeline теперь
формирует конечный G и G-only compiled image; сквозной files→G замер выполнен
отдельно. Production SourceMap/byte parity и интеграция в PUBLISH остаются отдельно.
