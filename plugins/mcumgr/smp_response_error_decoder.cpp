#include "smp_response_error_decoder.h"

#include <QCborStreamReader>
#include "smp_message.h"

namespace {

constexpr quint16 shell_group_id = 9;
constexpr quint8 shell_execute_command = 0;

bool decode_map(QCborStreamReader &reader, quint8 version, quint16 level,
                QString *parent, smp_error_t *error,
                bool top_level_rc_is_payload)
{
    QString key;
    bool keyset = true;
    while (!reader.lastError() && reader.hasNext())
    {
        if (!keyset && !key.isEmpty())
        {
            key.clear();
        }
        keyset = false;

        switch (reader.type())
        {
        case QCborStreamReader::UnsignedInteger:
        case QCborStreamReader::NegativeInteger:
            if (key == QStringLiteral("rc") && version == 1 && level == 2 &&
                parent != nullptr && *parent == QStringLiteral("err"))
            {
                error->rc = reader.toInteger();
                error->type = SMP_ERROR_RET;
            }
            else if (key == QStringLiteral("group") && version == 1 && level == 2 &&
                     parent != nullptr && *parent == QStringLiteral("err"))
            {
                error->group = reader.toInteger();
                error->type = SMP_ERROR_RET;
            }
            else if (key == QStringLiteral("rc") && level == 1 &&
                     !top_level_rc_is_payload)
            {
                error->rc = reader.toInteger();
                error->type = SMP_ERROR_RC;
            }
            reader.next();
            break;
        case QCborStreamReader::String:
        {
            QString data;
            auto result = reader.readString();
            while (result.status == QCborStreamReader::Ok)
            {
                data.append(result.data);
                result = reader.readString();
            }
            if (result.status == QCborStreamReader::Error)
            {
                return false;
            }
            if (key.isEmpty())
            {
                key = data;
                keyset = true;
            }
            break;
        }
        case QCborStreamReader::Array:
        case QCborStreamReader::Map:
            reader.enterContainer();
            while (reader.lastError() == QCborError::NoError && reader.hasNext())
            {
                if (!decode_map(reader, version, level + 1, &key, error,
                                top_level_rc_is_payload))
                {
                    return false;
                }
            }
            if (reader.lastError() == QCborError::NoError)
            {
                reader.leaveContainer();
            }
            break;
        default:
            reader.next();
            break;
        }
    }
    return reader.lastError() == QCborError::NoError;
}

} // namespace

namespace smp_response_error_decoder {

bool decode(const QByteArray &data, quint8 version, quint16 group, quint8 op,
            quint8 command, smp_error_t *error)
{
    if (error == nullptr)
    {
        return false;
    }
    error->type = SMP_ERROR_NONE;
    error->rc = 0;
    error->group = 0;

    const bool top_level_rc_is_payload = group == shell_group_id &&
            op == SMP_OP_WRITE_RESPONSE && command == shell_execute_command;
    QCborStreamReader reader(data);
    if (!decode_map(reader, version, 0, nullptr, error, top_level_rc_is_payload))
    {
        return false;
    }
    if (error->type != SMP_ERROR_NONE && error->rc == 0)
    {
        error->type = SMP_ERROR_NONE;
    }
    return true;
}

} // namespace smp_response_error_decoder
