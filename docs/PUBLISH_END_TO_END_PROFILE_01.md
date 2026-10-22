# PUBLISH-END-TO-END-PROFILE-01

Актуальный baseline текущего production PUBLISH, без нового Parser/evaluator и без
trace hooks. Benchmark собран заново из корневого CMake, MSVC Release + IPO, PGO OFF,
обычные default options. Header/Source overlap OFF; Source ready tokens, parallel
prehash, string batch и path reuse ON. Использована build-копия UnitProXL; исходный
проект не менялся.

## Методика

Одна исключённая warmup pair, затем восемь пар ordinary publish / publish-timeline.
Порядок вариантов чередуется. Все вызовы отдельными процессами, native вне sandbox;
builds, tests и differential trace во время серии не запускались. После каждого
из 18 вызовов проверены frozen SHA256 compiled.bin/runtime.bin. Это baseline,
а не A/B нового алгоритма и не доказательство ускорения относительно старой серии.

## Измерения

Медиана ordinary PUBLISH: **1337,50 мс**. Медиана timeline: **1348,80 мс**.
Одинаковые artifacts во всех вызовах: 24 027 types, 727 868 members, 137 584 objects,
367 144 links, 188 630 strings, 161 612 identities.

Таблица показывает **средние** timeline по одним и тем же восьми вызовам, чтобы
сохранить арифметическое разложение (медианы доступны в summary.json).

| Верхний уровень, взаимоисключающие расходы | Среднее, мс |
| --- | ---: |
| Cleanup | 7,156 |
| Context setup | 0,009 |
| Configuration | 29,355 |
| Initial lexical preparation | 174,875 |
| Assign | 0,030 |
| Semantic, всего | 719,599 |
| Post-semantic | 2,711 |
| Persistence | 200,758 |
| Post-persistence | 0,005 |
| Resident runtime publication | 195,004 |
| Temporary context release | 20,725 |
| Accounted | 1350,225 |
| Unaccounted внутри operation | 0,009 |
| Operation | 1350,234 |
| Внешний caller overhead | 0,003 |
| Caller total | 1350,239 |

Подразделение semantic: эти строки уже входят в 719,599 мс выше.

| Внутри semantic | Среднее, мс |
| --- | ---: |
| Header physical preparation | 74,903 |
| Header semantics + effective preprocessing + Header G writes | 219,726 |
| Source Parser + его preprocessing/binding/G writes | 412,817 |
| SourceMap finalization | 1,473 |
| Остальное semantic | 10,681 |

Максимальное абсолютное unaccounted по серии: **0,0107 мс**. Timeline отключает
per-token Source и Header member counters; это проверено по нулевым счётчикам.
Разности timeline minus ordinary внутри пар лежат от −24,76 до +63,78 мс: не следует
приписывать их только observer overhead или считать вычитание двух медиан его
точной оценкой. Эта серия не устанавливает причину вариации между вызовами.

## Ограничения и приоритеты

Это coarse accounting полного pipeline. **Чистое effective preprocessing и отдельная
стоимость мутаций Graph не выделены** из Header/Source. Их нельзя объявлять нулевыми
или оценивать вычитанием experimental lifecycle. Resident runtime publication —
отдельная стадия материализации runtime, не таймер всех Graph API calls. Подробная
эксклюзивная разбивка preprocessing/Graph требует отдельного инструмента и observer
control; текущая серия не подменяет это недостающими цифрами.

Source является крупнейшей semantic частью. Persistence и runtime publication
вместе стоят около 396 мс по средним. Initial lexical preparation — ещё около 175 мс.
Они требуют отдельных измеримых задач наряду с Header compatibility.

Sensitivity при неизменных остальных фазах: total mean минус Header semantic mean
= **1130,513 мс**. Даже бесплатный Header semantic в этой конфигурации не достигнет
1000 мс. Это арифметический сценарий, не прогноз интеграции и не универсальный lower
bound после других архитектурных изменений. 144,63 мс EP01 относится к другой границе
таймера и по-прежнему не подставляется вместо production Header.

## Артефакты и воспроизведение

Команды и driver: `experiments/publish-differential-trace-01/README.md` и run_profile.py.
Свежая сборка и CMakeCache: `build/publish-end-to-end-profile-01/`.
Raw stdout/stderr, measurements.csv и summary.json: подкаталог results/.

Хеши, совпавшие после каждого вызова:

- compiled.bin: `a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d`;
- runtime.bin: `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`.

Production implementation в этом этапе не менялась. Новая архитектура остаётся
изолированным экспериментом; эта серия не подтверждает её ускорение PUBLISH.
