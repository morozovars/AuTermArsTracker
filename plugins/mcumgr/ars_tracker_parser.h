#ifndef ARS_TRACKER_PARSER_H
#define ARS_TRACKER_PARSER_H

#include <QString>
#include <QList>
#include <QtGlobal>
#include "ars_tracker_backend.h"

namespace ars_tracker_parser {

struct battery_info_t {
    int volt_mV;
    int cur_mA;
    int soc;
    int fullCap_mAh;
    int remainCap_mAh;
    int t2eMin;
    int t2fMin;
    int availableCap_mAh;
    int temp;
    int cycles;
};

struct memory_usage_t {
    quint64 total_bytes;
    quint64 used_bytes;
    double percent;
};

struct bad_blocks_t {
    int count;
    QList<int> blocks;
    bool count_mismatch;
};

bool parse_meas_ls_output(const QString &shell_output,
                          QList<ars_tracker_session_t> *sessions,
                          QString *error_message);
bool parse_param_sn_output(const QString &shell_output, QString *decoded_serial,
                           QString *error_message);
bool parse_param_bid_output(const QString &shell_output, QString *board_id,
                            QString *error_message);
bool parse_param_type_output(const QString &shell_output, QString *tracker_type,
                             QString *error_message);
bool parse_status_output(const QString &shell_output, QString *tracker_status,
                         QString *error_message);
bool parse_battery_info_output(const QString &shell_output, battery_info_t *battery_info,
                               QString *formatted_value, QString *error_message);
bool parse_memory_usage_output(const QString &shell_output, memory_usage_t *memory_usage,
                               QString *formatted_value, QString *error_message);
bool parse_bad_blocks_output(const QString &shell_output, bad_blocks_t *bad_blocks,
                             QString *formatted_value, QString *error_message);

}

#endif // ARS_TRACKER_PARSER_H
