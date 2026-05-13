#ifndef ARS_TRACKER_UTILS_H
#define ARS_TRACKER_UTILS_H

#include <QString>
#include <QStringList>
#include "smp_group.h"

namespace ars_tracker_utils
{
struct parsed_status_t
{
    int code = -1;
    QString name = "Unknown";
    QString color = "808080";
};

struct serial_parts_t
{
    bool valid = false;
    QString fullSerial;
    QString pairId;
    QString sideCode;
    QString sideName;
    bool isRight = false;
    bool isLeft = false;
};

QString scan_status_to_string(group_status status);
parsed_status_t parse_status_text(const QString &raw_status);
bool serial_is_valid(const QString &serial_number, QString *error_message);
QString device_display_text(const QString &serial_number, const QString &port_name, bool verbose_logs);
QString pair_id_from_serial(const QString &serial);
QString side_label_from_serial(const QString &serial);
QString compact_telemetry_text(const QString &raw_text);
QString format_battery_compact(const QString &raw_text);
QString format_memory_compact(const QString &raw_text);
serial_parts_t parse_serial_parts(const QString &serial);
QString format_session_display_name(const QString &raw_name);
void append_size_abbreviation(uint32_t size, QString *output);
}

#endif // ARS_TRACKER_UTILS_H
