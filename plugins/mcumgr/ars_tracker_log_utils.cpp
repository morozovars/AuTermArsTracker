#include "ars_tracker_log_utils.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <algorithm>

namespace {

QString without_ansi(QString text)
{
    static const QRegularExpression ansi(
            QStringLiteral("\\x1B(?:\\[[0-?]*[ -/]*[@-~]|\\][^\\x07]*(?:\\x07|\\x1B\\\\))"));
    text.remove(ansi);
    return text;
}

int log_number(const QString &name)
{
    return name.mid(4, 4).toInt();
}

} // namespace

namespace ars_tracker_log_utils {

bool shell_result_is_success(bool status_complete, bool ret_valid, qint32 ret)
{
    return status_complete && ret_valid && ret == 0;
}

support_flow_transition_t support_flow_after_callback(support_flow_state_t state,
                                                      bool command_success,
                                                      bool context_valid)
{
    support_flow_transition_t result;
    if (!context_valid)
    {
        return result;
    }
    switch (state)
    {
    case SUPPORT_FLOW_RESET_CWD_BEFORE_PROBE:
        result.mark_unsupported = !command_success;
        if (command_success)
        {
            result.next_state = SUPPORT_FLOW_CHECK_LOGS_DIRECTORY;
            result.next_command = SUPPORT_COMMAND_CHECK_LOGS_DIRECTORY;
        }
        return result;
    case SUPPORT_FLOW_CHECK_LOGS_DIRECTORY:
        result.mark_available = command_success;
        result.mark_unsupported = !command_success;
        if (command_success)
        {
            result.next_state = SUPPORT_FLOW_RESET_CWD_AFTER_PROBE;
            result.next_command = SUPPORT_COMMAND_RESET_CWD_AFTER_PROBE;
        }
        return result;
    case SUPPORT_FLOW_RESET_CWD_AFTER_PROBE:
        result.mark_available = true;
        result.next_state = SUPPORT_FLOW_PREPARE_LOGS;
        result.next_command = SUPPORT_COMMAND_PREPARE_LOGS;
        return result;
    case SUPPORT_FLOW_PREPARE_LOGS:
        result.mark_available = true;
        return result;
    default:
        return result;
    }
}

bool load_should_start_listing(bool prepare_success, bool context_valid)
{
    return prepare_success && context_valid;
}

bool support_check_should_defer(bool processor_busy, bool telemetry_busy,
                                bool info_refreshing, bool scan_active)
{
    return processor_busy || telemetry_busy || info_refreshing || scan_active;
}

bool support_context_matches(const QString &port, const QString &serial,
                             quint64 generation, const QString &expected_port,
                             const QString &expected_serial, quint64 expected_generation)
{
    return port.compare(expected_port, Qt::CaseInsensitive) == 0 &&
           serial.compare(expected_serial, Qt::CaseInsensitive) == 0 &&
           generation == expected_generation;
}

controls_state_t controls_state(bool device_present, bool supported, bool busy, bool load_active,
                                bool selected_device_load_active, bool has_text)
{
    controls_state_t result;
    result.load_visible = supported;
    result.save_visible = supported;
    result.load_enabled = supported && !busy && !load_active;
    result.save_enabled = supported && !load_active && has_text;
    result.clear_enabled = device_present && !selected_device_load_active;
    return result;
}

QString device_logs_default_file_name(const QString &serial, const QDateTime &timestamp)
{
    QString safe_serial = serial.trimmed();
    safe_serial.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]+")),
                        QStringLiteral("_"));
    safe_serial.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1F]+")),
                        QStringLiteral("_"));
    while (safe_serial.endsWith(' ') || safe_serial.endsWith('.'))
    {
        safe_serial.chop(1);
    }

    const QString prefix = safe_serial.isEmpty() ? QStringLiteral("ars_tracker_logs_") :
                           QStringLiteral("ars_tracker_%1_logs_").arg(safe_serial);
    return prefix + timestamp.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")) +
           QStringLiteral(".log");
}

QString device_logs_log_filter()
{
    return QStringLiteral("Log files (*.log)");
}

QString device_logs_save_filters()
{
    return device_logs_log_filter() + QStringLiteral(";;Text files (*.txt);;All files (*.*)");
}

QString ensure_device_logs_file_extension(const QString &file_name,
                                          const QString &selected_filter)
{
    if (file_name.isEmpty() || selected_filter != device_logs_log_filter() ||
        !QFileInfo(file_name).suffix().isEmpty())
    {
        return file_name;
    }
    return file_name + QStringLiteral(".log");
}

QString text_document_to_ansi(const QTextDocument *document)
{
    if (document == nullptr)
    {
        return QString();
    }

    QString result;
    bool colour_active = false;
    QColor active_foreground;
    QColor active_background;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next())
    {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
        {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
            {
                continue;
            }

            const QTextCharFormat format = fragment.charFormat();
            const QBrush foreground_brush = format.foreground();
            const QBrush background_brush = format.background();
            const QColor foreground = foreground_brush.style() == Qt::NoBrush ?
                                      QColor() : foreground_brush.color();
            const QColor background = background_brush.style() == Qt::NoBrush ?
                                      QColor() : background_brush.color();
            const bool colours_changed = foreground != active_foreground ||
                                         background != active_background;
            if (colours_changed)
            {
                if (colour_active)
                {
                    result.append(QStringLiteral("\x1b[0m"));
                }
                if (foreground.isValid())
                {
                    result.append(QStringLiteral("\x1b[38;2;%1;%2;%3m")
                                      .arg(foreground.red())
                                      .arg(foreground.green())
                                      .arg(foreground.blue()));
                }
                if (background.isValid())
                {
                    result.append(QStringLiteral("\x1b[48;2;%1;%2;%3m")
                                      .arg(background.red())
                                      .arg(background.green())
                                      .arg(background.blue()));
                }
                active_foreground = foreground;
                active_background = background;
                colour_active = foreground.isValid() || background.isValid();
            }
            result.append(fragment.text());
        }
        if (block.next().isValid())
        {
            if (colour_active)
            {
                result.append(QStringLiteral("\x1b[0m"));
                active_foreground = QColor();
                active_background = QColor();
                colour_active = false;
            }
            result.append('\n');
        }
    }
    if (colour_active)
    {
        result.append(QStringLiteral("\x1b[0m"));
    }
    return result;
}

save_text_result_t save_text_utf8(const QString &file_name, const QString &text)
{
    save_text_result_t result;
    if (file_name.isEmpty())
    {
        return result;
    }

    QSaveFile file(file_name);
    if (!file.open(QIODevice::WriteOnly))
    {
        result.status = SAVE_TEXT_OPEN_ERROR;
        result.error_message = file.errorString();
        return result;
    }

    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size())
    {
        result.status = SAVE_TEXT_WRITE_ERROR;
        result.error_message = file.errorString();
        file.cancelWriting();
        return result;
    }

    if (!file.commit())
    {
        result.status = SAVE_TEXT_COMMIT_ERROR;
        result.error_message = file.errorString();
        return result;
    }

    result.status = SAVE_TEXT_SUCCESS;
    return result;
}

QStringList order_log_files(const QStringList &file_names)
{
    static const QRegularExpression log_name(QStringLiteral("^log\\.[0-9]{4}$"));
    QSet<QString> unique;
    for (const QString &name : file_names)
    {
        if (log_name.match(name).hasMatch())
        {
            unique.insert(name);
        }
    }

    QStringList sorted = unique.values();
    std::sort(sorted.begin(), sorted.end(), [](const QString &left, const QString &right) {
        return log_number(left) < log_number(right);
    });
    if (sorted.size() < 2)
    {
        return sorted;
    }

    int largest_gap = -1;
    int start_index = 0;
    for (int i = 0; i < sorted.size(); ++i)
    {
        const int current = log_number(sorted.at(i));
        const int next = log_number(sorted.at((i + 1) % sorted.size()));
        const int gap = (next - current + 10000) % 10000;
        if (gap > largest_gap)
        {
            largest_gap = gap;
            start_index = (i + 1) % sorted.size();
        }
    }

    QStringList result;
    result.reserve(sorted.size());
    for (int i = 0; i < sorted.size(); ++i)
    {
        result.append(sorted.at((start_index + i) % sorted.size()));
    }
    return result;
}

bool parse_listing(const QString &shell_output, QStringList *ordered_files,
                   QString *error_message)
{
    if (ordered_files == nullptr)
    {
        return false;
    }
    ordered_files->clear();
    if (error_message != nullptr)
    {
        error_message->clear();
    }

    const QString cleaned = without_ansi(shell_output);
    if (cleaned.contains(QChar::Null))
    {
        if (error_message != nullptr)
        {
            *error_message = QStringLiteral("Directory listing contains invalid data.");
        }
        return false;
    }

    QStringList candidates;
    bool saw_nonempty = false;
    const QStringList rows = cleaned.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    for (QString row : rows)
    {
        row = row.trimmed();
        if (row.isEmpty())
        {
            continue;
        }
        saw_nonempty = true;
        if (row == QStringLiteral("fs ls NAND:/logs") ||
            row == QStringLiteral("fs ls /NAND:/logs") || row.endsWith('>') ||
            row.endsWith('#') || row.endsWith('$'))
        {
            continue;
        }

        // The firmware's Zephyr fs shell emits one bare entry name per line and appends '/'
        // to directories. Other well-formed entries are valid but are not log files.
        if (row.contains('/') || row.contains('\\') || row.contains(QRegularExpression(
                    QStringLiteral("[\\x00-\\x1f]"))))
        {
            if (row.endsWith('/') && row.count('/') == 1)
            {
                continue;
            }
            if (error_message != nullptr)
            {
                *error_message = QStringLiteral("Unrecognized directory listing row: %1").arg(row);
            }
            return false;
        }
        candidates.append(row);
    }

    if (saw_nonempty && candidates.isEmpty())
    {
        // Prompt/echo-only output is not a complete directory listing.
        if (error_message != nullptr)
        {
            *error_message = QStringLiteral("Directory listing did not contain entries or an empty response.");
        }
        return false;
    }

    *ordered_files = order_log_files(candidates);
    return true;
}

void append_missing_marker(QByteArray *history, const QStringList &missing_files)
{
    if (history == nullptr || missing_files.isEmpty())
    {
        return;
    }
    history->append("\n[Incomplete log history: missing ");
    history->append(missing_files.join(QStringLiteral(", ")).toUtf8());
    history->append("]\n");
}

void append_downloaded_file(QByteArray *history, const QByteArray &file_data,
                            const QStringList &missing_before)
{
    if (history == nullptr)
    {
        return;
    }
    append_missing_marker(history, missing_before);
    history->append(file_data);
}

} // namespace ars_tracker_log_utils
