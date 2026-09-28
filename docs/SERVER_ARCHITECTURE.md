# Server Architecture V4

## Physical source tree

Visual Studio must mirror the physical file-system hierarchy exactly.

Current Project construction subtrees include:

```text
server_engine/
├── project/
│   ├── assign/
│   ├── construction/
│   ├── file/
│   ├── frontend/
│   ├── graph/
│   ├── preprocessor/
│   ├── persistence/
│   ├── runtime/
│   ├── semantic/
│   ├── string/
│   ├── project.*
│   ├── project_build.*
│   ├── project_load.*
│   ├── project_rebuild.*
│   ├── project_configuration_*
│   ├── project_identity.hpp
│   ├── project_lifecycle_context.hpp
│   ├── project_path.*
│   └── preprocessor_configuration.hpp
└── docs/
    ├── SERVER_ARCHITECTURE.md
    ├── SERVER_CONFIGURATION.md
    ├── PROJECT.md
    ├── PROJECT_CONFIGURATION.md
    ├── FRONTEND_ARCHITECTURE.md
    ├── DIAGNOSTICS.md
    └── JSON.md
```

As new Project subtrees are introduced, the physical directory is the
architecture; IDE filters mirror it rather than inventing logical-only groups.

## Visual Studio rule

`.vcxproj.filters` must reproduce the physical source tree exactly.

No artificial `Source Files`, `Header Files`, `Server`, or other logical-only
groups are used.

Current Project filters must include the physical:

```text
project
├── assign
├── construction
├── file
├── frontend
├── persistence
├── preprocessor
├── runtime
├── semantic
└── string
```

The physical tree is authoritative.

## Server ownership

```text
main
 |
 +-- server
      |
      +-- server_context
           |
           +-- server_configuration
           |    `-- one process-wide ABI + Runtime/SHM policy
           +-- authentication
           +-- request_queue
           +-- communication
           |    |
           |    +-- console endpoint, only when configured
           |
           +-- project
```

## Server ABI and SHM contract

One Server instance owns one ABI and one SHM layout contract.

```text
server.json
    -> settings.abi.target
    -> settings.abi.pack
    -> settings.shm.mode
    -> settings.shm.fixed_base_address
         required only for fixed_direct

one Server
    -> one ABI
    -> one Runtime/SHM materialization policy
    -> one active Project Runtime/SHM
```

ABI and SHM policy are process configuration. `project.json` does not contain or
override either contract.

The supported Runtime/SHM modes are:

```text
FIXED_DIRECT
    SHM == native Runtime storage
    native references point directly inside the fixed mapping
    no Runtime/SHM transfer

RELOCATABLE_TRANSFER
    SHM may map at an arbitrary address
    native Task Runtime storage is separate
    values transfer between SHM and native Runtime
    native execution still uses direct C++ references
```

SHM size is derived from final G + ABI and is not configured independently.

Each mode-specific operation borrows the same process-wide
`server_settings_configuration`. The explicit root Project path is an operation
input; it is not resident Project state and is not stored in `server_context`.

```text
LOAD
    root Project path
    load_context
        Server settings

BUILD
    root Project path
    build_context
        Server settings
        persisted BUILD-state views
        graph_delta sparse semantic changes
        temporary BUILD reuse/change state

PUBLISH / REBUILD
    root Project path
    full_construction_context
        Server settings
        fresh dense G construction state
```

There is no universal `builder_context`.

## Configuration directory

The directory name is `configuration`, not `config`.

It contains only Server process configuration types and loading/parsing logic.

## Server License

`server.license` is required process-level Server state. It is independent from
Project files and from Authentication.

```text
load/validate server.json
    -> load/validate server.license
    -> start Authentication
    -> start Communication
    -> optional Project startup
```

V1 contains `version`, `expires_at`, and `max_connections`.

`expires_at` uses UTC `YYYY-MM-DDTHH:MM:SSZ`. Startup fails closed when the file
is missing, malformed, unsupported, expired, or has an invalid connection
limit.

`max_connections` is the licensed maximum number of simultaneously active TCP
client connections. Console endpoints are not counted. The TCP/session slice
consumes this validated limit when connection admission is implemented.

V1 deliberately does not claim cryptographic authenticity for `server.license`.
Signature/envelope verification is a separate licensing-security slice.

## Server identity

Server identity is separate from incoming Client Authentication:

```text
Server identity
    proves the Server process to an outbound service

Client Authentication
    proves a remote client to ServerEngine
```

Microsoft Entra Server identity uses the confidential-client/client-credentials
model with a certificate. The access token is memory-only and is intended to
authenticate the Server to the License Service; it is not itself the Server
license.

Startup direction is now:

```text
server.json
    -> server.license
    -> Server identity
    -> server.lease
    -> Authentication
    -> Communication
    -> optional Project startup
```

The next licensing slice replaces the development/local lease source with a
License Service exchange authenticated by this Server identity.

## Server runtime lease

Server licensing now has two lifetimes:

```text
server.license
    long-lived entitlement ceiling

server.lease
    short-lived runtime authorization
```

The lease is Server-level and Project-independent. It may only narrow limits
from `server.license`. The Server control loop uses lease expiration as an
absolute wait deadline, so a standalone idle Server cannot continue indefinitely
after the lease expires.

Current V1 deliberately does not fake remote security. Signature verification,
Microsoft Entra daemon/server identity, License Service acquisition, renewal,
grace policy, and concurrent-Server accounting are the next slice.

Target:

```text
ServerEngine
    -> Microsoft Entra Server identity
    -> License Service
    -> signed short-lived server.lease
    -> local verification/enforcement
    -> renew before expiration
```

## Current Server startup boundary

The current startup sequence is:

```text
server.start()
    -> load + validate server.json
    -> load + validate server.license
    -> Authentication.start()
    -> Communication.start()
    -> optional Project startup
```

The order is architectural:

- `server.license` is Server-level state and is independent from Project state.
- Authentication must be ready before Communication can accept remote clients.
- Communication is stopped before Authentication during shutdown.
- Project startup remains optional and occurs only after Server infrastructure is ready.

A failure in configuration, license validation, Authentication startup, or
Communication startup fails closed.

## Server License

`server.license` is required process-level Server state. It is not part of
`project.json` and is not an Authentication credential store.

V1 contract:

```text
server.license
    version
    expires_at
    max_connections
```

`expires_at` is a UTC startup validity boundary using:

```text
YYYY-MM-DDTHH:MM:SSZ
```

`max_connections` is the licensed maximum number of simultaneously active TCP
Client Sessions. Console does not consume this limit.

The current V1 validates file structure, expiration, and limits. Cryptographic
license signing/verification is intentionally a separate future security slice;
the current code must not be described as providing tamper-resistant licensing.

## Authentication ownership

Authentication is Server-level, not Project-level.

Supported modes:

```text
none
contract
external
```

Meaning:

```text
none
    Authentication explicitly disabled.
    Startup succeeds without a provider.

contract
    Server-owned Authentication Contract boundary.
    Provider implementation is not yet present and fails closed.

external
    Delegated external identity provider.
    Microsoft Entra is the currently implemented provider.
```

Authentication and Authorization remain separate:

```text
Authentication
    establishes/verifies identity

Server Policy
    decides which established identity/origin may submit a request

Server
    executes request semantics
```

Neither Project nor Runtime owns Authentication.

## Microsoft Entra provider

For Microsoft Entra:

```text
server.json
    authentication.mode = external
    authentication.external.provider = microsoft_entra
    authentication.external.tenant_id
    authentication.external.audience
```

Provider startup uses local validation material:

```text
server.json
    |
    v
entra.cache
    |
    +-- tenant_id
    +-- audience
    +-- issuer
    +-- jwks_uri
    +-- retrieved_at
    `-- signing keys
            kid
            kty
            alg
            n
            e
            issuer
    |
    v
resident Microsoft Entra provider state
```

`entra.cache` is validation material only. It does not persist Client access
tokens or refresh tokens.

The startup contract is deliberately autonomous:

```text
valid matching cache
    -> provider may start without live Microsoft connectivity

missing / malformed / mismatched cache
    -> fail closed
```

Live OIDC metadata/JWKS refresh and signing-key rollover are separate provider
maintenance work. Authentication startup is not equivalent to a live Microsoft
availability check.

## Microsoft Entra JWT validation

ENTRA-JWT-V1 validates one Microsoft Entra access token locally against the
resident provider configuration and cached signing keys.

Validation path:

```text
access token
    -> three-part JWT structure
    -> decode JOSE header
    -> alg == RS256
    -> kid
    -> cached RSA signing key
    -> RSASSA-PKCS1-v1_5 / SHA-256 signature
    -> decode claims
    -> exact issuer
    -> exact configured audience
    -> exact tenant id
    -> exp
    -> optional nbf
    -> authenticated token identity
```

The resulting transport-independent identity contains available token values:

```text
tenant_id   <- tid
subject     <- sub
object_id   <- oid
client_id   <- azp, or appid for the supported token shape
```

Token validation does not create a Client Session and does not authorize a
Server request. Those responsibilities remain separate.

Cryptography is behind a narrow platform boundary:

```text
authentication/crypto/rsa_sha256_verifier.hpp

Windows
    -> CNG / BCrypt

non-Windows
    -> currently fail closed with crypto_unavailable
```

ServerEngine does not contain a private RSA implementation and does not add
OpenSSL to the Windows path.

## Next Communication slice

With Authentication and access-token validation in place, the next boundary is:

```text
TCP connection
    -> Client Session
         session_id
         name
         optional subid
         authentication state
    -> LOGIN
         name
         optional subid
         provider authentication data
    -> Authentication
    -> authenticated Client Session
    -> Server Policy
    -> Server request dispatch
```

The future Client Session admission path consumes
`server.license.max_connections`.

`name + subid` identifies a logical client/application category and is not
unique. `session_id` is the unique active connection identity.

## Authentication lifecycle

Authentication is Server process state, not Project state and not yet a Client
Session contract.

```text
server.start()
    -> load/validate server.json
    -> authentication.start()
    -> communication.start()
    -> optional Project startup
```

Modes are `none`, `contract`, and `external`. `none` initializes successfully
without a provider. `contract` and `external` fail closed until their provider
implementations are defined.

Communication never starts before Authentication initialization succeeds.
Shutdown stops Communication before Authentication.

## Server Policy

Server Policy is the single process-level access gate between Communication
request identity and Server request execution.

```text
Communication request
    |
    +-- origin = internal | console | tcp
    +-- TCP: name + optional subid
    |
    v
Server Policy
    |
    +-- denied -> transport-neutral access_denied response
    |
    `-- allowed -> server::execute()
```

Authentication and Server Policy are separate:

```text
Authentication
    establishes/validates identity

Server Policy
    decides whether that identity may submit a Server request

Server
    executes the request semantics
```

The first frozen policy rule is Project lifecycle access:

```text
LOAD
PUBLISH
BUILD
REBUILD
UNLOAD
```

Allowed origins:

```text
internal
console
tcp: name="Studio", subid="Studio"
```

Other TCP identities, including `Studio/HMI`, `Studio/Viewer`, and arbitrary
custom clients, are denied Project lifecycle access.

This restriction is enforced at the Communication -> Server boundary. Project
lifecycle functions themselves do not know about Console, TCP, Studio, LOGIN,
or authentication.

Access rules for GET_STATE, GET_VALUE, SHUTDOWN, Runtime commands,
subscriptions, and client messaging are intentionally not changed by this
slice; they are frozen separately.

## Communication directory

`communication/` owns common transport-neutral communication contracts:

```text
server_request
request_queue
communication
```

Transport-specific implementation belongs in subdirectories.

Current transport-specific subtree:

```text
communication/
└── console/
```

Future examples may include:

```text
communication/
├── console/
├── tcp/
└── ...
```

without moving transport-neutral command infrastructure.

## Console cross-platform implementation

The console transport is split into:

```text
console_input.hpp
console_input_windows.cpp
console_input_posix.cpp
server_console.hpp
server_console.cpp
```

`console_input.hpp` is platform-neutral.

Windows implementation:

```text
WaitForMultipleObjects(
    STD_INPUT_HANDLE,
    stop_event
)
```

POSIX implementation:

```text
poll(
    STDIN_FILENO,
    wake_pipe
)
```

Only the implementation file for the active platform is compiled by CMake.

The native Visual Studio project includes only the Windows backend because that
project is a Windows build artifact.

## Optional console

If no console endpoint is present in `server.json`:

- no `server_console` object is created;
- no console thread is started;
- no console input backend is opened.

Console is one communication transport, not a mandatory Server component.

## Architectural invariants

1. Physical folders define architecture.
2. Visual Studio mirrors physical folders exactly.
3. `configuration/` contains process configuration contracts/loaders.
4. `communication/` contains shared command infrastructure.
5. Each transport owns its dedicated implementation subtree.
6. Server/control thread is the sole lifecycle owner.
7. At most one resident Project is active.
8. `server_context.project == nullptr` exactly means `UNLOADED`.
9. LOAD, BUILD, and REBUILD require `UNLOADED`.
10. UNLOAD requires `LOADED`.
11. Resident Project contains runtime state only.
12. BUILD acceleration state is persisted outside resident `project`.
13. LOAD requires only the compiled Project artifact.
14. Missing required BUILD acceleration state means REBUILD is required.
15. Runtime hot paths do not depend on control-plane synchronization.
16. V4 has one Graph concept: `G`. BUILD/REBUILD do not create Graph generations.

## PIMPL construction rule

`console_input::implementation` is intentionally private and platform-specific.

Because `std::unique_ptr<implementation>` requires a complete implementation type
when constructing the object, `console_input` constructor/destructor definitions
live in each platform implementation file after the corresponding
`console_input::implementation` definition.

There is no shared `console_input_common.cpp`.

This avoids creating or destroying an incomplete PIMPL type and keeps native
Windows/POSIX state fully isolated inside its backend.


## Transport-neutral Server request boundary

Communication transports own framing, parsing, and presentation only. They
translate console syntax, TCP/JSON, or any future protocol into the same
`server_request` representation and publish a `server_request_message` to the
single `request_queue`.

```text
Console                     TCP/JSON                    future transport
   |                           |                              |
   | parse                     | parse                        |
   +---------------------------+------------------------------+
                               |
                        server_request
                               |
                         request_queue
                               |
                         server::execute()
                               |
              +----------------+----------------+
              |                                 |
         lifecycle                        Runtime Query V1
     LOAD/PUBLISH/BUILD/...          GET_STATE / GET_VALUE
```

`server::execute()` owns Server semantics. Transport implementations must not
implement lifecycle or Runtime behavior and must not expose JSON/TCP types to
the Server core.

`server_request_origin` is the direct, non-owning reply destination carried by
the queued envelope. It is not part of the semantic request payload and requires
no endpoint lookup. A future TCP session may therefore correlate and serialize
its own protocol request identifiers without reusing the process-local
`operation_id`.

The first Runtime query slice adds `GET_STATE` and `GET_VALUE`.

`GET_VALUE` resolves semantic identity/member names through mmap-native
`compiled.bin`, translates Graph slots through resident `runtime_binding_index`,
and reads the final FIXED_DIRECT Runtime image. The resident index contains only
published physical binding facts: dense object/member/base offsets and intrinsic
byte sizes. `runtime_layout` itself remains construction-only and G is never
copied. Runtime Query does not re-run ABI layout rules.

Mutating requests (`RUN`, `FREEZE`, `STEP`, `SET`, `RESET_IC`, `SNAP_IC`) are
not part of this slice. They require Runtime Controller / Runtime Command Queue
semantics and must not become direct Server-control-thread writes.

## Client LOGIN identity contract

Client application identity is deliberately small and transport-neutral.

```text
LOGIN
    name
    subid?      optional
    authentication_data?   mode/provider-specific, defined later
```

`name` is required. It identifies the logical client/application name used by
the Server communication domain.

`subid` is optional. It refines one client family without adding Server-side
client-type enums.

Examples:

```text
name="Studio", subid="HMI"
name="Studio", subid="Viewer"
name="Studio", subid="Studio"

name="Recorder"
name="PythonClient"
name="MyApi"
```

The Server does not define an enum for Studio/HMI/Viewer/custom. New client
families and Studio sub-applications therefore do not require a Server protocol
or C++ enum change.

`name` and `subid` are client/application identity, not authenticated user
identity and not authorization state.

```text
session_id
    unique active Session identity

name + optional subid
    non-unique logical client/application identity

Authentication
    determines whether the connection may authenticate according to the
    configured Server Authentication mode/provider

Authorization
    is a separate future policy domain
```

Multiple simultaneous Sessions may use identical `name` and `subid`.
Only `session_id` is unique among active Sessions.

Authentication mode does not change the base LOGIN identity shape:

```text
authentication.mode = none
    LOGIN(name, subid?) requires no provider-specific authentication data

authentication.mode = contract
    LOGIN keeps name/subid and carries Contract-provider authentication data

authentication.mode = external
    LOGIN keeps name/subid and carries External-provider authentication data
```

The exact provider-specific authentication payload and JSON wire encoding are
not frozen by this contract.

## Request execution result

```text
communication endpoint
        |
        v
server_request_message
|- server_request
`- direct origin callback
        |
        v
request_queue
        |
        v
Server/control thread
        |
        v
server::execute()
        |
        v
server_response
|- operation_id
|- server_status
`- diagnostic_collection
        |
        v
direct origin callback
        |
        v
originating endpoint
```

Invariants:

1. Server/control thread is the sole lifecycle owner.
2. `server::execute()` owns transport-neutral Server request semantics and does not format or print.
3. One external request creates exactly one `operation_id` and one `diagnostic_collection`.
4. `server_response` owns the complete operation result.
5. `server_request_message::origin` is direct and non-owning; no endpoint lookup is performed.
6. No virtual response hierarchy or shared ownership is used.
7. SHUTDOWN result is presented before communication endpoints are stopped.
8. Failed LOAD, BUILD, or REBUILD leaves Server UNLOADED.

## Project LOAD entry points

There are exactly two ways to request LOAD:

```text
1. Startup auto-LOAD

server.json
    |
    `- project.path
          |
          v
      server::load()

2. Runtime command

LOAD <project.json>
          |
          v
      server::load()
```

Both paths use the same `server::load()` implementation.

`server::load()` is the single owner of:

- UNLOADED/LOADED validation;
- project path resolution relative to `server.json`;
- Project construction;
- Project LOAD execution;
- failed-LOAD cleanup.

The state contract is:

```text
context.project == nullptr
    == UNLOADED
```

LOAD is valid only while UNLOADED.

```text
UNLOADED + LOAD success -> LOADED
UNLOADED + LOAD failure -> UNLOADED
LOADED   + LOAD         -> project_already_loaded
```

Startup `project.path` with `startup=load` is not a separate loading mechanism
and does not bypass normal LOAD state validation.

## Project documentation

Project construction is mode-oriented. There is no universal Project
construction context.

Temporary operation state is owned by explicit `load_context`, `build_context`,
and `rebuild_context` boundaries.

Detailed contracts are separated into:

```text
PROJECT.md
    lifecycle
    persisted BUILD state
    SourceSave / DB / compiled-G boundaries
    BUILD / REBUILD / LOAD publication

PROJECT_CONFIGURATION.md
    project.json schema
    composition
    configuration proof

FRONTEND_ARCHITECTURE.md
    file_id / string_id / identity_ref
    preprocessing/frontend
    lexical reuse
    dependency topology
```

Server architecture owns process lifecycle and resident Project publication.

## G acquisition modes

The Server has four paths to the same final G:

```text
LOAD
    compiled.bin -> G

PUBLISH
    project.json/source -> full compiler -> G
    -> compiled.bin only

REBUILD
    project.json/source -> the same full compiler -> G
    -> compiled.bin + fresh BUILD acceleration

BUILD
    mmap persisted BUILD baselines + sparse changed work -> G
    -> compiled.bin + refreshed BUILD acceleration
```

After G exists, every mode uses the same Runtime/SHM construction boundary:

```text
G + ABI + SHM policy
    -> derive physical Runtime/SHM layout once
    -> FIXED_DIRECT
         SHM == native Runtime
       or
       RELOCATABLE_TRANSFER
         relocatable SHM + native Task Runtime
    -> Project
```

For FIXED_DIRECT, whole Project Runtime/SHM offsets are 64-bit. Temporary
ABI-derived member offsets inside one native record use a 32-bit
`record_offset`; `UINT32_MAX` is reserved as the invalid offset. A single
native value/type layout must therefore fit within `UINT32_MAX` bytes.

Pointer/reference representation is derived from the Runtime target ABI, not
from the Server process. A Windows x64 Server can therefore build a
`windows-x86` FIXED_DIRECT image: Server-side pointers address the writable SHM
view, while Runtime reference slots contain 4-byte target virtual addresses.
The configured fixed SHM range must fit the target address space and must not
overlap the construction-time pending-link marker range.

This is a Runtime construction contract and does not change `compiled.bin` or
the semantic G representation.

### MSVC class ABI compiler oracle

`CXX-CLASS-ABI-V1B` does not infer C++ inheritance layout from documentation or
from the Server process architecture. Before the Runtime layout backend changes,
the repository builds one standalone source with the installed MSVC compiler in
both Visual Studio target platforms:

```text
Win32
x64
```

For every platform it measures the same cases under:

```text
pack 1
pack 2
pack 4
pack 8
pack 16
```

The oracle records compiler-produced `sizeof`, `alignof`, direct-base
offsets, base-member offsets, derived-member offset, and array stride. Its
matrix covers both single and non-virtual multiple inheritance:

```text
ordinary single inheritance
alignment/padding
base tail padding
polymorphic base
derived class introducing virtual dispatch
derived class introducing virtual dispatch after an over-aligned base
derived class introducing virtual dispatch with an over-aligned own member
override/final
virtual destructor
pure-virtual base with concrete derived class
empty-base optimization boundary
vfptr-only base
polymorphic root with an aligned data member
multi-level inheritance
native reference member inside a base subobject
ordinary multiple inheritance
multiple inheritance with alignment pressure
polymorphic first base
polymorphic second base
two polymorphic bases
derived class introducing virtual dispatch over two non-polymorphic bases
three direct bases with the polymorphic primary declared in the middle
three direct bases with two polymorphic bases
```

Offsets are measured from real compiler-generated objects and base conversions;
`offsetof` is not used for non-standard-layout classes. The oracle intentionally
does not depend on `runtime_layout`, so it cannot validate an implementation
against itself.

Run it with:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\nun_msvc_class_abi_oracle.ps1 `
    -ResultPath build\msvc-class-abi-oracle-v1b3.csv
```

The result path must not already exist. The runner configures dedicated
`build-oracle-x64` and `build-oracle-win32` directories, builds only
`ServerEngineV4MsvcClassAbiOracle`, and combines exactly 115 rows per
architecture into one CSV. The schema carries three explicit base columns so
declaration identity and compiler-selected physical base order can be compared
directly.

The final V1B.3 discriminator uses a Win32 class that introduces its own vfptr
while its direct base is 4-byte aligned and its own member is 8-byte aligned.
This isolates whether MSVC aligns the post-vfptr base region by the bases alone
or by the complete class alignment before Runtime layout rules are frozen.

Semantic G and `compiled.bin` preserve an ordered `graph_range` of 0..N direct
bases. Multiple inheritance is therefore not a Graph-format restriction.
The current source parser remains intentionally narrower and still rejects
multiple inheritance until its frontend slice is widened.

Cold `compiled.bin` verification validates the general 0..N base graph in
`O(T + E)`: direct bases must be unique and the inheritance graph must be
acyclic. No handle ordering, sort, or hash lookup is required.

Virtual-base metadata remains part of the semantic base record for future ABI
work. Current source parsing and Runtime physical layout remain fail-closed for
virtual inheritance. The empty-base row is also an oracle boundary only until
an explicit EBO layout stage is accepted.

### Windows C++ class ABI Runtime layout

`CXX-CLASS-ABI-V1B` installs the compiler-validated physical backend for
`windows-x86` and `windows-x64`. Semantic G remains ABI-independent and keeps
direct bases in declaration order. Runtime derives one construction-only
`record_offset` per persisted `base_record`; no physical base or member offset
is persisted in `compiled.bin`.

For a non-empty, non-virtual record, Runtime first resolves all direct base and
own-member layouts and computes the packed complete-record alignment. If a
polymorphic direct base exists, the first polymorphic base in declaration order
is the MSVC primary base at offset zero. Remaining polymorphic bases are placed
in declaration order, followed by non-polymorphic bases in declaration order.
If there is no polymorphic base but the record is polymorphic, Runtime reserves
one target-width vfptr at offset zero and aligns the following base region by
the complete-record alignment. Otherwise bases begin at offset zero in
declaration order. Own members follow all base subobjects.

Base placement always advances by the full `sizeof(base)`; base tail padding is
not reused. Effective base/member/vfptr alignment is bounded by the configured
pack. Empty-base optimization and virtual inheritance remain fail-closed.
Polymorphic storage is physical layout only: vfptr bytes remain zero from the
Runtime clear, so virtual calls, RTTI/dynamic_cast, dynamic `typeid`, and
virtual destruction are outside the Runtime contract.

Materialization recursively constructs direct base subobjects at their derived
`base_offsets` before constructing the current record's own members. The same
top-level object identity is preserved through base recursion so native
reference-member construction remains within the final object image.

The selected mode changes physical materialization, not semantic G. There is no
second semantic build stage and no second Graph.

Runtime/SHM is Phase 2. The current Phase-1 resident Project owns the mmap-native
compiled artifact at this convergence boundary.

`PUBLISH` deliberately does not create `project.manifest`, `source.bin`, or
`database.bin`. It removes the previous artifact set before full construction so
stale BUILD acceleration can never be paired with the newly published
`compiled.bin`. PUBLISH also disables BUILD-only semantic dependency/presence
capture while parsing; only compiled provenance required by `compiled.bin` is
retained.

## Persisted Project artifacts

The Project artifact root is:

```text
<root-project-dir>/.serverengine/<root-project.json filename>/
```

The direct physical layout is:

```text
.serverengine/<root-project.json>/
    project.manifest
    source.bin
    database.bin
    compiled.bin
```

The filenames come from `server.json.settings.files`.

Artifact roles are intentionally asymmetric:

```text
LOAD
    compiled.bin

PUBLISH
    output: compiled.bin only

BUILD
    project.manifest
    source.bin
    database.bin
    compiled.bin

REBUILD
    same full compiler as PUBLISH
    output: compiled.bin + fresh BUILD acceleration artifacts
```

`compiled.bin` contains the one compiled Project result used by LOAD to obtain
G. It is mmap-native: semantic strings/identities, G arrays, persisted read
indexes, the physical-file/root Source Map, and the ordered Studio-facing Assign
table are bound directly from the mapped file without reconstructing mutable containers. ABI-derived Runtime
layout belongs to Phase 2 and is not part of the current compiled format.

`project.manifest`, `source.bin`, and `database.bin` are BUILD
acceleration/lineage state. LOAD does not open them.

If a required BUILD artifact is absent or invalid, incremental BUILD cannot
continue and REBUILD is required.

There is no selector file or active/inactive persistence slot.

`source.bin` has a versioned/checksummed image contract for finalized File
Context state plus BUILD-only semantic presence and semantic dependency sidecars.
BUILD memory-maps it read-only directly.
`source_save_view::bind()` is O(1) and allocation-free. `source.bin` v5 contains
the normalized-path lookup index plus root-contiguous semantic dependency
observations and one sparse mmap-native reverse dependency hash index. The
semantic reverse hash index is sized by unique referenced targets `U`, while
its dependent-root adjacency stores semantic edges `E`; memory is `O(U + E)`,
not `O(Graph slots)`. BUILD does not rebuild an O(F) path hash table or an
O(|G|) semantic range table before incremental discovery.

BUILD change discovery is journal-first. Before dirty detection, BUILD captures
the checkpoint for the `source.bin` that may be produced by that BUILD. Dirty
detection starts from the persisted SourceSave checkpoint.

A valid Windows/NTFS SourceSave checkpoint reads the volume USN journal once and
maps changed file references through the persisted `file_reference -> file_id`
index. Matching data events are rebuilt directly. A dense bitset emits ascending
`file_id` order without sorting or per-file opens.

If journal continuity/support is unavailable, BUILD selects every current
persisted `file_id` as an acquisition candidate without filesystem reads.
A shared parallel exact-acquisition stage then reads/hashes each candidate once;
only SHA-256-different or missing files enter sparse File Context overlays.
Persisted OLD reverse topology expands `semantic_changed`, not raw journal
events, into the affected physical closure. Project-declaration edges in that
same OLD DAG identify physically affected semantic roots, so BUILD does not need
a duplicated contribution-to-root persistence index. This also preserves empty
semantic roots that had no old Source Map contributions. Reverse-closure
membership is tracked by a dynamically grown sparse `file_id` set, so closure
memory is `O(A)` rather than a zeroed `O(F)` marker.

Physical closure is followed by a separate semantic closure. Parser/Semantic
records, per semantic root, the lineage-stable type/object handles whose current
definition or storage identity was consumed. `source.bin` persists those
root-local observations together with an exact sparse reverse hash index:

```text
invalidated semantic root
    -> OLD compiled Source Map contributions
    -> contributed type/object handles
    -> source.bin reverse semantic dependency lookup
    -> dependent semantic roots
    -> transitive closure
```

This covers dependencies that have no physical include edge in the global Header
domain. Source link endpoints record both object and record-type dependencies, so
a changed member layout forces replay before an old `member_index` can be reused.

Changed Project composition is reconciled separately from physical reverse
closure. `project.manifest` identifies the OLD project.json tree; source.bin
provides each OLD Project node's direct Header/Source edges. CURRENT composition
is replayed into the sparse File Context overlay and emits the CURRENT semantic
root set. BUILD computes removed/new/persistent root roles without scanning all
files. A persisted root-preprocessor hash distinguishes composition-only edits
from root preprocessing-context edits; the latter invalidates all OLD roots and
replays all CURRENT roots. Appended CURRENT Header/Source roots are materialized
and lexed through the sparse lexical replacement path because they cannot appear
in OLD SourceSave change classification.

`database.bin` is BUILD-only retained frontend state keyed by `file_id`. It
pairs exact Header/Source snapshot bytes with their lexical words/directive
anchors. Exact changed Header/Source records are masked before parallel lexical
replacement starts; present changed files tokenize from the bytes retained by
exact classification, while missing files remain masked until affected
preprocessing decides whether they are still referenced. It is not a Semantic DB
and does not persist String Table or Identity Space state.
`compiled.bin` is the sole persisted owner of `string_id` / `identity_ref`
lineage as well as G.

ABI-derived Runtime layout is deferred to Phase 2. Its representation and any
future persistence/reuse policy are not part of the current Phase-1 artifact
contract.

There is no SAVE lifecycle command.

## Project configuration locator graph

The configuration manifest preserves how each child Project was declared.

```text
entry N
    declaring_file -> earlier entry
    locator_type    -> relative | absolute
    locator         -> normalized declared filesystem locator
```

Root-first declaration-order DFS guarantees:

```text
declaring_file < N
```

for every non-root entry. BUILD therefore reconstructs physical configuration
paths in one linear pass using already resolved parent entries.

The manifest store validates the distinguished root entry on both write and
read:

```text
declaring_file = invalid_configuration_file
locator_type = relative
locator = root project.json filename
```

Invalid root metadata is rejected by the artifact boundary itself.

```text
NO SORT
NO LOOKUP
```

Relative locators resolve from the declaring Project directory. Absolute locators
resolve directly and are location-bound. Resolved paths and platform path keys
remain temporary construction state.

### Source provenance boundary

Resident Project source provenance is final compiled metadata, not Graph hot
state. Runtime/Studio will obtain `physical file -> semantic data` from the
`compiled.bin` Source Map. BUILD-only physical dependency topology remains in
`source.bin`; aggregate semantic presence will live beside that topology rather
than inside G or inside individual DAG nodes.

Construction provenance is produced directly by Parser/Semantic and persisted
mmap-native in `compiled.bin`. BUILD-only semantic presence is persisted beside
the physical topology in `source.bin`.


## Client Session boundary

```text
TCP accept
    -> client_session(session_id)
    -> LOGIN(name)
    -> logged_in
    -> ordinary Server requests
```

LOGIN carries no credential. It is not Authentication and never enters
`request_queue`. `session_id` is unique per active connection; `name` is
deliberately non-unique.

Process-level Authentication selects Server mode:

```text
success -> FULL
failure/unavailable -> DEMO
```

Policy:

```text
FULL
    all commands for all logged-in clients

DEMO
    max active TCP connections = 1
    client name is unrestricted
    LOAD / UNLOAD / read/runtime work allowed
    PUBLISH / BUILD / REBUILD denied
```

DEMO allows one active TCP connection total and runs for five minutes. Client
`name` remains application metadata and does not participate in licensing or
authorization. The current slice implements mode selection and command policy.
Connection-count and timed shutdown enforcement belong to the TCP
accept/run-loop slice because the TCP backend does not exist yet.

## Project lifecycle state machine

```text
UNLOADED
    +-- LOAD <path> success ------> LOADED
    +-- LOAD <path> failure ------> UNLOADED
    +-- PUBLISH <path> success ---> LOADED
    +-- PUBLISH <path> failure ---> UNLOADED
    +-- BUILD <path> success -----> LOADED
    +-- BUILD <path> failure -----> UNLOADED
    +-- REBUILD <path> success ---> LOADED
    `-- REBUILD <path> failure ---> UNLOADED

LOADED
    `-- UNLOAD -------------------> UNLOADED
```

LOAD, PUBLISH, BUILD, and REBUILD each receive a Project path.

BUILD opens the persisted BUILD artifacts for that Project path. It
does not consume `server_context.project`.

If BUILD fails, the old persisted BUILD state remains available for later
incremental reuse, but no resident Project remains active.

## Compiled Source Provenance Boundary

The final Project keeps G free of file ownership. Source provenance is a cold
compiled sidecar intended for Runtime/Studio inspection and sparse BUILD
reconciliation.

```text
G
    semantic/runtime result

Source Map
    semantic root -> contributions
    physical file -> contribution indices
```

Source Map is not a semantic dependency graph and does not alter the File
Context dependency DAG.


## Development phase boundary

Current implementation work is deliberately ordered as:

```text
PHASE 1
LOAD / PUBLISH / BUILD / REBUILD
    -> G

PHASE 2
G
    -> Runtime
    -> SHM
    -> resident Project
```

Phase 1 includes persistence for the four ways of obtaining G. PUBLISH and
REBUILD share one full source-construction implementation. `compiled.bin` is
mandatory in both modes; only REBUILD enables the project.manifest/source.bin/
database.bin BUILD-acceleration branches. BUILD has a different failure contract
and therefore does not inherit full-construction persistence mechanics
automatically.

## Current Project construction boundary

The implemented shared PUBLISH/REBUILD full-construction path is organized as:

```text
recursive project.json composition
    -> root preprocessor configuration
    -> configuration manifest
    -> fresh File Context
    -> Header/Source exact-byte materialization
    -> retained lexical generation
    -> sparse directive anchors
    -> Assign exact-byte materialization
    -> ordered Assign user-table construction
    -> Header semantic line
         -> C++ preprocessing / active quoted-include discovery
         -> complete Type domain
         -> Header internal-static construction state
    -> semantic barrier
    -> Source semantic line
         -> no C++ preprocessing/include
         -> Project objects / initialization / links
    -> one final G
    -> terminal dependency-topology finalization
```

Assign is Studio-facing user data only. It performs no semantic variable
resolution, G mutation, Runtime binding, or file dependency emission.

PUBLISH and REBUILD both pre-clean `project.manifest`, `source.bin`,
`database.bin`, and `compiled.bin` before full source construction so no stale
BUILD lineage can survive beside a new final G. PUBLISH then persists only
`compiled.bin`; REBUILD additionally creates fresh BUILD acceleration. REBUILD uses
the final artifact names directly; there are no `.tmp` files, A/B slots,
selector files, or rollback generations. The pre-clean prevents a failed
best-effort acceleration branch from leaving a stale previous BUILD baseline.

After topology finalization, REBUILD freezes construction state and fans out four
independent direct-mmap persistence branches through the existing fixed
construction execution lanes:

```text
final G / construction state
    +-> compiled.bin      -> validate + flush -> resident mmap Project
    +-> source.bin        -> BUILD acceleration
    +-> database.bin      -> BUILD acceleration
    `-> project.manifest  -> BUILD acceleration
```

The compiled branch is the only mandatory persistence branch. source.bin,
database.bin, and project.manifest are best-effort outputs; failures are warnings
and become a problem for the next BUILD rather than invalidating the new G.

source.bin is encoded directly from finalized File Context/Source Map state.
database.bin streams exact Header/Source snapshot bytes together with retained
Lexical Generation records, words, and directive anchors into mapped sections;
String Table and Identity Space lineage are persisted once in compiled.bin.
The optional SourceSave USN identity indexes are prepared once and retained only
as persistence-specific tracking state. None of these production paths creates
a full-size serialized `std::vector<std::byte>` or `.tmp` artifact. source.bin
and database.bin receive cold verification before flush. The compiled encoder
calculates section CRCs and ends with `compiled_project_view::bind()` for
structural validation, then the compiled branch flushes and reopens read-only.
PUBLISH/REBUILD do not repeat that bind or call the full `verify_contents()`
audit on the fresh image. Full CRC/semantic auditing is an explicit diagnostic
operation and remains covered by persistence tests.

LOAD maps and structurally binds an existing `compiled.bin` read-only without
rebuilding Graph/string/identity containers. The mapping and
`compiled_project_view` are owned by the resident `project`; `verify_contents()`
remains a separate cold audit and is not part of the normal LOAD hot path.

REBUILD publishes through the same resident representation after the mandatory
compiled branch is valid and durable. The scoped persistence lanes are joined
before construction state is destroyed, so no detached/background owner is
introduced. A G/compiled/publication failure is still a REBUILD failure and
runs the existing artifact cleanup. LOAD and REBUILD therefore complete their
Phase-1 `-> G` contract; BUILD remains the unfinished Phase-1 lifecycle path
before Runtime/SHM work.

The implemented BUILD code currently reaches:

```text
project.manifest load
    -> configuration-input verification
    -> recomposition when configuration bytes changed
    -> aggregate hash comparison
    -> source.bin read-only mmap/bind
    -> exact physical dirty detection
    -> OLD reverse dependency affected closure
```

The C++ BUILD entry now matches the lifecycle contract:

```text
UNLOADED
    -> BUILD <project-path>
    -> resolve Project path relative to server.json
    -> open persisted BUILD artifacts
```

BUILD does not consume resident Project state. Remaining BUILD work includes
persisted File Context/DB reuse, sparse affected frontend/Parser/Semantic work,
and construction of G. BUILD persistence mechanics are intentionally not frozen
yet because a failed BUILD must preserve the previously persisted BUILD state.

## Filesystem and Project path boundaries

Filesystem path concerns are split by ownership.

The common boundary:

```text
filesystem_path.hpp
filesystem_path.cpp
```

owns:

```text
strict UTF-8 <-> native path conversion
filesystem_path_key
filesystem_path_key_hash
make_filesystem_path_key()
```

Filesystem-equivalence semantics are platform-defined inside that common
boundary:

```text
Windows
    lexically normalized path
        -> invariant Unicode lowercase key
        -> case-insensitive equivalence

POSIX
    lexically normalized path
        -> case-sensitive equivalence
```

This contract is shared by Server configuration validation and Project
construction. `configuration/` does not depend on `project/`.

The Project-specific boundary:

```text
project_path.hpp
project_path.cpp
```

owns only:

```text
resolve_project_path(path, output)
    -> absolute
    -> lexically normalized
    -> success | failed
```

Project construction therefore uses the two boundaries in sequence:

```text
input locator
    -> resolve_project_path()
    -> make_filesystem_path_key()
```

Both operations are status-returning no-exception boundaries. Failure stops
construction; there is no fallback to different filesystem-equivalence
semantics.

## File Context storage boundary

`file_context` is construction state, never resident Project runtime state.

Identity lifetime:

```text
REBUILD
    fresh file_id space

BUILD
    bind/restore committed slots
    preserve existing file_id
    append new file_id values only
```

Logical compact state:

```text
file_record[]
file_physical_record[]
file_dependency_record[]
native_path_chars[]
path_index[]
forward_edges[]
reverse_edges[]
```

REBUILD materializes these arrays for a fresh construction.

BUILD now binds `source.bin` directly as the committed File Context baseline.
Existing file identity and adjacency remain mmap-backed; only touched existing
files materialize sparse native-path/physical/content state, while newly
discovered files append local records after the committed `file_id` range.
No O(F) File Context reconstruction is performed at BUILD startup.

Affected dependency adjacency is now replaced sparsely. BUILD marks each source
whose outgoing set is reconstructed, compares only that source's old mmap edges
with its new staged edges, and keeps reverse changes as sparse add/remove deltas.
Unchanged adjacency remains mmap-backed; no dense topology reconstruction occurs.

Physical acquisition retains the existing fast proof contract:

```text
native change token proves unchanged
    -> no read

otherwise
    -> stable exact read
    -> SHA-256
```

The direct reverse topology is persisted SourceSave data because BUILD uses it
to compute the affected closure before updating dependency relations for the
current construction.

`database.bin` is bound separately as the immutable source/lexical BUILD
baseline. File Context reads unchanged Header/Source spelling directly from mmap
through `file_content_baseline_view`; `lexical_generation` reads encoded
words/directive anchors through `lexical_baseline_view`. Changed/appended files
occupy sparse native overlays. Neither construction subsystem depends on
database persistence implementation types.

REBUILD may build complete topology in `O(F + E)` with the current no-sort
finalizer.

BUILD updates topology in `O(A + E_old(A) + E_new(A))` for `A` replaced
sources while preserving the same logical `file_id -> file_id` adjacency model.
This bound deliberately includes reading old outgoing edges: exact removals
cannot be known without examining the previous adjacency of a replaced source.

There is no generation-specific dependency-node identity and no Graph-generation
model. BUILD may reuse persisted storage internally, but the architectural
result of LOAD, PUBLISH, BUILD, or REBUILD is always one `G`.

The `graph` class is dense final-G state only. BUILD never binds `compiled.bin`
into `graph`; it reads OLD semantic state directly from `compiled_project_view`
and records changed semantics in BUILD-local `graph_delta`.


## Semantic Source Map

V4 keeps semantic source provenance separate from hot G records.

```text
G
    final semantic WHAT
    no file_id/source_id in type/object/link records

compiled.bin Source Map
    semantic root -> contiguous contributions
    physical file_id -> secondary contribution index

source.bin
    physical file state
    direct preprocessing/file dependency DAG
    BUILD-only semantic presence counters
```

One canonical Source Map contribution is:

```text
{ physical file_id, semantic data }
```

The semantic data is one of:

```text
type declaration -> identity_ref slot
type definition  -> identity_ref slot
object           -> identity_ref slot
link             -> link_handle slot
```

Members are not duplicated in Source Map. A type definition maps to G and its
members are queried from that type. Object type and link endpoints are likewise
queried from G.

The same physical header may execute under more than one semantic root. Those
executions are distinct ownership facts even when they produce the same physical
datum. Canonical storage is therefore:

```text
root_file_id -> contiguous contribution range
```

There is no global `(physical file_id, semantic datum)` canonicalization and no
root contribution-index array. Runtime/Studio's physical query is the one
secondary index:

```text
file_id -> contribution_id[]
```

Both views refer to the same root-owned contribution records; semantic payload is
not copied into a second representation.

The File Context DAG remains only direct file dependency topology produced by
actual preprocessing execution. Source Map ownership is not a second semantic
dependency DAG.

`source.bin` persists aggregate semantic presence needed to subtract/add root
ownership during sparse BUILD:

```text
type_handle   -> declaration_count, definition_count
object_handle -> producer_count
link_handle   -> producer_count
```

These counters are derived from root-owned contributions and are BUILD state.
They are not Runtime semantic payload and are not embedded in G.
