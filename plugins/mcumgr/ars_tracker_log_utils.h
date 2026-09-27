#ifndef ARS_TRACKER_LOG_UTILS_H
#define ARS_TRACKER_LOG_UTILS_H

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace ars_tracker_log_utils {

bool shell_result_is_success(bool status_complete, bool ret_valid, qint32 ret);
int support_state_after_cd(bool status_complete, bool ret_valid, qint32 ret);
bool support_check_should_defer(bool processor_busy, bool telemetry_busy,
                                bool info_refreshing, bool scan_active);
bool support_context_matches(const QString &port, const QString &serial,
                             quint64 generation, const QString &expected_port,
                             const QString &expected_serial, quint64 expected_generation);
QString extract_absolute_cwd(const QString &shell_output);
bool parse_listing(const QString &shell_output, QStringList *ordered_files,
                   QString *error_message);
QStringList order_log_files(const QStringList &file_names);
void append_downloaded_file(QByteArray *history, const QByteArray &file_data,
                            const QStringList &missing_before = QStringList());
void append_missing_marker(QByteArray *history, const QStringList &missing_files);

} // namespace ars_tracker_log_utils

#endif // ARS_TRACKER_LOG_UTILS_H
