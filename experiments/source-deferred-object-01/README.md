# SOURCE-DEFERRED-OBJECT-01

Архитектурный аудит: `docs/SOURCE_DEFERRED_OBJECT_01.md`.

`verify_alias_order.py` запускает три маленьких PUBLISH на исходном production executable и декодирует выпущенные Runtime plans. Проверяет, что два логически разных endpoints через static reference пишут в один адрес; перестановка их записей меняет конечное значение. Третий fixture показывает сохранение первого initialization slot при повторной записи.

```powershell
python experiments/source-deferred-object-01/verify_alias_order.py build/source-deferred-object-01/alias-order
```

Это ограниченная plan-level проверка одного direct link и двух scalar records. Она не читает resident SHM, не реализует общий Runtime oracle и не измеряет производительность. Реализации deferred builder или изменений production здесь нет.
