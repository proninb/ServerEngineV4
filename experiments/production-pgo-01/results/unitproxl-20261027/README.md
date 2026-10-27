# Production PGO UnitProXL — 2026-10-27

Portable evidence from the completed unsandboxed real-server series: 15 alternating ABBA/BAAB blocks, 60 measured requests, one excluded warmup per arm. Summary and full measured samples are retained, with lifecycle/native SHM validation, CMake configurations and the local bundle completion/hash manifest.

Source baseline: 87abe00 plus the frozen local patch described in docs/PRODUCTION_PGO_01.md. completion-status paths identify the original local bundle; they are not files included in this results folder. Executables, PGC/PGD, licenses, source corpus and complete input manifests remain local under build/production-pgo-current-02 and are not published here. This subset is analysis evidence, not a self-contained release bundle and cannot pass the exact-image release verifier alone.

Read docs/PRODUCTION_PGO_01.md and docs/PRODUCTION_PGO_RELEASE.md for scope, provenance, limitations and release gates. No deployment was performed.

CMake cache copies normalize trailing empty lines; original-file hashes in the completion manifest refer to the unchanged local bundle files.
