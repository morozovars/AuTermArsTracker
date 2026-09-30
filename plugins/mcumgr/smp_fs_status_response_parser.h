#ifndef SMP_FS_STATUS_RESPONSE_PARSER_H
#define SMP_FS_STATUS_RESPONSE_PARSER_H

#include <QByteArray>
#include <QString>
#include <cstdint>

namespace smp_fs_status_response_parser
{
bool parse_file_size(const QByteArray &data, uint32_t *file_size, QString *error_message);
}

#endif // SMP_FS_STATUS_RESPONSE_PARSER_H
