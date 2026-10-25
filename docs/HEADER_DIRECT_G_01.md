# HEADER-DIRECT-G-01 — 23 октября 2026

**Откатан по решению пользователя.** Production сохраняет Header V2. Исходники
adapter и изменения общих экспериментальных файлов убраны из рабочего дерева;
восстановимая копия и patch сохранены в
`build/header-direct-g-01/rollback-20261023/`. Ниже — исторический отчёт эксперимента,
не описание активного pipeline. Measurements и G-only artifact сохранены в build/.

Реализовано формирование **одного конечного production G** после параллельного
Header extraction, temporal Binding и constructor/reference evaluation. Source
Parser не запускается. Production PUBLISH не переключён на эксперимент.

## Что добавлено

- Из локальных результатов формируется transport с field descriptors, declarations,
  WHO mapping и независимыми immutable shapes. Полные Parser requests и operation
  records освобождаются перед writer.
- Coordinator создаёт identities по deterministic WHO, затем воспроизводит ordered
  Graph declaration/request/definition events через существующие production API.
- Сохраняются aliases, record family, field/base access, virtual flags, derived
  types, direct construction values и nested defaults. WHO-only forward declaration
  не создаёт Graph type slot.
- Input/preprocessor-only spellings также переходят в конечную String Table.
  Writer не использует строки, типы или G из oracle.
- После уничтожения input world, transport и worker pool конечный G сравнивается с
  независимым Header V2 и кодируется штатным compiled encoder.

Это adapter на существующих Graph API, а не заполнение приватных columns и не
Local G с последующей пересборкой. Дополнительная валидация production API пока
остаётся частью стоимости writer.

## Проверка UnitProXL

| Конечный domain | Совпало с независимым V2 |
|---|---:|
| Named types | 24 027 |
| Members | 727 868 |
| Bases | 23 915 |
| Derived types | 5899 |
| Strings, весь spelling inventory | 51 048 |
| Nested constructor defaults | 5403 |

Comparator проверяет WHO и parents, порядок и metadata named types, ordered bases
и access, имена/порядок/access всех members, normalized resolved shapes и значения
construction. Nested defaults сравниваются по owner/path/value. Это semantic G
parity, не byte parity: legacy String IDs и физические derived IDs не являются
доказанно идентичными только из этих проверок.

G-only `compiled.bin` проходит штатные bind и `verify_contents()`. Размер образа
**41 802 760 bytes**, SHA-256:
`1e946525bbb481327778038ef67a82cad5c12e3a1a557b8dab0255ed7e9af81d`.
В этом образе намеренно пустые files/provenance; это не production PUBLISH artifact
и не замена compiled.bin рабочего проекта. Runtime publication не выполняется.

## Сквозное время

Первые результаты после появления G оказались хуже прежнего projection-only
измерения. В финальной native серии `build/header-direct-g-01/results-final-20261023/`:
один warmup + восемь measured pairs, одинаковый executable/engine objects,
чередование standalone V2 control/candidate, 16 frontend и 8 evaluator workers.

| Медиана, мс | Новый Header |
|---|---:|
| Подготовка входа | 352,94 |
| Из неё physical preparation | 154,02 |
| Из неё effective stream | 170,61 |
| Semantic model | 133,12 |
| Detach/release requests | 13,04 |
| Final G writer, включая строки | 130,77 |
| Explicit transient release | 10,00 |
| **Полный files → G** | **651,86** |
| **Standalone V2 files → G, парный control** | **385,45** |

Медиана парного ухудшения **266,64 мс**; candidate быстрее в **0/8** пар.
Медианы стадий не складываются в точную медиану total. Total также включает
worker startup/shutdown и destruction оставшегося input world. Oracle,
comparator, сериализация и verify_contents вне таймера в обеих конфигурациях.
Final G остаётся жив на границе timer.

Standalone V2 использует ту же composition/include preparation и token-reuse
admission, но передаёт effective tokens прямо Header V2. Новый pipeline сначала
полностью материализует effective streams, а затем создаёт финальный G. Поэтому
быстрые 132 мс модели не означают быстрого files→G. Разница не доказывает отдельно
цену каждого решения: для выбора следующей оптимизации нужен узкий paired audit.

Этот baseline не равен production Header-only PUBLISH 409,13 мс из предыдущей
серии: там другой executable/preparation и включены SourceMap, persistence и
resident publication. Не вычитать результаты разных границ таймера друг из друга.

## Проверки и ограничения

Self-tests покрывают forward declarations, class access labels, base access,
aliases/namespaces, qualified/array shapes, constructors, reference pending/stale
generations, nested arrays/defaults, repeated definitions, union и polymorphism.
Прямой и обратный extraction schedule сравниваются с независимым V2. Отдельный
lifetime executable включает request counters; timing executable их не включает.
Оба CTest прошли (**2/2**); по 13 positive fixtures с прямым/обратным schedule,
а также отказ на member initializer gap. На полном UnitProXL lifetime executable
также прошёл: перед writer **0 живых Parser requests**, G parity и roundtrip успешны.
Регрессия EP01: model test и все **1080 oracle checks** прошли.

Member initializer gaps явно отклоняются. Callable grammar пока проверяется
независимым oracle перед benchmark; writer сохраняет virtual metadata, но не
является самостоятельной полной заменой production callable validation.
Exact first diagnostics Graph/grammar вне прежней projection не доказаны.
Production String ID encounter order, SourceMap/reuse tape и byte parity остаются
незавершёнными. Полную Header V2 grammar parity и production integration не объявляем.

## Воспроизведение

Сборка, тесты и CLI: `experiments/header-direct-g-01/README.md`.
Полная native серия с hashes и frozen executable:

```powershell
python experiments/header-direct-g-01/run.py build/header-direct-g-01/Release/HeaderDirectG01.exe build/header-only-publish-01/run-20261023/headers/project.json build/header-direct-g-01/new-results
```

Итог: конечный G появился и корректно потребляется штатным compiled encoder.
Ускорения относительно V2 пока нет; подключение к production ради производительности
не обосновано. Следующая работа должна сокращать полный input→G lifecycle,
сохраняя этот результат как контроль корректности.
