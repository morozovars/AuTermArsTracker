# Splash postprocessing call chain analysis

## 1. Summary
Most likely orchestration issue is a timestamp unit mismatch in the Process pipeline before `PostProcessing::processSplashData(...)`.

- Integral samples are handled as `timestamp100ms` (session planned window is also in 100ms).
- Splash samples are parsed from `sp:` lines and kept without unit conversion.
- In current Process path, splash filtering is performed against 100ms bounds (`startTimestamp100ms`/`finishTimestamp100ms`).
- If splash `tPeak/timestamp` are in ms (as in reporter `AlgaProcessor` path), many/all splash events are filtered out before postprocessing.
- When splash arrays are empty at postprocessing input, all splash-derived metrics remain zero (`touches*`, `kicksPasses*`, `possessions`, `dribbles*`, `ballDistanceM`, `ballTimeSec`).

## 2. Process entry point

| UI action | function | file | notes |
|---|---|---|---|
| Process button click | `onProcessSessionClicked()` | `plugins/mcumgr/ars_tracker_sessions_tab.cpp` | opens modal processing flow |
| Start flow | `startSessionProcessingFlow()` | `plugins/mcumgr/ars_tracker_sessions_tab.cpp` | computes planned time window, scans pairs |
| Per pair | `processNextSessionPair()` | `plugins/mcumgr/ars_tracker_sessions_tab.cpp` | loads left/right `processedStr.csv`, calls postprocessor |
| Postprocess pair | `ArsSessionPostprocessor::processPair(...)` | `plugins/mcumgr/ars_tracker/ars_session_postprocessor.cpp` | filters integral/splash, invokes ALGA `PostProcessing` |

## 3. Input files and expected layout

| Expected path | Actual Auterm path | Used by | Contains splash | Risk |
|---|---|---|---|---|
| `<session>/<trackerL>/processedStr.csv` and `<session>/<trackerR>/processedStr.csv` | Auterm often uses `<session>/raw/<tracker>/processedStr.csv` | `ArsSessionProcessingLoader::scanSessionPairs` | yes (`sp:` lines) | loader scans only direct subdirs of `sessionPath`, not explicitly `raw/` |
| `processedStr.csv` line `i:` | same | `ArsProcessedStrParser` | no (integral) | timestamps interpreted as 100ms units |
| `processedStr.csv` line `sp:` | same | `ArsProcessedStrParser` | yes (splash) | timestamps kept as-is, unit not normalized in this path |

## 4. Splash read/filter pipeline

| Step | Function/file | Input | Output | Counts available | Notes |
|---|---|---|---|---|---|
| Parse CSV | `ArsProcessedStrParser::parseFile` (`plugins/mcumgr/ars_tracker/ars_processed_str_parser.cpp`) | `processedStr.csv` | `integralStates`, `splashRecords` | yes | `i:` and `sp:` parsed into separate arrays |
| Time window setup | `startSessionProcessingFlow` (`ars_tracker_sessions_tab.cpp`) | planned times + session start | `startTimestamp100ms`, `finishTimestamp100ms` | yes | window explicitly in 100ms |
| Integral filter | `filter_integral_by_timestamp` (`ars_session_postprocessor.cpp`) | integrals + 100ms range | filtered integrals | yes | matches integral unit |
| Splash filter | `filter_splash_by_timestamp` (`ars_session_postprocessor.cpp`) | splash + 100ms range | filtered splash | yes | potential unit mismatch point |
| ALGA call | `PostProcessing::processSplashData` | filtered splash vectors | touches/shots base events | no direct counters, only outputs | if filtered splash empty -> downstream zeros |

## 5. PostProcessing invocation

| Function called | Arguments | Splash passed | Integral passed | Options/thresholds | Notes |
|---|---|---|---|---|---|
| `pp.processIntegralStateReports(...)` | right, left filtered integral vectors | no | yes | internal ALGA thresholds | called in Process path |
| `pp.processSplashData(...)` | right, left filtered splash vectors | yes | no | internal ALGA touch/shot thresholds | called in Process path |
| `pp.buildSummary()` | none | uses postprocessed internal state | uses postprocessed internal state | shot/touch/possession computed inside ALGA | summary written to pair JSON |
| `pp.buildTouchIntensityPerMinute()` | none | yes (via touches) | indirectly (range from distance timeline) | internal moving window | CSV `touchIntensity_<pairId>.csv` writer |

## 6. Metrics output pipeline

| Metric | Produced by | Pair JSON key | SessionData key | Report key | Current status |
|---|---|---|---|---|---|
| touches | `PostProcessingSummary` | `touchesLeft/right/Total` | `metrics.touchesLeft/right/Total` | touches rows/charts | zero when splash filtered out |
| kicks/passes | `PostProcessingSummary` | `kicksPassesLeft/right/Total` | `metrics.shotsLeft/right/Total` | shots in report | zero when splash filtered out |
| shot zones/max | `PostProcessingSummary` | `light/medium/strongKicksCount`, `maxKickForceG` | `weak/medium/strongShots`, `maxShotG` | shot sections | zero when splash filtered out |
| possessions/dribbles | `PostProcessingSummary` | `possessions`, `highSpeedDribblesCount`, `dribblesWithFinalKickCount/%`, `oneTouchPlays`, `twoThreeTouchPossessions`, `moreThanThreeTouchDribbles` | mapped 1:1 to report metrics | possession/dribble sections | zero when splash/touches are empty |
| ball distance/time | `PostProcessingSummary` | `ballDistanceM`, `ballTimeSec` | currently not mapped to reporter metrics object | depends on reporter model usage | can be present in pair JSON, may be omitted in SessionData mapping |
| touch intensity | `pp.buildTouchIntensityPerMinute()` | `touchIntensity_<pairId>.csv` | `result.touchIntensityByMinute[]` | touch-intensity chart | depends on touches not being empty |

## 7. Findings

1. Process pipeline uses `ArsSessionPostprocessor`, not reporter `AlgaProcessor`.
2. Splash is parsed and passed through pipeline; it is not completely skipped by design.
3. Splash filtering is performed using 100ms planned window boundaries.
4. High-probability gap: splash timestamp units in parser path are not normalized to same unit as filter boundaries.
5. Reporter `AlgaProcessor` path explicitly normalizes integral to ms and rebases splash consistently; Process path does not do equivalent conversion.
6. If `leftSplashFiltered/rightSplashFiltered` become 0 while raw splash counts are non-zero, all splash-derived metrics become zero in `summary`.
7. SessionData bridge maps these keys correctly from pair JSON; if pair JSON already has zeros, report will also show zeros.
8. Additional structural risk: loader scans only direct `sessionPath` subdirs for trackers (`.../<tracker>/processedStr.csv`), not explicit `sessionPath/raw/<tracker>`. Depending on real workspace layout, this can select wrong/no inputs.

## 8. Recommended minimal fix

No algorithm changes; only orchestration/data-contract fixes:

1. Normalize splash timestamp units in Process path before `filter_splash_by_timestamp` (same unit as planned window and integrals).
2. Align Process parser behavior with reporter `AlgaProcessor` parsing/normalization contract.
3. Keep filtering in one explicit unit (recommended: ms end-to-end, or strict 100ms end-to-end) and convert once.
4. Add/keep pair-level diagnostics (already added) to confirm where counts drop:
   - raw integral/splash counts and min/max timestamps,
   - filtered integral/splash counts,
   - postprocessing summary splash metrics.
5. Validate loader path policy for `raw/` layout and make it explicit if needed.

## 9. Temporary diagnostics added in code

Added only debug logs (no algorithm changes):

- `plugins/mcumgr/ars_tracker_sessions_tab.cpp`
  - per-pair input file paths
  - parsed raw integral/splash counts and malformed counters

- `plugins/mcumgr/ars_tracker/ars_session_postprocessor.cpp`
  - pair window range + min/max timestamp ranges for integral/splash
  - counts passed to ALGA postprocessing
  - resulting splash-derived metrics from `PostProcessingSummary`

These logs are intended to show exactly at which stage splash-derived metrics become zero.

## 10. Timestamp unit fix

Implemented minimal orchestration fix in `plugins/mcumgr/ars_tracker/ars_session_postprocessor.cpp`:

- `request.timeRange.startTimestamp100ms` / `finishTimestamp100ms` remain unchanged for integral filtering.
- For splash filtering, window is explicitly converted to milliseconds:
  - `splashWindowStartMs = startTimestamp100ms * 100`
  - `splashWindowFinishMs = finishTimestamp100ms * 100`
- `filter_splash_by_timestamp(...)` now compares splash timestamps against ms window.

Rationale:

- In Process path, `IntegralState.timestamp` is treated as 100ms units.
- ALGA splash logic (`PostProcessing::processSplashData`) uses millisecond-scale thresholds and compares event times in ms.
- Comparing splash timestamps directly against 100ms bounds could reject almost all splash events.

Added/updated logs now show both windows and raw ranges in one line per pair:

- `window100ms=[start,finish)`
- `windowMs=[start,finish)`
- `left/rightIntegralRange100ms=[min..max]`
- `left/rightSplashRangeMs=[min..max]`

Verification checklist from logs:

1. `windowMs` overlaps `left/rightSplashRangeMs` for active pairs.
2. `leftSplashRecordsFiltered/rightSplashRecordsFiltered` are no longer near-zero solely due to unit mismatch.
3. `passedToPostProcessing leftSplash/rightSplash` become non-empty where raw splash exists in window.
4. Summary metrics (`touches*`, `kicksPasses*`, `possessions`, `dribbles*`) become non-zero when data and thresholds allow.
