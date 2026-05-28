# Report data contract analysis

## 1. Reporter expected inputs

| File/path | Producer | Consumer | Required/optional | Purpose | Missing-file behavior |
|---|---|---|---|---|---|
| `<session>/SessionInfo.json` | Full-data producer (outside reporter), optional overlay in export mode | `loadFullDataSession()`, `overlaySessionInfo()` in `ars_reporter/src/main.cpp` | Required in `full-data-session`; optional in `export-session`; optional in discovered mode | Session metadata, planned metrics, settings, exercises | In full-data mode: fail (parse/read error). In export mode: ignored if absent. |
| `<session>/TeamInfo.json` | Full-data producer | `loadFullDataSession()`, `overlayTeamInfo()` | Required in `full-data-session`; optional in export mode | Team metadata and image paths | In full-data mode: fail. In export mode: fallback team used. |
| `<session>/PlayerInfo.json` | Full-data producer | `loadFullDataSession()`, `readPlayerInfoOverlay()` | Required in `full-data-session`; optional in export mode | Players metadata | In full-data mode: fail. In export mode: fallback single player used. |
| `<session>/PlayerTrackersPairs.json` | Full-data producer | `loadFullDataSession()`, `parsePairs()` | Required in `full-data-session`; optional in export mode | Mapping playerId -> trackerL/trackerR | In full-data mode: fail. In export mode: fallback `ldata.csv/rdata.csv` or single-player mapping. |
| `<session>/<trackerL>/processedStr.csv` and `<session>/<trackerR>/processedStr.csv` | Preprocessed tracker export | `AlgaProcessor::processPlayer*()` call chain in `main.cpp` | Required for full-data/discovered/collection modes per pair | Source timeseries for metrics/events | Missing pair -> result with `status="error"` / processingErrors, partial output continues. |
| `<input>/<exportId>.json` (export manifest) + `<input>/<exportId>/ldata.csv`, `<input>/<exportId>/rdata.csv` | Export-session source | `findExportJsonFile()`, `loadExportSession()` | Required in `export-session` mode | Export format ingestion | Missing -> export mode not detected or hard error if detected but files absent. |
| `<session>/postprocessed/SessionData.json` | Not produced by reporter automatically in UI path; expected by current AuTerm integration | `ArsTrackerSessionsTab::onGeneratePdfClicked()` in `plugins/mcumgr/ars_tracker_sessions_tab.cpp` | Required by current UI integration only | Prebuilt reporter data model for direct `generatePdfReport(...)` call | If absent: current UI shows "SessionData.json not found ... Run Process first." |
| `<assetRoot>/assets/...` images (`title`, `team_logo`, `ars_logo`, `header_bg`, `players/*`) | Optional asset pack | `ReportLayout.cpp` asset resolution | Optional | Branding/photos in PDF/DOCX | Missing images do not fail report; blocks become empty/fallback. |
| Font files from search paths (`assets/fonts`, `fonts`, env `ARS_REPORT_FONT_DIR`) | Optional | `report/ReportFonts.cpp` | Optional | Font styling fidelity | Missing fonts: fallback system fonts; report still generated. |
| Microcycle: `<microcycle>/MicrocycleInfo.json` | Microcycle producer | `readMicrocycleInfo()` | Required in `microcycle` mode | Core microcycle config | Missing -> hard error "MicrocycleInfo.json is required for microcycle report". |
| Microcycle optional: `TeamInfo.json`, `PlayerInfo.json`, `PlayerTrackersPairs.json`, `CoachConclusions.json`, session subfolders | Microcycle producer | `runMicrocycleReport()` | Optional (except sessions data must exist) | Enriched microcycle output | Missing optional files -> fallback and processingErrors entries. |

### SessionData.json path defaults inside reporter

- `defaultSessionDataPath(pdfPath)` in `ars_reporter/src/main.cpp` returns `<dir_of_pdf>/SessionData.json`.
- `resolveOutputPaths(...)` returns:
  - if `--out` is pdf/docx file path: `SessionData.json` near that file;
  - if `--out` is directory: `<out>/<yyyy-MM-dd_HH-mm-ss>/SessionData.json`.
- `runSessionReport(...)` always calls `writeSessionData(...)` for `pdf/docx/json/all`, i.e. JSON artifact is always emitted in CLI flow.

## 2. SessionData.json schema expected by reporter

Source of truth: `ars_reporter/src/SessionModels.h`, `SessionModels.cpp`, `SessionDataWriter.cpp`.

| JSON path | C++ model field | Type | Required/optional | Used in PDF: yes/no | Notes |
|---|---|---|---|---|---|
| `reportType` | constant | string | optional on read, written as `"session"` | no | Metadata marker. |
| `session.sessionId` | `SessionData::session.sessionId` | string | effectively required (logic relies on it) | yes | Parsed as optional in `readSessionData`, strict in `parseSessionInfo` path. |
| `session.timestamp` | `session.timestamp` | number | optional | limited | Used in output naming/time fallback. |
| `session.date` | `session.date` | string | optional | yes | Cover/date fields. |
| `session.startTime` | `session.startTime` | string | optional | yes | Time range and exercise interval logic. |
| `session.endTime` | `session.endTime` | string | optional | yes | Same. |
| `session.type` | `session.type` | string | optional | yes | Display. |
| `session.teamId` | `session.teamId` | string | optional | limited | Links/team label. |
| `session.location` | `session.location` | string | optional | yes | Display. |
| `session.field.widthM/lengthM` | `session.field` | number | optional | yes | Field size display. |
| `session.goals[]` | `session.goals` | array[string] | optional | yes | Display section. |
| `session.plannedMetrics.*` | `session.planned` + has* flags | numbers | optional | yes | Plan/fact charts/tables. Missing -> `notAvailable`. |
| `session.calculationSettings.*` | `session.settings` | object | optional but always written | yes | Thresholds for text and some calculations. |
| `session.exercises[]` | `session.exercises` | array[object] | optional | yes | Exercise pages/charts. |
| `team.teamId/name/ageCategory/logoPath/titleImagePath` | `SessionData::team` | strings | optional (except logical teamId) | yes | Branding and header blocks. |
| `team.coaches[]` | `team.coaches` | array | optional | yes | Cover section. |
| `players[]` | `players` | array | expected | yes | Personal/team pages iterate players list. |
| `players[].playerId` | `PlayerInfo.playerId` | string | required logically | yes | Primary join key with results. |
| `players[].fullName/number/...` | `PlayerInfo` | mixed | optional | yes | Missing tolerated, shown as `notAvailable`. |
| `results[]` | `results` | array | expected | yes | Core per-player metrics source. |
| `results[].playerId` | `PlayerSessionResult.playerId` | string | required logically | yes | Join with players. |
| `results[].status` | `result.status` | string | optional (defaults empty) | yes | `ok/error/notAvailable` display. |
| `results[].error` | `result.error` | string | optional | yes | Error blocks. |
| `results[].metrics.*` | `PlayerSessionMetrics` | mixed numbers/null | optional by field | yes | Missing fields supported via availability flags/null. |
| `results[].touchIntensityByMinute[]` | `touchIntensityByMinute` | array | optional | yes | Line chart data. |
| `results[].finishingDribbles[]` | `finishingDribbles` | array | optional | yes | Event tables/charts. |
| `results[].possessionEvents[]` | `possessionEvents` | array | optional | yes | Event tables/charts. |
| `results[].shotEvents[]` | `shotEvents` | array | optional | yes | Shot/event visualizations. |
| `teamSummary.*` | `teamSummary` | object | optional | yes | If absent/empty, recomputed by `computeTeamSummary(data)`. |
| `planComparison` | `planComparison` | object or array form | optional | yes | If absent, recomputed by `buildPlanComparison(data)`. |
| `exerciseStats[]` | `exerciseStats` | array | optional | yes | Team exercise pages. |
| `processingErrors[]` | `processingErrors` | array | optional | yes | Final error page. |

### Practical mandatory minimum for non-crashing PDF

- `players[]` and `results[]` should be present and consistent by `playerId` for meaningful report.
- `session.*`, `team.*`, many metric subfields can be absent; renderer prints `notAvailable`.
- If `teamSummary`/`planComparison` absent, reporter recomputes them.

## 3. Current AutermArsTracker produced files

| File/path | Producer code | Format/schema summary | Notes |
|---|---|---|---|
| `<session>/SessionInfo.json` | `plugins/mcumgr/ars_tracker/ars_session_info_json.cpp` (`saveSessionInfoJson`, `updateSessionInfoActualTimeJson`, teamId/assignments methods) | Root fields: `plannedMetrics`, `startTime`, `endTime`, `type`, `location`, `goals`, optional `actualTime`, optional `TeamId`, optional `trackerPlayerBindings` | This is **not** `SessionData.json` schema. |
| `<session>/postprocessed/<pairId>.json` | `plugins/mcumgr/ars_tracker/ars_session_postprocessor.cpp` | Per pair file with `pairSerial`, `sessionName`, `status`, `timeRange`, `input`, `metrics` (PostProcessingSummary keys) | Pair-centric, not player-centric reporter model. |
| `<session>/postprocessed/touchIntensity_<pairId>.csv` | `ars_session_postprocessor.cpp` | CSV columns: `minuteIndex,timestampMs,touchesInMovingMinute` | Useful for intensity chart, but reporter expects per-player merged JSON samples. |
| `<session>/raw/<trackerSerial>/...` | Session download flow + tracker files | Raw/processed tracker files (`processedStr.csv` used by processing loader/postprocessor) | Reporter CLI can process `processedStr.csv` directly when provided in full-data/export formats. |
| In-memory Generate PDF input today | `plugins/mcumgr/ars_tracker_sessions_tab.cpp` | Reads only `postprocessed/SessionData.json`, then `readSessionData()` and `generatePdfReport(...)` | This hard dependency causes current mismatch. |

## 4. Gap analysis

| Reporter expects | Auterm currently provides | Gap | Proposed mapping | Risk |
|---|---|---|---|---|
| Single aggregated `SessionData.json` (session/team/players/results/events) for direct API path | Pair-level `postprocessed/<pair>.json` + `touchIntensity_<pair>.csv` + `SessionInfo.json` | No aggregator file and no reporter schema file at `postprocessed/SessionData.json` | Build compatibility `SessionData.json` before PDF call | Medium: mapping pair->player requires bindings/team context. |
| `players[]` + `results[]` linked by `playerId` | `trackerPlayerBindings` in `SessionInfo.json` (optional), plus pair IDs | Need deterministic player identity mapping | Use `trackerPlayerBindings` as primary mapping, fallback unresolved player placeholders | Medium |
| Rich metrics names (`distanceM`, `shotsTotal`, `weakShots`, etc.) | Postprocessing metrics with different key names (`kicksPassesTotal`, `lightKicksCount`, etc.) | Field-name mismatch and some semantics mismatch | Translation layer from pair metrics -> reporter metrics | Medium |
| Optional event arrays (`shotEvents`, `possessionEvents`, `finishingDribbles`) | Pair JSON currently has mostly summary metrics (no full event arrays) | Event detail may be absent | Fill empty arrays; renderer tolerates `notAvailable` | Low |
| Exercise stats from interval processing | Auterm process step does not emit reporter exercise stats | Missing exercise-specific aggregates | For MVP keep `exerciseStats=[]` | Low |
| CLI mode can consume full-data session folders with `<tracker>/processedStr.csv` | Auterm stores tracker folders under `raw/` and processed artifacts in `postprocessed/` | Layout mismatch for direct `loadInputSession()` usage | Either synthesize compatibility folder or avoid CLI loader in UI path | Medium |

## 5. Recommended integration strategy

### Preferred safe path for current architecture

Generate a **compatibility `SessionData.json`** in `<session>/postprocessed/SessionData.json` before calling direct reporter API.

Why this is safest now:

1. Current UI code already calls `readSessionData(...)` + `generatePdfReport(...)` and only misses the source JSON.
2. No need to refactor reporter input detection or change existing Auterm postprocessing outputs.
3. Keeps MVP local to one adapter step: merge `SessionInfo.json` + `postprocessed/<pair>.json` + optional bindings into reporter schema.

### Alternative paths

- Teach reporter to read Auterm-native session layout (`SessionInfo.json` + `postprocessed/*.json`) directly: larger change in reporter contracts.
- Build full wrapper/service to invoke `loadInputSession()` style processing from raw files: more invasive and slower for MVP.

---

## Why current Generate PDF searches `postprocessed/SessionData.json`

In `plugins/mcumgr/ars_tracker_sessions_tab.cpp` (`onGeneratePdfClicked()`), input is hardcoded:

- `sessionDataPath = <session>/postprocessed/SessionData.json`
- then `readSessionData(sessionDataPath)` and `generatePdfReport(outputPdfPath, data, sessionPath)`.

So the UI path does not run reporter CLI pipeline (`loadInputSession()` + `writeSessionData()`), it expects prebuilt reporter JSON.

## Minimal next step

Implement a small compatibility writer that builds `postprocessed/SessionData.json` from existing Auterm artifacts before PDF generation, then keep current `readSessionData(...) -> generatePdfReport(...)` call unchanged.

## 6. Compatibility SessionData builder

Implemented builder: `plugins/mcumgr/ars_tracker/ars_report_session_data_builder.cpp` with API:

- `ArsReportSessionDataBuildResult buildArsReportSessionDataJson(const QString sessionPath)`

### Inputs used from Auterm-native workspace

- `<session>/SessionInfo.json`
- `<session>/postprocessed/<pairId>.json` (all `*.json` except `SessionData.json`)
- `<session>/postprocessed/touchIntensity_<pairId>.csv` (optional per pair)
- `<workspace>/teams/team_<TeamId>.json` via `ArsTeamRepository`
- `<workspace>/players/<playerId>.json` via `ArsPlayerRepository`
- `<workspace>/tracker_bindings.json` via `ArsTrackerBindingRepository`
- session bindings from `SessionInfo.json.trackerPlayerBindings` via `ArsSessionInfoJson::readSessionPairAssignments`

### Output

- Writes compatibility artifact to:
  - `<session>/postprocessed/SessionData.json`

### Mapping rules implemented

- `SessionInfo.TeamId`/`teamId` -> `session.teamId` (string)
- `trackerPlayerBindings` (session scope) has priority for pair->player binding
- fallback to global `tracker_bindings.json` by `(teamId, pairId)`
- unresolved binding -> synthetic player `pair_<pairId>` with name `Неназначенный игрок <pairId>`
- `team_<id>.json` -> reporter `team` object (`teamId`, `name`, `ageCategory`, `logoPath`, `coaches`)
- `player <id>.json` -> reporter player; `fullName` from `surname + name` when explicit full name is absent
- pair metrics -> reporter `results[].metrics` keys expected by `readSessionData()`
- `touchIntensity_<pairId>.csv` -> `results[].touchIntensityByMinute[]` with `minute`/`touches` (plus source columns for diagnostics)

### Generate PDF integration

- `onGeneratePdfClicked()` now calls builder before `readSessionData(...)`.
- On builder failure: shows `QMessageBox::critical` and exits.
- On success: uses generated `sessionDataPath` and logs warnings in debug output.

## 7. Generate PDF bridge fixes

- Unresolved pairs are now skipped from PDF input:
  - if pair has no resolved player binding (session bindings first, then global tracker bindings), pair is excluded from `players`/`results`.
  - warning is added: `Skipped pair <pairId>: no resolved player binding`.
  - if no assigned players remain, builder returns hard error:
    `No assigned players found for this session. Assign tracker pairs to players before generating PDF.`

- Team logo path resolution is aligned with workspace layout:
  - builder reads team data via `ArsTeamRepository`.
  - `team.logoPath` is preserved as workspace-relative path (for example `teams/assets/team_4/logo.png`) and validated against workspace root.
  - `Generate PDF` now passes workspace root as `assetRoot` to reporter (`generatePdfReport(...)`), so team assets under `workspace/teams/...` are resolvable.

- Touch intensity mapping is fixed:
  - builder reads `postprocessed/touchIntensity_<pairId>.csv` for assigned pairs.
  - mapped into `results[].touchIntensityByMinute` (result-level, not metrics-level).
  - each sample includes reporter-compatible keys (`minute`, `touches`) and source diagnostic keys (`minuteIndex`, `timestampMs`, `touchesInMovingMinute`).
