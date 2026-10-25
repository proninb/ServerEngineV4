# SOURCE-FINAL-CELL-CONTRACT-01

Дата: 2026-10-24. Основание — принятое пользователем правило из приложения к задаче: повторная явная инициализация одной конечной runtime-ячейки, в том числе через разные aliases, выдаёт warning; побеждает последняя запись по порядку Source в дереве проекта и позиции внутри файла.

**Это целевой контракт новой архитектуры, не описание уже внедрённого V2.** Production в данной задаче не изменён. Ранее измеренные реализации и golden artifacts остаются историческими контрольными точками.

## Независимые величины

* `file_id` — существующая идентичность физического файла и ключ к его content/origin. Его не перенумеровывать ради scheduler.
* `source_order` — временный rank Source task в текущем полном PUBLISH.
* `local_order` — временный порядковый номер semantic operation внутри Source.
* `identity_ref` — WHO сущности; worker-local symbol index не выдавать за глобальный `identity_ref`.
* Graph/SHM WHERE — физическое размещение, не приоритет записи.

Scheduler передаёт `(file_id, source_order)`, а operation несёт `(source_order, local_order)` и physical origin. Только эта логическая пара задаёт приоритет. Worker ID, время завершения, числовой file_id и номер выделенного Graph slot не участвуют в last-write-wins.

Порядок дерева фиксируется как declaration-order DFS: поддерево вложенного Project обходится в месте его появления в `project` array. Source tasks получают последовательные ranks после фильтрации остальных kinds. Если UI предоставляет иной установленный tree traversal, planner должен получать именно его явную последовательность; он не должен подменять её сортировкой file IDs. Повторное включение одного Source сохраняет нынешнюю проверку duplicate input.

Постоянное поле `order` в file model не требуется. Вместе с тем информация о выборе winner должна сохраняться достаточно долго: временность scheduler rank не означает, что LOAD/BUILD могут потерять данные, необходимые для восстановления семантики. Способ persistence winners/provenance — отдельное проектное решение.

## Разрешение конечных значений

1. Сохранить текущие проверки грамматики, типов, existence/visibility и допустимости operations. Новое alias-aware LWW само по себе не разрешает undeclared forward references.
2. Выбрать окончательные bindings согласно логическому Source order. Для повторного BIND одного логического reference текущий принятый last-binding-wins сохраняется; это final-state linking, не исполнение промежуточных assignments как C++-программы.
3. Разрешить alias paths относительно **окончательной** модели references. Проверить циклы/недоступные targets и зависимости nested reference targets.
4. Установить конечную runtime-ячейку каждой инициализации. Можно использовать символический canonical storage target либо физическое размещение, когда оно известно; абсолютный адрес конкретного SHM mapping не является постоянной semantic identity.
5. Для каждой конечной ячейки выбрать явную Source-запись с максимальным `(source_order, local_order)`. Для остальных сформировать deterministic duplicate diagnostics, сохранив origins.
6. Материализовать победившие независимые ячейки в любом порядке. Перед параллельным memcpy должны быть доказаны disjoint storage ranges. Совпадения по начальному offset недостаточно, если разрешены частичные перекрытия или разные widths.

Worker completion не может выбирать winner. Operation, которую позже переопределили, всё равно проходит semantic validation: поздняя корректная запись не отменяет раннюю ошибку.

## Пример, меняющий поведение относительно V2

```cpp
a.ref = b;
a.ref.x = 1; // event 1
b.x = 2;     // event 2
a.ref.x = 3; // event 3
```

После linking все три explicit writes относятся к `b.x`. Новый результат: **3**. Duplicate diagnostics: `(event 1 → event 2)` и `(event 2 → event 3)`, каждая с обеими исходными позициями. Все три source occurrences остаются доступными через эти origins; последняя запись в G не заменяет диагностику.

Текущий V2 сохраняет первый slot каждого логического endpoint и последнее значение для этого endpoint. Его итоговый Runtime list — `[a.ref.x=3, b.x=2]`, результат **2**. Поэтому старый runtime golden для этого fixture не может одновременно быть oracle нового контракта. Это намеренное semantic изменение, а не разрешение произвольной перестановки старого initialization list.

## Диагностика

Для n explicit writes одной конечной ячейки — n−1 duplicate transitions в логическом порядке. Warning указывает прежний и новый origin, желательно обе логические формы endpoint и canonical target. Одна операция над строкой/массивом может покрывать несколько ячеек: формат агрегации warnings должен оставаться детерминированным и не терять origins; отдельная per-byte лавина сообщений не следует автоматически из scalar-контракта.

Не смешивать implicit Header/member/constructor defaults с очередью повторных Source writes: defaults дают исходное состояние, explicit Source задаёт override. Явный initializer Source declaration также является явным источником значения и должен быть учтён при проектировании event stream; текущая отдельная cold object-construction cell не заменяет его origin/event.

Детерминированная выдача сообщений может потребовать сортировки только compact diagnostics по event order. Отсутствие сортировки ready results по file rank не означает запрета всех сортировок внутри final-target reduction.

## Совместимость

Для нового алгоритма физические handles/байты полного PUBLISH могут отличаться. Требуются корректные WHO, типы, финальные связи/значения, origins, LOAD, Runtime и детерминизм нового алгоритма. Normal BUILD сохраняет существующую stable-WHERE lineage нового baseline.

Отказ от old byte parity не разрешает stale SourceMap или расхождение semantic G/query и Runtime. Если победитель выбран только при подготовке `runtime.bin`, нужно определить, что видят `compiled.bin`, query, source-save и следующий BUILD.

При удалении последнего writer incremental BUILD должен суметь восстановить предыдущего допустимого writer, в том числе через alias и из другого Source. Также reorder Project tree или изменение binding способно менять winners без изменения текста остальных writers. Дизайн provenance/revalidation обязан покрывать эти случаи, прежде чем новый путь станет production.

Текущий `parser_warning` имеет один file/range/detail; он не содержит вторую структурированную source location. Текущий G initialization record не хранит event. Эти недостающие данные нельзя получить из существующих final arrays без дополнительного ввода.

## Ограниченная исполняемая проверка

`experiments/parallel-local-source-arch-01/verify_contract.py` фиксирует scalar reduction contract на уже заданных final targets: 6 permutations результата worker completion, независимость от численных file IDs, local order, warning origin pairs и изменение final referent. Это **не parallel Source parser, не alias linker и не production implementation**.

Тот же скрипт запускает текущий production PUBLISH на вложенном Project. При declaration-order DFS ожидается winner из `tail.cpp` со значением 2, но текущий V2 выдаёт 1 из nested child, поскольку его file_id назначен позднее. Декодированный Runtime-план подтверждает необходимость отделить tree rank от file identity.

Команда: `python experiments/parallel-local-source-arch-01/verify_contract.py build/parallel-local-source-arch-01`. Результаты: `summary.json`, nested project fixture, stdout/stderr. Performance/scaling этой проверкой не измеряются.
