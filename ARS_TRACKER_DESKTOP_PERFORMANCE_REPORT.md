# Ars Tracker Desktop Performance Report

## 1. Executive summary
При ~30 COM-портах основная причина лагов с высокой вероятностью в том, что критический pipeline `scan -> probe(param sn) -> attach persistent -> initial telemetry -> UI sync` выполняется в `Qt mainThread` и в значительной степени последовательно.

Ключевые наблюдения:
- В текущем runtime по умолчанию используется `Inspector old path`, не worker path.
- Startup scan в логе идет последовательно по портам, один `param sn` за раз.
- После scan сразу стартует пакет lightweight telemetry, который тоже проходит через main-thread обработчики и вызывает частые UI updates.
- Есть выраженный UI overupdate: повторные `firmware controls` и `combo unchanged` каждую секунду.

Оценка вероятных причин лагов:
1. Критично: последовательный scan/probe и коммуникация в main thread.
2. Критично: frequent UI sync/repaint (controls/combo/table/info widgets).
3. Важно: последовательный lightweight telemetry pipeline на много устройств.
4. Важно: пер-device readyRead/transport callbacks обрабатываются в GUI потоке при old path.
5. Желательно: логовый поток (если verbose включен) и частые append/update текста.

## 2. Как сейчас работает scan/connection/telemetry pipeline
### Scan
Цепочка вызовов в `plugin_mcumgr.cpp`:
1. `request_ars_tracker_port_scan(...)`.
2. `start_ars_tracker_port_scan()`.
3. Выбор пути:
- `Inspector/Startup context` -> `INSPECTOR old path`.
- `TrackersTab context` -> worker path только если `ARS_TRACKER_SCAN_WORKER=1`.
4. Old path:
- `begin_next_ars_tracker_port_probe()`.
- `configure_ars_tracker_scan_serial_port(...)`, `QSerialPort::open`.
- `send_ars_tracker_port_probe_command()` -> shell `param sn`.
- callback `handle_ars_tracker_scan_shell_status(...)`.
- `complete_ars_tracker_port_probe(...)`.
- loop к следующему порту.
5. `finish_ars_tracker_port_scan(...)`.

### Connection attach
Для найденного устройства:
- `attach_persistent_ars_tracker_device(...)`.
- При default (`deviceWorkers=0`) создается persistent `QSerialPort`, `smp_uart_auterm`, `smp_processor`, `smp_group_*` в UI-объекте и callbacks идут в GUI thread.

### Initial telemetry
- Во время scan устройства добавляются в pending.
- На finish scan: `start_pending_ars_tracker_initial_lightweight_telemetry()`.
- Enqueue `status`, `bat i`, `mem i`.
- `start_next_ars_tracker_lightweight_telemetry_command()` выполняет команды последовательно.

## 3. Timeline по auterm_debug.txt
Источник: `build/Desktop_Qt_6_9_2_llvm_mingw_64_bit-Debug/debug/auterm_debug.txt`.

Факты:
- Config: `autoStartupScan=1 scanWorker=0 deviceWorkers=0 allowOldMainThreadTransport=1 startupContext=Inspector`.
- Startup auto scan start: `18:02:46.741`.
- Scan request: `18:02:47.203`.
- Scan path: `scan using INSPECTOR old path ... ports=25`.
- `param sn` отправок: `25`.
- Средний интервал между отправками: `~504.6 ms`.
- Минимальный интервал: `409 ms`.
- Максимальный интервал: `2426 ms` (таймаут/медленный порт).
- Длительность scan (start->finish): `~12998 ms`.
- Finish: `found=24`.
- Initial telemetry batch: `pendingCount=21`, `enqueued=21`, `skipped=0`.
- Lightweight telemetry responses в логе: `72`.

Показатель thread affinity по логу:
- `name=Qt mainThread`: `1081 / 1084` строк (`99.7%`).

## 4. Что выполняется в GUI thread
По коду и логам в default runtime:
- COM scan/probe open/send/receive/timeout callbacks.
- Persistent device `readyRead` callback (old path).
- `smp_uart_auterm::serial_read` и `smp_processor::message_received` через соединения на GUI объект.
- attach/release device resources.
- lightweight telemetry scheduling/response handling.
- `populate_ars_tracker_serial_ports(...)`.
- `update_ars_tracker_firmware_upload_controls(...)`.
- `ars_tracker_info_changed(...)` и массовые `QLineEdit/QLabel` обновления.
- `schedule_ars_trackers_table_refresh(...)` и refresh таблицы.

Подтверждения:
- Лог `tid=... name=Qt mainThread` на scan/probe/SMP/telemetry/UI строках.
- В old attach path `connect(serialPort->readyRead, this, lambda...)` в `plugin_mcumgr.cpp`.

## 5. Что уже вынесено в workers, но возможно не используется
### Есть в коде
- `ArsTrackerPortScanWorker` (`ars_tracker_port_scan_worker.*`): локальный `QSerialPort` + local SMP probe в отдельном thread.
- `ArsTrackerDeviceWorker` (`ars_tracker_device_worker.*`): persistent per-device serial/SMP/telemetry в worker thread.

### Фактический runtime (данный лог)
- Worker scan не используется: `scanWorker=0`, нет `scan using WORKER path`, нет worker-start записей.
- Device workers не используются: `deviceWorkers=0`.
- Используется stable old main-thread transport path.

## 6. Главные bottlenecks
### Критично
1. Sequential scan/probe в GUI thread.
2. Persistent serial/SMP callback path в GUI thread (при old transport).
3. UI overupdate после scan и в steady-state.

### Важно
4. Lightweight telemetry очередь на десятки устройств, вызовы/ответы идут плотным потоком в main thread.
5. Частые control-sync (`update_ars_tracker_firmware_upload_controls`) и combo refresh even when unchanged.

### Желательно
6. Логовый overhead при verbose/debug режиме и потенциальный UI-log append cost.

## 7. Подтверждения из кода: file/function/примерная строка
- `plugins/mcumgr/plugin_mcumgr.cpp`:
1. `request_ars_tracker_port_scan` (~7645+).
2. `start_ars_tracker_port_scan` (~10900+).
3. old scan: `begin_next_ars_tracker_port_probe`, `send_ars_tracker_port_probe_command`, `handle_ars_tracker_scan_shell_status`, `complete_ars_tracker_port_probe` (~11300-12300).
4. finish hook: `finish_ars_tracker_port_scan` (~11200+).
5. telemetry queue: `enqueue_ars_tracker_initial_lightweight_telemetry`, `start_pending_...`, `start_next_...` (~8150-8600).
6. attach old persistent path: `attach_persistent_ars_tracker_device` (old branch with `new QSerialPort(this)` and `readyRead` lambda) (~10680+).
7. UI update hot points: `populate_ars_tracker_serial_ports` (~10300+), `update_ars_tracker_firmware_upload_controls` (~17560+), `ars_tracker_info_changed` (~17600+), table refresh scheduling (~7100+, ~8800+).

- `plugins/mcumgr/ars_tracker_port_scan_worker.cpp`:
1. `startScan`, `probePort` (локальный serial + event loop + shell status).

- `plugins/mcumgr/ars_tracker_device_worker.cpp`:
1. `start` creates serial/transport/processor in worker thread.
2. `requestStatus/Battery/Memory` and shell callback path.

## 8. Подтверждения из логов
- Startup config line показывает runtime defaults и route.
- `scan using INSPECTOR old path ... ports=25`.
- 25 последовательных `port scan sending param sn to COM...`.
- `scan finished found=24`.
- `Lightweight telemetry initial batch start/done` сразу после finish.
- Далее поток повторных UI строк:
1. `ArsTracker UI port combo unchanged, skip repopulate`.
2. `ArsTracker firmware UI controls: ...` (много раз).

Счётчики из текущего файла лога:
- `combo unchanged` строк: `117`.
- `firmware UI controls` строк: `269`.

## 9. Риски текущей архитектуры при 30 COM-портах
- Один slow/timeout порт удлиняет общий scan (последовательный pipeline).
- Пики readyRead/telemetry совпадают с UI sync и приводят к event loop jitter.
- Массовые UI updates на каждую мелочь ухудшают отзывчивость даже при уже найденных устройствах.
- Смешение Inspector и Trackers responsibilities в одном heavy UI plugin увеличивает coupling и вероятность regressions.

## 10. Рекомендуемая целевая архитектура
- Scan: worker pool с ограниченной параллельностью (4-8 одновременно), per-port timeout, aggregated results.
- Persistent IO: per-device worker (или bounded worker pool) для serial/SMP обработки.
- Main thread: только snapshot-model updates и coalesced UI repaint.
- Scheduler:
1. Per-device command queue (strict in-order per tracker).
2. Cross-device fairness with rate limit + jitter.
- UI:
1. debounce/coalesce 100-250 ms для controls/table/combo.
2. forbid full rebuild unless topology changed.
- Inspector vs Trackers:
1. изолировать active single-device operations от multi-device polling.

## 11. Быстрые фиксы на 1-2 часа
1. Урезать UI overupdate:
- пропускать `update_ars_tracker_firmware_upload_controls` если вычисленное состояние не изменилось.
- не трогать combo/table если snapshot не менялся.
2. Поднять debounce для table/controls до 150-250 ms.
3. Отключить лишние debug trace по умолчанию (уже частично сделано).
4. Убедиться, что startup-rescan отключен default (уже так).

## 12. Средние фиксы на 1 день
1. Включить worker scan как controlled rollout (`ARS_TRACKER_SCAN_WORKER=1`) и стабилизировать именно startup/trackers context.
2. Включить device workers (`ARS_TRACKER_DEVICE_WORKERS=1`) на limited cohort, добавить failover на old path per port.
3. Перенести lightweight telemetry transport-level работу в worker path, в GUI передавать только compact parsed values.
4. Добавить строгий coalescing UI sync функций (`sync_*`, `update_*`) через общий throttled dispatcher.

## 13. Более правильный рефакторинг на 2-4 дня
1. Вынести tracker runtime model в отдельный backend слой (thread-safe snapshots).
2. Ввести command scheduler:
- per-device FIFO;
- global concurrency budget;
- cancellation/backoff policy.
3. Разделить pipelines:
- discovery/probe;
- persistent telemetry;
- sessions/download/firmware.
4. Перейти от widget-by-widget churn к model-driven update (в т.ч. для Trackers table).

## 14. План следующего patch-а (первый приоритет)
1. Не менять бизнес-логику команд.
2. Сделать только UI-overupdate patch:
- guard-by-state для `update_ars_tracker_firmware_upload_controls`;
- guard-by-hash для `populate_ars_tracker_serial_ports`;
- единый debounce timer для `sync_ars_tracker_serial_controls` + table refresh.
3. Добавить метрики:
- вызовы/сек этих функций;
- время выполнения;
- event-loop lag correlation.
4. Проверка на стенде 30 COM:
- startup scan latency;
- GUI responsiveness during/after scan;
- частота UI updates в логе.

---

### Дополнительно: вероятности гипотез
- Последовательная коммуникация + main-thread execution как основной фактор лагов: **High (0.8-0.9)**.
- UI overupdate как значимый второй фактор: **High (0.7-0.85)**.
- Логирование как доминирующий фактор в текущем stable режиме: **Medium (0.3-0.5)**.
- Проблема только в scan, без post-scan telemetry/UI: **Low-Medium (0.25-0.4)**.
