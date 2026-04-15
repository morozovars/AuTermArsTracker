#ifndef ARS_TRACKER_PARSER_H
#define ARS_TRACKER_PARSER_H

#include <QString>
#include <QList>
#include "ars_tracker_backend.h"

namespace ars_tracker_parser {

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

}

#endif // ARS_TRACKER_PARSER_H
