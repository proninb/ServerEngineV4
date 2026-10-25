# PARALLEL-LOCAL-SOURCE-ARCH-01 — contract/audit slice

Целевой новый alias-aware LWW contract: `docs/SOURCE_FINAL_CELL_CONTRACT_01.md`.
Аудит реализации и недостающих границ: `docs/PARALLEL_LOCAL_SOURCE_ARCH_01.md`.

```powershell
python experiments/parallel-local-source-arch-01/verify_contract.py build/parallel-local-source-arch-01
```

Скрипт проверяет scalar reduction по **уже заданным** final targets, independent worker-completion permutations и origins. Отдельный nested-project fixture запускает исходный production PUBLISH и показывает расхождение file-ID processing order с declaration-order DFS.

Реализации parallel Source parser, global linker и нового Runtime здесь нет. Это contract check и code audit; performance этим скриптом не измеряется. Production-файлы не изменены.
