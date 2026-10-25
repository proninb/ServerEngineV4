# HEADER-V2-STATE-LAZY-01

Production V2, прежние lookup/generations/commit и запись сразу в G. Только пять
record-local unordered containers заменены на in-place optional storage.
Пустые reads не создают container; первый write/reserve использует прежний STL.
Созданный container остаётся жив до конца record, включая состояния после erase.
Empty iterators не должны переживать первую мутацию — все V2 callers проверены.

Production всегда использует lazy state. Benchmark/test-only eager control:

```powershell
cmake -S . -B build/header-v2-state-lazy-01 -G "Visual Studio 18 2026" -A x64 -DSERVER_ENGINE_BUILD_BENCHMARKS=ON -DSERVER_ENGINE_ENABLE_IPO=ON -DSERVER_ENGINE_HEADER_STATE_LAZY_AB=ON "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Zi" "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG:FULL /OPT:REF /OPT:ICF /INCREMENTAL:NO"
cmake --build build/header-v2-state-lazy-01 --config Release --target ServerEngineV4PublishBenchmark ServerEngineV4FrontendTests ServerEngineV4HeaderRecordIndexTests -- /m:4
ctest --test-dir build/header-v2-state-lazy-01 -C Release -R "^(header_record_index|frontend_regressions|frontend_regressions_eager)$" --output-on-failure
python experiments/header-v2-state-lazy-01/run.py build/header-v2-state-lazy-01/Release/ServerEngineV4PublishBenchmark.exe build/header-only-publish-01/run-20261023/full/project.json build/header-v2-state-lazy-01/new-results
```

A/B option по умолчанию OFF; он определяет macro только для benchmark и frontend
tests, не для ServerEngineV4. Переменная CW_HEADER_STATE_EAGER читается один раз
до parsing и никогда не меняется workers. Eager control создаёт те же контейнеры
сразу, но через общий adapter: лишние read branches общие для обоих вариантов.
Поэтому дополнительно проведён bridge с оригинальным executable до adapter.

```powershell
python experiments/header-v2-state-lazy-01/run.py build/header-v2-state-lazy-01/Release/ServerEngineV4PublishBenchmark.exe build/header-only-publish-01/run-20261023/full/project.json build/header-v2-state-lazy-01/new-bridge --baseline build/source-cpu-sampling-02/frozen/ServerEngineV4PublishBenchmark.exe --pairs 12
```

В bridge CSV поле eager обозначает original eager executable; его SHA и control
kind записаны в manifest. Default — 20 measured pairs; всегда две warmup pairs.
Не выполнять builds/tests одновременно с native timing. Скрипт замораживает EXE,
сохраняет сырые logs/CSV и сравнивает compiled/runtime SHA с исходными артефактами
того же изолированного fixture после каждого вызова. Требуется fixture внутри build/.
Артефакты должны быть предварительно получены проверенным baseline.

Первая серия: `build/header-v2-state-lazy-01/results-20261023/`.
Bridge: `build/header-v2-state-lazy-01/bridge-20261023/`.
Источники, hashes и paired bootstrap CI — в manifest/summary соответствующей серии.
Простой тест allocator считает реальные STL allocate calls только для пяти test
maps; его bytes не являются full-production allocation audit.

Результаты: `docs/HEADER_V2_STATE_LAZY_01.md`.
