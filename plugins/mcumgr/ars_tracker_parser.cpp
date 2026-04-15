#include "ars_tracker_parser.h"

#include <QByteArray>
#include <QSet>
#include <QRegularExpression>

static bool is_prompt_line(const QString &line)
{
    if (line.endsWith(">") || line.endsWith("#") || line.endsWith("$"))
    {
        return true;
    }

    return false;
}

static bool ignore_generic_noise_line(const QString &line)
{
    if (line.isEmpty())
    {
        return true;
    }

    if (is_prompt_line(line))
    {
        return true;
    }

    QString lowered = line.toLower();
    if (lowered.startsWith("ok") || lowered.startsWith("status") || lowered.startsWith("loading") ||
        lowered.startsWith("done"))
    {
        return true;
    }

    static const QRegularExpression noise_line("^[\\-=*_\\s]+$");
    if (noise_line.match(line).hasMatch())
    {
        return true;
    }

    return false;
}

static QStringList extract_relevant_lines(const QString &shell_output, const QString &command_echo,
                                          bool ignore_error_lines = false)
{
    QStringList rows = shell_output.split('\n');
    QStringList filtered_rows;

    for (QString row : rows)
    {
        row = row.trimmed();

        if (ignore_generic_noise_line(row))
        {
            continue;
        }

        if (row == command_echo)
        {
            continue;
        }

        if (ignore_error_lines == true && row.toLower().startsWith("error"))
        {
            continue;
        }

        filtered_rows.append(row);
    }

    return filtered_rows;
}

static QStringList extract_status_lines(const QString &shell_output)
{
    QStringList rows = shell_output.split('\n');
    QStringList filtered_rows;

    for (QString row : rows)
    {
        row = row.trimmed();

        if (row.isEmpty() || is_prompt_line(row) || row == "status")
        {
            continue;
        }

        QString lowered = row.toLower();
        if (lowered.startsWith("ok") || lowered.startsWith("loading") ||
            lowered.startsWith("done"))
        {
            continue;
        }

        static const QRegularExpression noise_line("^[\\-=*_\\s]+$");
        if (noise_line.match(row).hasMatch())
        {
            continue;
        }

        filtered_rows.append(row);
    }

    return filtered_rows;
}

bool ars_tracker_parser::parse_meas_ls_output(const QString &shell_output,
                                              QList<ars_tracker_session_t> *sessions,
                                              QString *error_message)
{
    sessions->clear();

    // Assumption: `meas ls` returns one session name/path per line, e.g.:
    // sessionA\nsessionB\n...
    // Prompt/status/noise lines are ignored. Update this parser if tracker format changes.
    QStringList rows = shell_output.split('\n');
    QSet<QString> unique_ids;

    for (QString row : rows)
    {
        row = row.trimmed();

        if (ignore_generic_noise_line(row) || row == "meas ls")
        {
            continue;
        }

        if (unique_ids.contains(row))
        {
            continue;
        }

        unique_ids.insert(row);

        ars_tracker_session_t session;
        session.id = row;
        session.display_name = row;
        session.remote_path_or_name = row;
        session.raw_source_line = row;
        sessions->append(session);
    }

    if (sessions->isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("No sessions were returned by meas ls.");
        }

        return false;
    }

    return true;
}

bool ars_tracker_parser::parse_param_sn_output(const QString &shell_output, QString *decoded_serial,
                                               QString *error_message)
{
    QStringList rows = extract_relevant_lines(shell_output, "param sn");

    if (rows.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Serial number response was empty.");
        }

        return false;
    }

    QString serial_hex = rows.first().trimmed();

    if (serial_hex.length() % 2 != 0)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Malformed serial number: hex string must have even length.");
        }

        return false;
    }

    static const QRegularExpression hex_only("^[0-9A-Fa-f]+$");
    if (hex_only.match(serial_hex).hasMatch() == false)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Malformed serial number: response is not valid hex ASCII.");
        }

        return false;
    }

    QByteArray decoded_bytes = QByteArray::fromHex(serial_hex.toLatin1());

    if (decoded_serial != nullptr)
    {
        *decoded_serial = QString::fromLatin1(decoded_bytes);
    }

    return true;
}

bool ars_tracker_parser::parse_param_bid_output(const QString &shell_output, QString *board_id,
                                                QString *error_message)
{
    QStringList rows = extract_relevant_lines(shell_output, "param bid");

    if (rows.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Board id response was empty.");
        }

        return false;
    }

    QString first_line = rows.first();
    static const QRegularExpression numeric_id("([0-9]+)");
    QRegularExpressionMatch match = numeric_id.match(first_line);

    if (match.hasMatch() == false)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Board id response did not contain a numeric id.");
        }

        return false;
    }

    if (board_id != nullptr)
    {
        *board_id = match.captured(1);
    }

    return true;
}

bool ars_tracker_parser::parse_param_type_output(const QString &shell_output, QString *tracker_type,
                                                 QString *error_message)
{
    QStringList rows = extract_relevant_lines(shell_output, "param type");

    if (rows.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Tracker type response was empty.");
        }

        return false;
    }

    QString token = rows.first().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).value(0);

    if (token.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Tracker type response was empty.");
        }

        return false;
    }

    if (tracker_type != nullptr)
    {
        if (token == "R")
        {
            *tracker_type = QString("R (right)");
        } else if (token == "L")
        {
            *tracker_type = QString("L (left)");
        } else
        {
            *tracker_type = token;
        }
    }

    return true;
}

bool ars_tracker_parser::parse_status_output(const QString &shell_output, QString *tracker_status,
                                             QString *error_message)
{
    QStringList rows = extract_status_lines(shell_output);

    if (rows.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Tracker status response was empty.");
        }

        return false;
    }

    // Assumption: the tracker `status` command may return either a direct state line or
    // key/value lines. Prefer explicit `status`/`state` keys; otherwise fall back to a concise
    // summary of the first useful lines.
    static const QRegularExpression state_line("^(?:status|state)\\s*[:=]\\s*(.+)$",
                                               QRegularExpression::CaseInsensitiveOption);

    for (const QString& row : rows)
    {
        QRegularExpressionMatch match = state_line.match(row);

        if (match.hasMatch())
        {
            if (tracker_status != nullptr)
            {
                *tracker_status = match.captured(1).trimmed();
            }

            return true;
        }
    }

    if (rows.length() == 1)
    {
        if (tracker_status != nullptr)
        {
            *tracker_status = rows.first();
        }

        return true;
    }

    QStringList summary_rows;

    for (int i = 0; i < rows.length() && i < 2; ++i)
    {
        summary_rows.append(rows.at(i));
    }

    if (tracker_status != nullptr)
    {
        *tracker_status = summary_rows.join(" | ");
    }

    return true;
}
