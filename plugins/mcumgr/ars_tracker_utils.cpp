#include "ars_tracker_utils.h"
#include <QDateTime>
#include <QDebug>
#include <QtMath>
#include "ars_tracker_parser.h"

namespace ars_tracker_utils
{
QString scan_status_to_string(group_status status)
{
    switch (status)
    {
    case STATUS_COMPLETE:
        return "complete";
    case STATUS_ERROR:
        return "error";
    case STATUS_TIMEOUT:
        return "timeout";
    case STATUS_CANCELLED:
        return "cancelled";
    case STATUS_PROCESSOR_TRANSPORT_ERROR:
        return "processor_transport_error";
    default:
        return QString("unknown(%1)").arg(int(status));
    }
}

parsed_status_t parse_status_text(const QString &raw_status)
{
    parsed_status_t parsed_status;
    QString cleaned_status = raw_status.trimmed();
    if (cleaned_status.isEmpty() || cleaned_status.compare("Not loaded", Qt::CaseInsensitive) == 0 ||
        cleaned_status.compare("N/A", Qt::CaseInsensitive) == 0 ||
        cleaned_status.startsWith("Loading", Qt::CaseInsensitive) ||
        cleaned_status.startsWith("Error", Qt::CaseInsensitive))
    {
        return parsed_status;
    }
    QString first_token = cleaned_status.section(',', 0, 0).trimmed();
    bool conversion_ok = false;
    int code = first_token.toInt(&conversion_ok);

    if (conversion_ok == false)
    {
        qWarning() << "ArsTracker status parse failed for raw value" << raw_status;
        return parsed_status;
    }

    parsed_status.code = code;
    switch (code)
    {
    case 0:
        parsed_status.name = "Init";
        parsed_status.color = "808080";
        break;
    case 1:
        parsed_status.name = "Ready";
        parsed_status.color = "1976D2";
        break;
    case 2:
        parsed_status.name = "Active";
        parsed_status.color = "2E7D32";
        break;
    case 3:
        parsed_status.name = "Error";
        parsed_status.color = "D32F2F";
        break;
    default:
        qWarning() << "ArsTracker status code is unknown for raw value" << raw_status
                   << "code" << code;
        break;
    }

    return parsed_status;
}

bool serial_is_valid(const QString &serial_number, QString *error_message)
{
    QString trimmed_serial = serial_number.trimmed();
    QStringList serial_parts = trimmed_serial.split('.');
    if (trimmed_serial.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = "Decoded serial is empty.";
        }
        return false;
    }

    if (serial_parts.length() < 4 || serial_parts.at(0) != "ARS" || serial_parts.at(1) != "1")
    {
        if (error_message != nullptr)
        {
            *error_message = "Decoded serial format is not a supported ArsTracker serial.";
        }
        return false;
    }

    QString side_field = serial_parts.at(2).trimmed();
    QString unique_field = serial_parts.last().trimmed();
    if ((side_field != "1" && side_field != "2") || unique_field.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = "Decoded serial side or unique field is invalid.";
        }
        return false;
    }

    return true;
}

QString device_display_text(const QString &serial_number, const QString &port_name, bool verbose_logs)
{
    QString trimmed_serial = serial_number.trimmed();
    if (trimmed_serial.isEmpty())
    {
        return port_name;
    }

    QStringList serial_parts = trimmed_serial.split('.');
    if (serial_parts.length() >= 4)
    {
        QString tracker_type = serial_parts.at(2).trimmed();
        QString tracker_unique = serial_parts.last().trimmed();
        QString tracker_side;

        if (tracker_type == "1")
        {
            tracker_side = "R";
        }
        else if (tracker_type == "2")
        {
            tracker_side = "L";
        }

        if (tracker_unique.isEmpty() == false && tracker_side.isEmpty() == false)
        {
            QString display_text = tracker_unique % tracker_side;
            if (verbose_logs)
            {
                qDebug() << "ArsTracker UI tracker display parsed. full serial="
                         << trimmed_serial << "unique=" << tracker_unique
                         << "side=" << tracker_side << "display=" << display_text
                         << "port=" << port_name;
            }
            return display_text;
        }
        return trimmed_serial;
    }

    return trimmed_serial;
}

QString pair_id_from_serial(const QString &serial)
{
    QString trimmed_serial = serial.trimmed();
    if (trimmed_serial.isEmpty())
    {
        return QString("-");
    }
    QStringList parts = trimmed_serial.split('.');
    if (parts.length() >= 4)
    {
        QString suffix = parts.last().trimmed();
        return suffix.isEmpty() ? QString("-") : suffix;
    }
    return QString("-");
}

QString side_label_from_serial(const QString &serial)
{
    QStringList parts = serial.trimmed().split('.');
    QString side_token = parts.length() >= 3 ? parts.at(2).trimmed() : QString();
    if (side_token == "1")
    {
        return "Right";
    }
    if (side_token == "2")
    {
        return "Left";
    }
    return "-";
}

QString compact_telemetry_text(const QString &raw_text)
{
    QString cleaned = raw_text;
    cleaned.replace('\r', ' ');
    cleaned.replace('\n', ' ');
    cleaned = cleaned.simplified();
    return cleaned;
}

QString format_battery_compact(const QString &raw_text)
{
    const QString compact = compact_telemetry_text(raw_text);
    ars_tracker_parser::battery_info_t battery_info;
    QString parse_error;
    if (ars_tracker_parser::parse_battery_info_output(
                compact, &battery_info, nullptr, &parse_error))
    {
        return QString("%1 mV  %2%").arg(battery_info.volt_mV).arg(battery_info.soc);
    }

    const QStringList fields = compact.split(',', Qt::KeepEmptyParts);
    if (fields.size() >= 3)
    {
        bool volt_ok = false;
        bool soc_ok = false;
        const int volt_mv = fields.at(0).trimmed().toInt(&volt_ok, 10);
        const int soc = fields.at(2).trimmed().toInt(&soc_ok, 10);
        if (volt_ok && soc_ok)
        {
            return QString("%1 mV  %2%").arg(volt_mv).arg(soc);
        }
    }

    return compact;
}

QString format_memory_compact(const QString &raw_text)
{
    const QString compact = compact_telemetry_text(raw_text);
    ars_tracker_parser::memory_usage_t memory_usage;
    QString parse_error;
    if (ars_tracker_parser::parse_memory_usage_output(
                compact, &memory_usage, nullptr, &parse_error))
    {
        const double total_mb =
                double(memory_usage.total_bytes) / (1024.0 * 1024.0);
        const double used_mb =
                double(memory_usage.used_bytes) / (1024.0 * 1024.0);
        return QString("%1 MB / %2 MB")
                .arg(qRound(total_mb))
                .arg(qRound(used_mb));
    }

    const QStringList fields = compact.split(',', Qt::KeepEmptyParts);
    if (fields.size() >= 2)
    {
        bool total_ok = false;
        bool used_ok = false;
        const quint64 total_bytes = fields.at(0).trimmed().toULongLong(&total_ok, 10);
        const quint64 used_bytes = fields.at(1).trimmed().toULongLong(&used_ok, 10);
        if (total_ok && used_ok)
        {
            const double total_mb = double(total_bytes) / (1024.0 * 1024.0);
            const double used_mb = double(used_bytes) / (1024.0 * 1024.0);
            return QString("%1 MB / %2 MB")
                    .arg(qRound(total_mb))
                    .arg(qRound(used_mb));
        }
    }

    return compact;
}

serial_parts_t parse_serial_parts(const QString &serial)
{
    serial_parts_t parts;
    parts.fullSerial = serial.trimmed();
    QStringList tokens = parts.fullSerial.split('.');
    if (tokens.size() != 4)
    {
        parts.sideName = "Unknown";
        return parts;
    }
    if (tokens.at(0).trimmed().compare("ARS", Qt::CaseInsensitive) != 0)
    {
        parts.sideName = "Unknown";
        return parts;
    }
    parts.sideCode = tokens.at(2).trimmed();
    parts.pairId = tokens.at(3).trimmed();
    if (parts.pairId.isEmpty())
    {
        parts.sideName = "Unknown";
        return parts;
    }
    parts.valid = true;
    if (parts.sideCode == "1")
    {
        parts.isRight = true;
        parts.sideName = "Right";
    }
    else if (parts.sideCode == "2")
    {
        parts.isLeft = true;
        parts.sideName = "Left";
    }
    else
    {
        parts.sideName = "Unknown";
    }
    return parts;
}

QString format_session_display_name(const QString &raw_name)
{
    QString name = raw_name.trimmed();
    if (name.isEmpty())
    {
        return name;
    }
    for (const QChar &ch : name)
    {
        if (ch.isDigit() == false)
        {
            return name;
        }
    }
    bool ok = false;
    qint64 secs = name.toLongLong(&ok, 10);
    if (!ok || secs < 1500000000LL || secs > 2500000000LL)
    {
        return name;
    }
    QDateTime dt = QDateTime::fromSecsSinceEpoch(secs).toLocalTime();
    if (dt.isValid() == false)
    {
        return name;
    }
    return QString("%1 - %2").arg(name, dt.toString("dd MMM yyyy HH:mm"));
}

void append_size_abbreviation(uint32_t size, QString *output)
{
    const QStringList list_abbreviations = { "B", "KiB", "MiB", "GiB", "TiB" };
    float converted_size = size;
    uint8_t abbreviation_index = 0;

    while (converted_size >= 1024 && abbreviation_index < list_abbreviations.size())
    {
        converted_size /= 1024.0;
        ++abbreviation_index;
    }

    output->append(
            QString::number(converted_size, 'g', 3).append(list_abbreviations.at(abbreviation_index)));
}
}
