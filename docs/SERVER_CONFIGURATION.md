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

```text
startup = load
    load the persisted Graph directly
    do not perform Project/source change detection

startup = build
    check Project content identity first
    unchanged -> source change detection
    changed   -> stream project.json and update construction state

startup = rebuild
    perform a full G0 rebuild
```

Absent `project`: the Server starts UNLOADED and waits for a command.

Relative Project paths are resolved relative to `server.json`.

All three startup values route to distinct lifecycle pipelines. BUILD currently
stops at the unimplemented persisted-identity stage; REBUILD validates the
streaming Project schema and stops before composition/Source Manager.

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

The TCP backend is not implemented in this step, so configuring it currently fails explicitly instead of being ignored.

## Logging

```jsonc
"logging": {
  "level": "info",
  "console": true,
  "file": "logs/server.log"
}
```

`file` is directly the path. No redundant `enabled` flag exists.

Logging configuration is parsed now; logging sink implementation is a later step.

## Telemetry

```jsonc
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
```

Telemetry configuration is parsed now; telemetry emission is a later step.

The three console meanings remain separate:

```text
communication console -> commands
logging console       -> diagnostic output
telemetry console     -> telemetry output
```

## Configuration ownership boundary

`server.json` configures the Server process and selects the Project startup
operation. LOAD, BUILD, and REBUILD own separate mode-local temporary state;
construction state never belongs to `server_context`.

See `PROJECT.md` and `PROJECT_CONFIGURATION.md` for Project contracts.
