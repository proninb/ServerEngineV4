# TCP-PGO-RELEASE-02

Frozen TCP-RELIABILITY-01 sources, a new ordinary executable, fresh production PGC/PGD/USE, real TCP A/B, post-training holdouts and a separately selected 600-second preliminary soak. No Parser/G/Runtime optimization or deployment is performed. Old PGO reference remains immutable.

Source snapshots include local uncommitted TCP changes and the inactive pre-existing resolver audit guard. `source/` plus freeze.json are authoritative; git HEAD and source.patch alone do not include every untracked header/test file. Do not silently reuse a profile after source/toolchain changes.

## Local reproduction

A new output directory is required. Local original UnitProXL and the established small fixtures, license/lease and matching MSVC installation are prerequisites; no corpus or credentials are distributed here.

```powershell
python experiments/production-pgo-01/freeze.py build/tcp-pgo-release-new
cmake -S build/tcp-pgo-release-new/source -B build/tcp-pgo-release-new/compiler -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTING=ON -DSERVER_ENGINE_ENABLE_IPO=ON -DSERVER_ENGINE_PRODUCTION_PGO=OFF '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /MP8'
cmake --build build/tcp-pgo-release-new/compiler --config Release --target ServerEngineV4 ServerEngineV4CommunicationRegressionTests ServerEngineV4CommunicationConnectionCoreTests ServerEngineV4CommunicationControlTests ServerEngineV4CommunicationProtocolTests ServerEngineV4ServerPolicyTests ServerEngineV4ClientSessionTests --parallel 16
python experiments/tcp-pgo-release-02/build_and_measure.py build/tcp-pgo-release-new
python experiments/tcp-pgo-release-02/holdout.py build/tcp-pgo-release-new
python experiments/tcp-pgo-release-02/soak.py build/tcp-pgo-release-new --seconds 600
python experiments/tcp-pgo-release-02/summarize.py build/tcp-pgo-release-new
```

Use ordinary unsandboxed execution for server training/testing. Stop if antivirus blocks an image; the scripts do not restore, exclude, rename around, or alter protection. A failed driver exits and preserves status/logs; it does not stitch partial measured series together. The MSVC tools path in the driver identifies the toolchain actually used.

## Declared gates

- Main UnitProXL: 15 alternating ABBA/BAAB blocks, 60 measured calls plus warmups; block-paired bootstrap, no source/phase instrumentation. Artifact and native SHM checks outside request timer.
- Holdout: three new synthetic fixtures (strings, inheritance, reference links), created only after fresh training is complete and excluded from it. Same-path OFF/USE artifact/LOAD/native parity; 15 ABBA/BAAB blocks per case in two persistent servers. A negative 95% interval plus >5% slowdown is a material regression gate. This separate warm-persistent protocol is not the main fresh-process protocol.
- Soak: one USE process; ten warmup cycles, then at least 600 seconds of PUBLISH/RUN/FREEZE/UNLOAD/LOAD and reconnect/duplicate LOGIN/partial frame reset/oversized frame cases. Oracle checks before RUN and after LOAD. Memory is sampled after UNLOAD/reconnect GET_STATE, not at arbitrary live peaks.
- Predeclared coarse memory drift limits: last-five minus first-five median quiescent private bytes <=128 MiB and handles <=16, separately for each project. Failure is retained even if all protocol/semantic checks pass. These thresholds do not establish absence of leaks; a failure also does not distinguish allocator retention from a leak.

`soak.py` exits nonzero when the memory gate fails and still preserves the full outcome/status and JSONL. `summarize.py` records separate gates and does not grant a preliminary release candidate when any gate fails. A 10-minute user-selected preliminary run is not long-duration production certification.

`results/20261027` contains portable measurements/status only. Local bundle executables, source copy, PGD/PGC, complete corpus and licenses remain under build/tcp-pgo-release-02. Evidence hashes refer to those original local files. No automatic deployment or default USE switch.
