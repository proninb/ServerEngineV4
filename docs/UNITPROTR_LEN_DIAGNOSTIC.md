# Причина отказа подготовки runtime

Обычный лог сообщает `Cannot prepare compiled.bin direct-encoding layout`.
Это общее сообщение скрывает конкретный этап. Отдельная диагностическая сборка
показала:

```text
layout=0
type_plan=0
runtime_plan=1
UNITPROTR_INIT_FAILURE index=790603 object=FWM__FWM3_DIO32 target_type=1073741829 kind=2 bits=500 result=1
```

Значение 0 означает success, 1 — invalid_input. `target_type=1073741829`
соответствует intrinsic unsigned_char; kind=2 — unsigned_integer.

Источник: `Sources/FWM/FWM3_SYS.page:26`:

```cpp
FWM__FWM3_DIO32.DIO_Data.LEN = 500;
```

`FWM_sfwDFB_DIO32_Sync_f01::DIO_Data` имеет тип `U_ANY_ARRAY_INT&`.
В исходном SDK и подготовленной копии `U_ANY_ARRAY_INT::LEN` объявлен `byte`,
а byte — unsigned char: диапазон 0..255. Runtime-построитель отклоняет 500
при кодировании значения. В Source встречаются также длины 2400, поэтому
проблема не ограничивается одним объектом.

Исправление должно согласовать тип длины с поддерживаемыми размерами массивов:
например, использовать 32-битное целое для LEN у соответствующих generic
ARRAY-типов, затем регенерировать экспорт и пересобрать использующие эти типы
нативные задачи. Изменение поля затрагивает ABI; обрезать длину до byte нельзя.

Полные результаты: `build/unitprotr-diagnostic-check/stderr.log` и
`build/unitprotr-runtime-diagnostic-check/stderr.log`. Диагностические изменения
исходного кода сняты; отдельные бинарные сборки и снимки сохранены в build.
Основной консольный сервер продолжает работать в состоянии UNLOADED.

## Адаптация подготовленной копии

В `examples/UnitProTr/Types/unity_pro_xl_base.h` поле LEN у четырёх типов
U_ANY_ARRAY_INT/BOOL/BYTE/DDT заменено на unsigned int (32 бита для выбранного
ABI). U_STRING::LEN не изменён. Исходный внешний SDK остаётся без изменений.
Адаптация выполняется `widen_array_lengths` в скрипте переноса; итоговый SHA-256
записан в `Types/annotation-removal.json`.

Отдельные PUBLISH, LOAD и LOAD новым процессом подтверждают значения 500, 2400,
4100, 65536 для четырёх типов и 1 для U_STRING::LEN.
Результат: `build/unitprotr-wide-len-check/summary.json`.
Полная проверка после адаптации выполняется отдельно. Нативные задачи,
использующие изменённые определения, требуют согласованной пересборки.
