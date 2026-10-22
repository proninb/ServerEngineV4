# PUBLISH-DIFFERENTIAL-TRACE-01 / COVERAGE-CONTRACT-01

Инструментирование и differential contract suite реализованы изолированно.
Новый evaluator не интегрирован в production. Исходники production не менялись
для trace: генератор создаёт копии шести translation units в build-каталоге.
Остальные engine objects взяты из SB03 Release; provenance oracle использует
точную реализацию production Header semantic reuse cache, извлечённую из parser.cpp.

## Результат проверки контрактов

36 fixtures × 5 evaluator counts (1/2/4/8/16) × 2 chunk targets (1/32768):
360 запусков. Внутри каждого проверены forward/reverse extraction и три порядка
локальных задач. Production trace последовательности одинаковы при всех configs.
CTest contract suite прошёл; известные несовместимости фиксируются как данные,
а не скрываются за успешным exit code. Контрольные positive/negative cases обязаны
иметь совпадение и проверяются assertions.

| Результат | Fixtures | Запусков |
| --- | ---: | ---: |
| Совпала существующая type/value projection | 8 | 80 |
| Совпала точная первая diagnostic | 1 | 10 |
| Partial projection: пропущены callables/initializers | 9 | 90 |
| Различается acceptance | 14 | 140 |
| Оба отвергают, но diagnostic не совпала точно | 4 | 40 |

Совпадение projection не доказывает metadata parity: access и polymorphism в её
формате отсутствуют. Сравнение negative требует kind/detail/file/offset/length;
общая ошибка без точного сообщения не считается parity.

Подтверждены execution, а не только статическим анализом:

- Projection принимает duplicate members, duplicate direct bases, наследование union,
  `unsigned float`, `typedef void`, multiple data declarators и method body, которые
  Header V2 отвергает.
- Projection пока не принимает поддерживаемые V2 conversion/named operators.
- Неверный member initializer может быть пропущен с gap вместо production rejection.
- Повторная definition с другим access или virtual metadata не проверяется как в V2.
- Virtual/incomplete base и некоторые declarators имеют несовпадение diagnostic.

Полные inputs и классификация каждого case находятся в coverage-summary.json и
coverage.csv. Это диагностическая база для последующих coverage slices; набор
fixtures не является исчерпывающей спецификацией grammar.

## Подтверждённый порядок String IDs

Два последовательных Header roots:

```cpp
// root 0
struct B { int x; };
struct A { B b; A(){b.x=2;} };
// root 1
struct Later { int after; };
```

| Строка | Production string_id | Input preparation snapshot |
| --- | ---: | ---: |
| A | 1 | 1 |
| B | 2 | 2 |
| b | 3 | 3 |
| x | 4 | 4 |
| b.x | 5 | отсутствует |
| after | 6 | 5 |
| Later | 7 | 6 |

В production nested path интернируется до строки следующего physical file/root.
Вход новой модели уже подготовлен целиком, и `after` получил ID 5. Это не
«неправильный final projection ID»: projection пока вообще не назначает string_id.
Это доказательство, что использование prepared table напрямую изменит legacy IDs.
Внутри одного physical file identifiers могут интернироваться заранее; нельзя
выводить порядок intern только из позиции declaration в effective tokens.

## Что именно записывается

Глобальный observation sequence общий для CSV streams, root — actual file_id.
Intern/derive/resolve записывают вызовы и результат, включая повторные hits;
их нельзя считать потоком только новых creation events. `derive` фиксирует
child/kind/payload/returned type_ref. Semantic trace содержит Parser entry points,
identity resolutions, diagnostics и post-pass snapshots member/base access и flags.
Projection экспортирует declarations, requests, physical request origins, operations
и numbered WHO. Детальная схема колонок — в README эксперимента.

SourceMap contribution/dependency calls и вход/выход reuse callbacks записываются
с actual root/file. Реальный reuse handler работает в прежнем порядке, включая
include/once replay. Для observer control весь Header oracle повторяется с tracing
disabled в том же executable. Сравниваются String IDs, WHO и Header G domain bytes,
первое сообщение и SourceMap/dependency/presence/index domains. Это проверка влияния
записи trace на результат, не измерение её временной стоимости и не независимое
доказательство полной production integration.

Проекция явно пишет `not_emitted` для String ID numbering, graph-local derived IDs
и production SourceMap/reuse: этих механизмов ещё нет. Хронологический comparator
не объявляет такие streams равными. Parser entries и extracted operations имеют
разную гранулярность; нормализация всех событий в общий contract остаётся работой
следующих slices. Seed/baseline append-state и все варианты preprocessing также
ещё не покрыты исчерпывающе.

## UnitProXL

На реальном effective input при 16 frontend / 8 evaluator workers, target32768:

- Production Header V2 accepted; projection accepted.
- Существующая type/value projection совпала, constructor gaps = 0.
- **5316 excluded callables**, excluded declaration initializers = 0.
- Классификация — **partial_projection**, а не full Header parity.
- Traced/untraced observer checks и проверки scheduling прошли.

Особенно важно, что нулевые gaps не обнаруживали пропущенные callable contracts.
Они теперь представлены отдельным явным счётчиком и не трактуются как coverage.

## Артефакты

Код: `experiments/publish-differential-trace-01/`. Сборка и воспроизведение — README.
`build/publish-differential-trace-01/coverage/` содержит:

- string-id-events.csv;
- type-id-events.csv;
- semantic-events.csv;
- provenance-events.csv;
- diagnostics.csv;
- coverage-summary.json и coverage.csv.

Агрегированные CSV используют w8/target32768 для каждого fixture; подкаталог traces
сохраняет все 360 configurations. UnitProXL trace находится отдельно в unitproxl;
файлы большие и не включаются в Git. Generated source-manifest связывает копии с
исходниками; frozen manifest/executables находятся в build-каталоге.

Следующий функциональный шаг — отдельный slice member initializers/diagnostic
contract. Callables/access/base/polymorphism требуют собственных slices. Direct G
adapter по-прежнему должен следовать после согласования этих контрактов и порядков.
