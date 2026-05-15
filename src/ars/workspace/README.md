# Ars Tracker Desktop Local Workspace

## Purpose
This module provides a baseline local storage workspace for Ars Tracker Desktop.
It initializes a persistent app-data directory and creates the minimum files needed for future Team, Players, Tracker Bindings, Sessions and Reports workflows.

## Local Storage Layout
Workspace root:
- `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/ars_workspace"`

Created structure:
- `team.json`
- `players.json`
- `tracker_bindings.json`
- `sessions/`

## Default Files
If files are missing, the module creates them with minimum valid JSON:

- `team.json`
```json
{
  "schema_version": 1,
  "team": null
}
```

- `players.json`
```json
{
  "schema_version": 1,
  "players": []
}
```

- `tracker_bindings.json`
```json
{
  "schema_version": 1,
  "bindings": []
}
```

## Current Scope
Implemented now:
- Workspace path resolution and initialization.
- Directory/file creation with logging.
- Minimal repositories for:
  - sessions directory
  - team file
  - players file
  - tracker bindings file

Not implemented yet:
- Reading/updating domain models in JSON.
- Sessions indexing and metadata model.
- Validation/migrations beyond schema_version placeholder.
- Reports generation pipeline.

## Future Usage
The module is intended to become the storage backbone for:
- Team management (`team.json`)
- Players management (`players.json`)
- Player-tracker assignments (`tracker_bindings.json`)
- Downloaded sessions storage (`sessions/`)
- Report generation from local sessions
