#ifndef SMP_SHELL_RESPONSE_PARSER_H
#define SMP_SHELL_RESPONSE_PARSER_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

struct smp_shell_execute_response_t {
    bool valid = false;
    bool ret_valid = false;
    qint32 ret = 0;
    QString output;
};

namespace smp_shell_response_parser {

smp_shell_execute_response_t parse_execute_response(const QByteArray &data);

} // namespace smp_shell_response_parser

#endif // SMP_SHELL_RESPONSE_PARSER_H
