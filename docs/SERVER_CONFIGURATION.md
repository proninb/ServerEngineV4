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
  "version": 3,

  "settings": {
    "abi": {
      "target": "windows-x64",
      "pack": 8
    },

    "files": {
      "manifest": "project.manifest",
      "source_save": "source.bin",
      "database": "database.bin",
      "compiled": "compiled.bin",
      "baseline": "baseline.bin"
    }
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

One Server owns one ABI and one SHM layout contract. `project.json` cannot
override it.

### Files

```jsonc
"settings": {
  ...
  "files": {
    "manifest": "project.manifest",
    "source_save": "source.bin",
    "database": "database.bin",
    "compiled": "compiled.bin",
    "baseline": "baseline.bin"
  }
}
```

Each value is one non-empty relative filename:

```text
no absolute path
no directory component
no "." / ".."
all five names are distinct under platform filesystem semantics
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
    baseline
    compiled

BUILD
    baseline
    manifest
    source_save
    database
    compiled

REBUILD
    produces fresh manifest/source_save/database/compiled
    and publishes them through baseline
```

The configuration parser reports schema failures against their fully qualified
context, for example:

```text
settings requires abi and files
settings.abi requires target and pack
settings.files.manifest must be a single relative file name
settings.files entries must use distinct file names
```

The current implementation already wires `settings.files.manifest` into
`project_configuration_manifest_store`.

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
rebuild
```

Both begin from `UNLOADED`.

```text
startup=load
    UNLOADED -> LOAD <path> -> LOADED on success

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
    restores the last committed final G

BUILD <project-path>
    requires UNLOADED
    reuses the last successful SourceSave/DB baseline

REBUILD <project-path>
    requires UNLOADED
    ignores the old incremental baseline

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
    committed final G
    -> Runtime / SHM
    -> LOADED

BUILD
    committed configuration + SourceSave + DB + final-G baseline
    -> exact dirty detection
    -> affected reverse closure
    -> sparse construction
    -> coordinated durable commit
    -> Runtime / SHM
    -> LOADED

REBUILD
    fresh configuration composition
    -> fresh SourceSave / DB
    -> fresh final G
    -> coordinated durable commit
    -> Runtime / SHM
    -> LOADED
```

Current implementation status:

```text
LOAD
    final-G restore is still scaffolded

REBUILD
    implemented through deterministic source closure
    and Assign byte materialization

BUILD
    BUILD <project-path> enters only from UNLOADED
    configuration-manifest verification/recomposition implemented
    SourceSave/DB incremental construction not implemented
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

TCP/JSON remains a configuration contract but its backend is not implemented in
the current architecture stage. Configuring it fails explicitly.

## Configuration Ownership Boundary

`server.json` configures process-level Server behavior, including the single
ABI used by the Server's one SHM layout.

Project construction inputs and their persisted manifest do not belong to
`server_context`.

See:

```text
PROJECT.md
PROJECT_CONFIGURATION.md
```
