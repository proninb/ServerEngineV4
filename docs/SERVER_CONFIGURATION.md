# Server Configuration

`server.json` is the process-level Server configuration.

The parser supports both:

```jsonc
// line comments
/* block comments */
```

## Current example

```jsonc
{
  "version": 1,

  "communication": {
    "endpoints": [
      {
        "name": "console",
        "transport": "console"
      }
    ]
  },

  "logging": {
    "level": "info",
    "console": true,
    "file": "logs/server.log"
  },

  "telemetry": {
    "console": true,
    "subsystems": [
      "server",
      "communication",
      "project",
      "load",
      "build",
      "rebuild",
      "save",
      "unload",
      "runtime",
      "persistence"
    ]
  }
}
```

## Project startup

Optional:

```jsonc
"project": {
  "path": "project.json",
  "startup": "load"
}
```

`startup` is optional and defaults to `load`.

Supported startup operations:

```text
startup = load
    UNLOADED -> LOAD -> LOADED

startup = rebuild
    UNLOADED -> REBUILD -> LOADED
```

`startup = build` is not valid. BUILD requires an already resident Project and
therefore can run only after LOAD or a previous successful BUILD.

Absent `project`: the Server starts UNLOADED and waits for a command.

Relative Project paths are resolved relative to `server.json`.

## Runtime Project commands

```text
LOAD <project-path>
    requires UNLOADED

BUILD
    requires LOADED
    operates on the currently resident Project

UNLOAD
    requires LOADED

REBUILD <project-path>
    requires UNLOADED

SHUTDOWN
EXIT
```

LOAD, BUILD, and REBUILD are separate lifecycle operations. No command silently
runs another mode as a prerequisite.

If LOAD, BUILD, or REBUILD fails, the Server is UNLOADED.

## Communication

Only endpoints listed in `communication.endpoints` exist.

Console:

```jsonc
{
  "name": "console",
  "transport": "console"
}
```

If this entry is removed, no console object or console input thread is created.

TCP/JSON remains part of the configuration contract:

```jsonc
{
  "name": "main",
  "transport": "tcp",
  "protocol": "json",
  "address": "0.0.0.0",
  "port": 39001
}
```

The TCP backend is not implemented in this step, so configuring it currently
fails explicitly instead of being ignored.

## Configuration ownership boundary

`server.json` configures the Server process and may select a startup LOAD or
REBUILD. BUILD is a runtime operation over an already resident Project.

Construction state never belongs to `server_context`.

See `PROJECT.md` and `PROJECT_CONFIGURATION.md` for Project contracts.
