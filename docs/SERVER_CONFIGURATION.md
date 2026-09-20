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
  "version": 1,

  "abi": {
    "target": "windows-x64",
    "pack": 8
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

## ABI

Required process-wide ABI:

```jsonc
"abi": {
  "target": "windows-x64",
  "pack": 8
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

One Server owns one ABI and one SHM layout contract. Every Project and
subproject constructed or restored by that Server uses this ABI.
`project.json` cannot override it.

## Project Startup

Optional:

```jsonc
"project": {
  "path": "project.json",
  "startup": "load"
}
```

`startup` defaults to `load`.

Supported values:

```text
load
rebuild
```

`startup=build` is invalid because BUILD requires an already resident Project.

Startup state transitions:

```text
startup=load
    UNLOADED -> LOAD -> LOADED on success

startup=rebuild
    UNLOADED -> REBUILD -> LOADED on success
```

A failed startup Project operation leaves the Server UNLOADED and startup fails.

Relative Project paths are resolved relative to `server.json`.

## Runtime Project Commands

```text
LOAD <project-path>
    requires UNLOADED

BUILD
    requires LOADED
    uses the currently resident Project

UNLOAD
    requires LOADED

REBUILD <project-path>
    requires UNLOADED

SHUTDOWN
EXIT
```

No Project lifecycle command silently invokes another mode.

## Current Project Pipeline Status

Current V4 implementation:

```text
LOAD
    persisted Graph restore is still scaffolded

REBUILD
    recursively composes all project.json inputs
    builds candidate project_configuration_manifest
    computes aggregate project_configuration_hash
    stops before Source Manager/G0

BUILD
    requires resident Project
    loads committed project.manifest
    verifies all known configuration inputs
    recomposes when configuration bytes changed
    compares aggregate configuration hash
    stops before Source Manager/Gn->Gn+1
```

The incomplete REBUILD path does not persist its candidate manifest because no
generation has been successfully published yet.

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
