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
  "version": 4,

  "settings": {
    "abi": {
      "target": "windows-x64",
      "pack": 8
    },

    "files": {
      "manifest": "project.manifest",
      "source_save": "source.bin",
      "database": "database.bin",
      "compiled": "compiled.bin"
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
