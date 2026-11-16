# Проверка и устранение ошибки GEST

Ошибка находилась в устаревшей подготовленной копии экспорта. В текущем
исходном проекте `C:/Boris/CW/UnitProTr/DATA/Project/Project` прямой привязки
`GEST[4] = DataExch_GEST[5]` уже нет.

Генератор проверяет совпадение размеров и нижних границ в
`UnityProXL.Translator2/CodeDom/CodeDomManager.cs` (`ValidateDirectReference`).
При отличающихся границах `TransferInSimple` создаёт блок копирования.
Для этого случая актуальная страница содержит:

```cpp
U_ARRAY_COPY_INT FWM__FWM_SYS_MNGRA_PLC___NocVer__2_GEST;
// SRC = manager.DataExch_GEST; DEST = exchange.GEST;
// SRC_START_INDEX = 1; SRC_END_INDEX = 4;
// DEST_START_INDEX = 0; DEST_END_INDEX = 3;
```

Код выполнения `Tasks/UnityProXL/BBs/ARRAY_COPY_INT.cpp` добавляет начальные
индексы к SRC/DEST и копирует элементы. Это копирование `[1..4] -> [0..3]`,
а не привязка ссылки к массиву другого типа и не предположение об адресном
смещении. Не требуется ослаблять проверку типов массивов в ServerEngineV4.

Копия `examples/UnitProTr` заново подготовлена из текущих Types и Books;
HIDDEN удалён только из копий, исходные и итоговые SHA-256 записаны в отчёты.
При следующей регенерации внешнего экспорта следует повторить
`scripts/prepare_unitprotr_project.py`.

Проверки:

- Локальный тест ARRAY_COPY_INT проходит: диапазоны 1..4 -> 0..3, обратное
  направление, перекрытие, разные длины, null и ошибочные диапазоны.
- PUBLISH и LOAD модели с настоящими заголовками UnitProTr,
  FWM_devDFB_MNGRn_PLC_HSBY, U_DATA_EXCH и U_ARRAY_COPY_INT проходят.
  Из загруженной модели прочитаны диапазоны 1, 4, 0, 3.
  Результат: `build/unitprotr-gest-resolved/summary.json`.
- Полный PUBLISH проходит прежнее место GEST. Последующая ошибка строкового
  экспорта устранена синхронизацией 2 809 изменённых Source-файлов с оригиналом.
  Повторное имя объекта в MCUP01 также исправлено в экспорте и перенесено
  синхронизацией 34 749 Source-файлов. Полный Source-разбор проходит.
  Текущая остановка — внутренняя подготовка compiled.bin/SHM, см. `validation.json`.

Эти проверки подтверждают перенос конфигурации блока. Исполнение всех
PLC-задач в ServerEngineV4 ими не подтверждается.
