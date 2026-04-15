#include "ars_tracker_parser.h"

#include <QSet>
#include <QRegularExpression>

static bool ignore_line(const QString &line)
{
    if (line.isEmpty())
    {
        return true;
    }

    // Ignore shell prompt lines, common prompt suffixes, and command echo.
    if (line.endsWith(">") || line.endsWith("#") || line.endsWith("$") || line == "meas ls")
    {
        return true;
    }

    // Ignore obvious informational noise/status lines.
    QString lowered = line.toLower();
    if (lowered.startsWith("ok") ||
        lowered.startsWith("error") ||
        lowered.startsWith("status") ||
        lowered.startsWith("loading") ||
        lowered.startsWith("done"))
    {
        return true;
    }

    // Ignore lines that likely contain separators/banners.
    static const QRegularExpression noise_line("^[\\-=*_\\s]+$");
    if (noise_line.match(line).hasMatch())
    {
        return true;
    }

    return false;
}


static QString extract_session_token(const QString &line)
{
    QString trimmed = line.trimmed();

    if (trimmed.isEmpty())
    {
        return trimmed;
    }

    QStringList parts = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

    if (parts.isEmpty())
    {
        return trimmed;
    }

    QString candidate = parts.last();

    while (candidate.endsWith(':') || candidate.endsWith(',') || candidate.endsWith(';'))
    {
        candidate.chop(1);
    }

    return candidate;
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

        if (ignore_line(row))
        {
            continue;
        }

        QString session_token = extract_session_token(row);

        if (session_token.isEmpty())
        {
            continue;
        }

        if (unique_ids.contains(session_token))
        {
            continue;
        }

        unique_ids.insert(session_token);

        ars_tracker_session_t session;
        session.id = session_token;
        session.display_name = session_token;
        session.session_name = session_token;
        session.remote_fs_root = session_token;
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
