#ifndef ARS_TRACKER_LOG_UTILS_H
#define ARS_TRACKER_LOG_UTILS_H

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace ars_tracker_log_utils {

bool shell_result_is_success(bool status_complete, bool ret_valid, qint32 ret);

enum support_flow_state_t {
    SUPPORT_FLOW_IDLE = 0,
    SUPPORT_FLOW_RESET_CWD_BEFORE_PROBE,
    SUPPORT_FLOW_CHECK_LOGS_DIRECTORY,
    SUPPORT_FLOW_RESET_CWD_AFTER_PROBE,
    SUPPORT_FLOW_PREPARE_LOGS,
    SUPPORT_FLOW_FINISHED,
};

enum support_flow_command_t {
    SUPPORT_COMMAND_NONE = 0,
    SUPPORT_COMMAND_CHECK_LOGS_DIRECTORY,
    SUPPORT_COMMAND_RESET_CWD_AFTER_PROBE,
    SUPPORT_COMMAND_PREPARE_LOGS,
};

struct support_flow_transition_t {
    support_flow_state_t next_state = SUPPORT_FLOW_FINISHED;
    support_flow_command_t next_command = SUPPORT_COMMAND_NONE;
    bool mark_available = false;
    bool mark_unsupported = false;
};

support_flow_transition_t support_flow_after_callback(support_flow_state_t state,
                                                      bool command_success,
                                                      bool context_valid = true);
bool load_should_start_listing(bool prepare_success, bool context_valid);
bool support_check_should_defer(bool processor_busy, bool telemetry_busy,
                                bool info_refreshing, bool scan_active);
bool support_context_matches(const QString &port, const QString &serial,
                             quint64 generation, const QString &expected_port,
                             const QString &expected_serial, quint64 expected_generation);
bool parse_listing(const QString &shell_output, QStringList *ordered_files,
                   QString *error_message);
QStringList order_log_files(const QStringList &file_names);
void append_downloaded_file(QByteArray *history, const QByteArray &file_data,
                            const QStringList &missing_before = QStringList());
void append_missing_marker(QByteArray *history, const QStringList &missing_files);

} // namespace ars_tracker_log_utils

#endif // ARS_TRACKER_LOG_UTILS_H
