# SEMANTIC-BIND-04: Final Bound Representation

Изолированный эксперимент с той же проверяемой проекцией Header, что SB01–03.
Production PUBLISH не подключён к этому коду.

`parser.hpp` сохраняет грамматику SB01, но при выдаче request переносит его
модификаторы в корневую arena. Временные `type_expr` и declarator builders пока
используют vector; устранение всех временных allocations здесь не заявляется.
После завершения Parser arena заморожена. Binder и результат используют один
payload без копирования всех форм после Binding.

`shape_range` содержит указатель на стабильный объект arena и два 32-битных
индекса. Рост vector внутри arena не инвалидирует range. Результат владеет
только непустыми arenas, массивом resolved leaves, ranges и destinations.
Ни один владелец request storage в результат не попадает. Индексированное
чтение формы не ищет сегмент requests.

Destination хранит owner, definition, физическое происхождение имени и base
flag. Owner и разрешённый named type пока находятся в пространстве индексов
эксперимента; для WHO остаются отдельные numbering maps. Это ещё не сериализуемая
запись G: имена, String IDs и полная Header-семантика сюда не добавлены.

## Сборка

Требуются ранее собранные Release engine objects SB03. Их путь задаётся
`ENGINE_OBJECT_DIR`; каждый измерительный запуск сохраняет их SHA256.

```powershell
cmake --build build/semantic-bind-03 --config Release --target SemanticBind03Engine --parallel 8
cmake -S experiments/semantic-bind-04 -B build/semantic-bind-04 -G "Visual Studio 18 2026" -A x64
cmake --build build/semantic-bind-04 --config Release --target SemanticBind04 SemanticBind04Legacy SemanticBind04Allocations -- /p:BuildProjectReferences=false
ctest --test-dir build/semantic-bind-04 -C Release --output-on-failure
```

Legacy использует неизменённые модели SB03. Его `baseline` — owning result,
`combined` — sparse + segmented + shared request owner. SB04 поддерживает
`combined` с отдельной shape arena. Они используют общий стенд и checksum.

```powershell
python experiments/semantic-bind-04/run_matrix.py build/semantic-bind-04 build/publish-subsecond-01/fixture/project.json build/semantic-bind-04/results-lifecycle 5
```

Для повторного запуска нужен новый output directory. Тайминги следует снимать
native, последовательно и без одновременно работающих сборок или тестов.

## Контракт измерений

- Сначала отдельный validation run сравнивается с независимым Header V2 oracle.
- Затем непрерывный lifecycle: Parser → combine → canonicalization → Binding →
  numbering → освобождение batch → первое последовательное чтение → уничтожение
  результата и numbering maps. Oracle, подсчёт памяти и подготовка shuffle order
  находятся снаружи. Вход уже подготовлен; preprocessing не входит в этот таймер.
- Отдельный run проверяет result-only чтение после уничтожения входных tokens
  и world: warmup и пять чередующихся sequential/shuffled проходов.
- `primary_bytes` сохраняет методику основных массивов SB03; это не peak RSS.
  `result_only_capacity_bytes` включает bound records, diagnostics, shape
  payload и сохраняемые shape descriptors, но исключает numbering maps,
  STL allocator metadata и shared_ptr control blocks.
- Allocation executable отдельно проверяет живые request instances и requested
  allocation bytes. Его время не используется. Input и pool startup не входят
  в его tracked model peak. PMR canonicalizer тоже учитывается.

Проверяются порядок событий/видимость/полнота типов, modifiers, точное первое
место ошибки, независимость от workers, сохранность destinations, move/copy,
рост и заморозка arena, освобождение requests до потребителя и arenas после
последнего потребителя. Constructor/reference binding, Direct G и chunking
одного root остаются отдельными экспериментами.
