#ifndef ARS_TRACKER_PARSER_H
#define ARS_TRACKER_PARSER_H

#include <QString>
#include <QList>
#include "ars_tracker_backend.h"

namespace ars_tracker_parser {

bool parse_meas_ls_output(const QString &shell_output,
                          QList<ars_tracker_session_t> *sessions,
                          QString *error_message);

}

#endif // ARS_TRACKER_PARSER_H
