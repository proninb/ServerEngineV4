# Server Configuration

`server.json` is process-level Server configuration.

The JSON parser supports:

```jsonc
// line comments
/* block comments */
```

## Example

```jsonc
{
  "version": 9,

  "settings": {
    "abi": {
      "target": "windows-x64",
      "pack": 8
    },

    "shm": {
      "mode": "fixed_direct",
      "name": "CW.ServerEngineV4.Project",
      "fixed_base_address": "0x0000010000000000"
    },

    "files": {
      "manifest": "project.manifest",
      "source_save": "source.bin",
      "database": "database.bin",
      "compiled": "compiled.bin"
    }
  },

  "authentication": {
    "mode": "none"
  },

  "communication": {
    "endpoints": [
      {
        "name": "console",
        "transport": "console"
      }
    ]
  },

  "project": {
    "path": "project.json",
    "startup": "load"
  },

  "logging": {
    "level": "info",
    "console": true,
    "file": "logs/server.log"
  }
}
```

## Settings

`settings` is required process-level configuration. It contains low-level
contracts shared by all Project lifecycle modes.

### ABI

```jsonc
"settings": {
  "abi": {
    "target": "windows-x64",
    "pack": 8
  },
  ...
}
```

Supported targets:

```text
windows-x86
windows-x64
posix-x64
```

Supported pack values:

```text
1
2
4
8
16
```

One Server owns one ABI and one Runtime/SHM materialization contract.
`project.json` cannot override either one.

### SHM

`settings.shm` is required process-level policy.

Fixed direct mode:

```jsonc
"shm": {
  "mode": "fixed_direct",
  "name": "CW.ServerEngineV4.Project",
  "fixed_base_address": "0x0000010000000000"
}
```

Relocatable transfer mode:

```jsonc
"shm": {
  "mode": "relocatable_transfer",
  "name": "CW.ServerEngineV4.Project"
}
```

Supported modes:

```text
fixed_direct
relocatable_transfer
```

`fixed_direct` means SHM is the native Runtime storage. Native C++ references
point directly inside that mapping, so the mapping must be created at the
configured fixed virtual address.

`relocatable_transfer` means SHM may be mapped at an arbitrary virtual address.
Native Task Runtime storage is separate and values transfer between SHM and that
native Runtime. Task execution still uses direct C++ references; SHM relocation
does not introduce proxies, handles, or offset lookup into the execution hot
path.

`name` is required for every SHM mode. It is the stable logical Project SHM
name shared by Server and Task processes. The portable configuration form uses
1-128 characters from:

```text
A-Z  a-z  0-9  .  _  -
```

There is no hidden generated suffix or product prefix. Windows uses the
configured name directly. POSIX adds only the leading `/` required by
`shm_open()`.

`fixed_base_address` is:

```text
required for fixed_direct
forbidden for relocatable_transfer
a non-zero target virtual address encoded as a 0x hexadecimal string
```

Platform mapping code is responsible for validating whether the configured
address can actually be reserved/mapped. Name collision and exact-address
collision fail closed; neither the name nor address receives an automatic
fallback.

FIXED_DIRECT is target-ABI driven. The Server process pointer width does not
have to equal the Runtime target pointer width. For example, a Windows x64
Server may materialize a `windows-x86` Runtime by writing 4-byte target
addresses, provided the fixed SHM range is representable by the x86 target and
the Server can map that same virtual address.

SHM size is deliberately not configuration. It is derived from final G + ABI.
There is no separately configured Runtime size.

### Files

```jsonc
"settings": {
  ...
  "files": {
    "manifest": "project.manifest",
    "source_save": "source.bin",
    "database": "database.bin",
    "compiled": "compiled.bin"
  }
}
```

Each value is one non-empty relative filename:

```text
no absolute path
no directory component
no "." / ".."
all four names are distinct under platform filesystem semantics
```

Filename equivalence follows the common filesystem path contract:

```text
Windows -> case-insensitive
POSIX   -> case-sensitive
```

The configuration layer uses `make_filesystem_path_key()` directly and does not
depend on Project construction code.

`settings.files` is Server-wide policy and exists independently of the optional
startup `project`.

Mode usage:

```text
LOAD
    compiled

PUBLISH
    full source construction
    writes compiled only
    removes stale BUILD-acceleration artifacts

BUILD
    manifest
    source_save
    database
    compiled

REBUILD
    same full source construction as PUBLISH
    writes compiled + fresh manifest/source_save/database
```

`compiled` is the only persisted artifact required by LOAD.
`manifest`, `source_save`, and `database` are BUILD acceleration/lineage state.
If required BUILD state is missing or invalid, incremental BUILD cannot proceed
and REBUILD is required.

The configuration parser reports schema failures against their fully qualified
context, for example:

```text
settings requires abi, shm, and files
settings.abi requires target and pack
settings.shm requires mode and name
settings.files.manifest must be a single relative file name
settings.files entries must use distinct file names
```

The current implementation already wires `settings.files.manifest` into
`project_configuration_manifest_store`.

## Server Identity

`server_identity` is the outbound identity of the Server process. It is
independent from incoming Client Authentication and from Project identity.

```jsonc
"server_identity": {
  "mode": "none"
}
```

Microsoft Entra confidential-client mode:

```jsonc
"server_identity": {
  "mode": "microsoft_entra",
  "microsoft_entra": {
    "tenant_id": "<tenant-id>",
    "client_id": "<ServerEngine daemon app client-id>",
    "scope": "api://<license-service-app-id>/.default",
    "certificate_thumbprint": "<40 hex SHA-1 thumbprint>",
    "certificate_store": "current_user"
  }
}
```

On Windows the Server reads the certificate from the Windows `MY` store,
requires an accessible CNG private key, creates a PS256 client assertion, and
requests an app-only token from the tenant-specific Microsoft Entra v2 token
endpoint. The assertion carries the certificate SHA-256 thumbprint in
`x5t#S256`.

The scope must end with `/.default`. The returned access token is retained in
process memory only. It is not a Server license and is not persisted.

V1 acquires the token during startup. Token renewal and the License Service
exchange are subsequent slices.

## Server License

`server.license` is a required Server-level file located beside `server.json`.
It is not configured inside `server.json`.

```jsonc
{
  "version": 1,
  "expires_at": "2099-12-31T23:59:59Z",
  "max_connections": 32
}
```

`expires_at` is a UTC startup-expiration boundary. `max_connections` is the
maximum active TCP client connection count; Console endpoints do not consume
this limit.

V1 validates structure, expiration, and limits. Cryptographic signature
verification is intentionally reserved for a later license-security slice.

## Server Lease

`server.lease` is the short-lived runtime authorization layer above the
longer-lived `server.license` entitlement.

```jsonc
{
  "version": 1,
  "lease_id": "<opaque lease id>",
  "not_before": "2026-09-28T00:00:00Z",
  "expires_at": "2026-09-28T12:00:00Z",
  "max_connections": 32
}
```

At startup the lease must be active, expire no later than `server.license`, and
may only narrow `max_connections`.

The Server control thread waits against the lease expiration deadline. When the
lease expires, an idle standalone Server leaves the run loop and shuts down.

V1 is an enforcement foundation, not the final security boundary:
`server.lease` is not yet cryptographically signed and is not yet acquired from
the License Service. The next slice is Microsoft Entra Server identity + signed
lease acquisition/renewal.

## Authentication

`authentication` is required process-level Server configuration.

### Disabled

```jsonc
"authentication": {
  "mode": "none"
}
```

`none` explicitly disables Authentication and initializes successfully.

### Server-owned contract

```jsonc
"authentication": {
  "mode": "contract"
}
```

`contract` reserves the Server-owned Authentication Contract boundary. The
provider implementation is not yet present, so this mode currently fails
closed during Server startup.

### Microsoft Entra

```jsonc
"authentication": {
  "mode": "external",
  "external": {
    "provider": "microsoft_entra",
    "tenant_id": "<tenant-id>",
    "audience": "<ServerEngine API application/client-id>"
  }
}
```

For `mode=external`, the `external` object is required. For all other modes it
is forbidden.

Current supported external provider:

```text
microsoft_entra
```

Required Microsoft Entra fields:

```text
provider
tenant_id
audience
```

The Server loads `entra.cache` from the directory containing `server.json`.

Current cache contract:

```jsonc
{
  "version": 1,
  "tenant_id": "<tenant-id>",
  "audience": "<ServerEngine API application/client-id>",
  "issuer": "<tenant-specific issuer>",
  "jwks_uri": "<Microsoft JWKS URI>",
  "retrieved_at": "<cache retrieval timestamp>",
  "keys": [
    {
      "kid": "<key id>",
      "kty": "RSA",
      "alg": "RS256",
      "n": "<base64url RSA modulus>",
      "e": "<base64url RSA exponent>",
      "issuer": "<issuer, when present>"
    }
  ]
}
```

The configured `tenant_id` and `audience` must match the cache. The signing-key
set must be non-empty. Unsupported key types/algorithms fail closed.

`entra.cache` contains public validation material only. Client access tokens and
refresh tokens are never persisted there.

A valid local cache allows autonomous Server startup without live Microsoft
connectivity. A missing, malformed, or mismatched cache fails Authentication
startup. Live metadata/JWKS refresh is not implemented in the current slice.

### Access-token validation

Microsoft Entra access tokens are validated locally after provider startup:

```text
JWT structure
alg == RS256
kid
RSA/SHA-256 signature
iss
aud
tid
exp
nbf, when present
```

The validator produces token identity values from:

```text
tid
sub
oid
azp / appid
```

Windows uses CNG/BCrypt for RS256 verification. The current non-Windows crypto
backend intentionally fails closed with `crypto_unavailable`.

JWT validation is independent from TCP framing, Client Session, LOGIN, and
Server Policy. Those layers consume the Authentication result but do not own
token semantics.

### Startup order

Authentication starts only after both `server.json` and `server.license` have
been validated:

```text
server.json
    -> server.license
    -> Authentication
    -> Communication
    -> optional Project startup
```

Communication is therefore never opened before Authentication reaches its
configured ready state.

## Project Startup

Optional:

```jsonc
"project": {
  "path": "project.json",
  "startup": "load"
}
```

`startup` defaults to `load`.

The currently configured startup schema supports:

```text
load
publish
rebuild
```

Both begin from `UNLOADED`.

```text
startup=load
    UNLOADED -> LOAD <path> -> LOADED on success

startup=publish
    UNLOADED -> PUBLISH <path> -> LOADED on success

startup=rebuild
    UNLOADED -> REBUILD <path> -> LOADED on success
```

BUILD is now architecturally an `UNLOADED + path` operation as well, but
`startup=build` is not added to the configuration schema by this documentation
change. It can be enabled separately if desired.

A failed startup Project operation leaves the Server UNLOADED and startup fails.

Relative Project paths are resolved relative to `server.json`.

## Runtime Project Commands

```text
LOAD <project-path>
    requires UNLOADED
    obtains final G from compiled.bin without Parser/source construction

PUBLISH <project-path>
    requires UNLOADED
    full source construction -> final G -> compiled.bin
    creates no BUILD acceleration state

BUILD <project-path>
    requires UNLOADED
    reuses persisted SourceSave/DB BUILD state

REBUILD <project-path>
    requires UNLOADED
    ignores persisted incremental BUILD state

UNLOAD
    requires LOADED

SHUTDOWN
EXIT
```

No Project lifecycle command silently invokes another mode.

BUILD is no longer an operation on the currently resident Project.

## Current Project Pipeline Status

Target architecture:

```text
LOAD
    compiled.bin -> final G
    -> Runtime / SHM
    -> LOADED

PUBLISH
    project.json + source inputs
    -> full compiler
    -> final G
    -> compiled.bin
    -> Runtime / SHM
    -> LOADED

BUILD
    persisted configuration + SourceSave + DB + compiled G
    -> exact dirty detection
    -> affected reverse closure
    -> sparse construction
    -> replace persisted artifacts
    -> Runtime / SHM
    -> LOADED

REBUILD
    fresh configuration composition
    -> fresh SourceSave / DB
    -> fresh final G
    -> replace persisted artifacts
    -> Runtime / SHM
    -> LOADED
```

Current Phase-1 status:

```text
LOAD
    mmap-native compiled.bin restore implemented

PUBLISH
    full source construction implemented through compiled.bin persistence
    creates no project.manifest/source.bin/database.bin

REBUILD
    complete through direct final artifact persistence and resident publication

BUILD
    enters only from UNLOADED
    configuration verification/recomposition implemented
    source.bin mmap baseline implemented
    exact physical dirty classification implemented
    sparse OLD reverse affected closure implemented
    database.bin exact source/lexical baseline implemented
    sparse lexical replacement implemented
    affected semantic-root selection implemented
    sparse Parser/Semantic -> final G construction still remains
```

## Communication

Only endpoints declared in `communication.endpoints` exist.

Console:

```jsonc
{
  "name": "console",
  "transport": "console"
}
```

If omitted:

```text
no server_console object
no console thread
no console input backend
```

TCP/JSON remains a configuration contract but its socket backend is not implemented
in the current architecture stage. Configuring it still fails explicitly.

The transport-neutral Client Session core is implemented independently of the
future TCP backend:

```text
accepted TCP connection
    -> client_session(session_id)
    -> LOGIN
    -> Authentication
    -> authenticated session
    -> ordinary Server requests
```

LOGIN is not queued as a Server request. Before LOGIN succeeds, the session
cannot emit a `request_identity`. `session_id` is unique per active connection;
`name + subid` is deliberately non-unique.

## Configuration Ownership Boundary

`server.json` configures process-level Server behavior, including the single
ABI and Runtime/SHM materialization policy used by the Server.

Project construction inputs and their persisted manifest do not belong to
`server_context`.

See:

```text
PROJECT.md
PROJECT_CONFIGURATION.md
```
