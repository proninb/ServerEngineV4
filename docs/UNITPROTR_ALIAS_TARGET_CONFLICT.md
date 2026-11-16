# UnitProTr: конфликт назначения через ссылки

## Обновление 15 ноября 2026: устранено

Исправленный оригинальный экспорт перенесён в examples/UnitProTr:
36 035 изменённых Source-файлов, 167 исходных заголовков плюс SDK umbrella.
Для выходных соединений добавлены промежуточные U_VARIABLE_BOOL.
Исходные LEN теперь объявлены int; скрипт сохраняет это определение.

Новые compiled.bin/runtime.bin созданы. Загрузка полного проекта новым
процессом и повторный UNLOAD/LOAD проходят: STATE LOADED.
Проверены DIO_Data.LEN=500, оба пути Active.VAL=0 и границы GEST.
Результаты: examples/UnitProTr/validation.json.
Сервер оставлен с build/unitprotr-fixed-20261115-run/load.json.
Ниже сохранена диагностика предыдущего экспорта.

Проверка 14 ноября 2026. Разбор проекта и сохранение compiled.bin/runtime.bin
проходят после расширения LEN в локальных типах массивов.

В движке исправлен порядок построения runtime. Прямые назначения размечаются
до конструирования объектов; назначения через разыменование — после него,
когда существуют значения ссылок из конструкторов. Ожидающие статические
связи разрешаются при проходе по такому пути.

Проверки исправления:

- ServerEngineV4Tests: compiled_project tests passed.
- build/unitprotr-constructor-alias-check/summary.json: TCP PUBLISH, LOAD и
  LOAD новым процессом. Привязка target.alias.input достигает target.leaf.input
  и заменяет конструкторную привязку к target.leaf.value; значение source=73.

## Текущий блокирующий конфликт

В examples/UnitProTr/Sources/FWM/FWM1_FWF.page:95:

```cpp
FWM__FWM1_FWF_PMP.Data=FWM__FWM1_FWF.PMP_Data;
```

Поэтому следующие назначения адресуют одно поле Active:

```cpp
// Sources/FWM/FWM1_FWF/FWM__FWM1_FWF_PMP.page:944
FWM__FWM1_FWF_PMP.Data.Active = FWM__FWM1_FWF_PMP___Data__3.OUT;

// Sources/FWM/FWM1_FWF/FWM__FWM1_FWF.page:81
FWM__FWM1_FWF.PMP_Data.Active = FWM__FWM1_FWF_Logic__12.OUT;
```

Оба источника различны. Диагностика сохранённого runtime подтверждает
совпадение физического назначения: смещение 2369960, объект
FWM__FWM1_FWF + 88. Индексы связей (с нуля): 28868 и 29859.

Автоматически выбирать один источник нельзя без изменения смысла модели.
Нужно исправить соединения в исходном проекте: оставить правильный источник
или разделить поля назначения, затем повторить экспорт.

Команда воспроизведения с сохранёнными диагностическими файлами:

```powershell
build/unitprotr-resolved-bin/ServerEngineV4.exe build/unitprotr-resident-run/load.json
```

Результат в build/unitprotr-resident-run/load-resolved.log:

```text
detail: link resolution: Multiple static links bind the same physical reference field through different aliases
```

Диагностический проект и файлы находятся в build/unitprotr-resident-model.
Внешний исходный проект не изменён. Полный LOAD пока не проходит.
