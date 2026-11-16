# Индексирование ссылки на массив в Source

Исправлены Source Parser V2, его прямой model-путь, read-only resolver и старый
Source-парсер: перед `[]` раскрывается ссылка на массив и добавляется явный
шаг `dereference` в путь к данным.

```cpp
struct Leaf { int value; };
struct A {
    Leaf data[2][3];
    Leaf (&view)[2][3];
    A() : view(data) {}
};
// Source:
A a;
a.view[1][2].value = 7;
```

Ссылка не превращается в копию массива. Запись попадает в `data`, а ссылки на
элементы сохраняют правильный адрес после PUBLISH и LOAD. Поддержаны
многомерные массивы и унаследованные поля. Проверки границ сохраняются;
индексирование произвольного указателя этим изменением не разрешается.

Frontend regressions и compiled_project tests проходят. В TCP-проверке с
двумя объектами, массивом 2x3 и ненулевым смещением базового класса значения
ссылок равны 11 и 7 после PUBLISH, LOAD и LOAD в новом процессе.
Результат: `build/unitprotr-array-reference-tcp/summary.json`.

UnitProTr проходит `ANA_CH_IN[0]` и адресные присваивания `TRANSFER.From/To`.
Актуальная следующая остановка описана в `SOURCE_POINTER_ADDRESSES.md`.
