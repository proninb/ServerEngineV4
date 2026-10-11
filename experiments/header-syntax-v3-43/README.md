# V3-43 — V3-42 codegen / lifetime / inclusion-order audit

**This is a verification package, not a new optimization.** V3-38A and V3-42 producers are frozen byte-for-byte. The full Header Graph A/B uses unchanged V3-42 code, and a separate executable reverses the order in which producer implementations appear in the translation unit. Each benchmark checks WHO/WHERE and construction parity against the independent Header V2 oracle.

- `V343LifetimeTests`: poison-byte default construction and 0/1/2/31/32/33 push cases; active slots are only read after `push()`.
- `V343CodegenRuntimeTests`: escaped-object probe, independently compiled observer, 1024 input cases; confirms identical observable semantics.
- `test_v343_access_audit.py`: enumerates indexed-layer read sites; a *drift guard*, not formal proof.
- `v343-codegen.md`: if `cl.exe` is visible in Developer PowerShell, compiles isolated probe with `/O2 /GL- /FAcs` and preserves excerpts for manual assembly review. **Probe assembly alone does not prove production machine code.** If `cl` is unavailable, report explicitly says SKIPPED; `-RequireAssembly` makes it a hard failure.
- `ServerEngineV4HeaderSyntaxV343Perf` = frozen original order; `ServerEngineV4HeaderSyntaxV343ReversePerf` = reversed declaration order, independently executed and parity checked. No new semantic variant.
- Reports default to `<RepoRoot>/build/benchmarks/v343-*`, existing files are never overwritten.
- Production git tree is never modified. Full PUBLISH integration is still `PARTIAL`.
