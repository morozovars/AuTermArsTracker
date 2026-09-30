#include "smp_fs_status_response_parser.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborParserError>
#include <QCborValue>
#include <limits>

namespace
{
bool find_file_size(const QCborValue &value, uint32_t *file_size, bool *found,
                    QString *error_message)
{
    if (value.isMap())
    {
        const QCborMap map = value.toMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        {
            if (it.key().isString() && it.key().toString() == QStringLiteral("len"))
            {
                if (!it.value().isInteger())
                {
                    if (error_message != nullptr)
                    {
                        *error_message = QStringLiteral("FS status response contains a non-integer len field.");
                    }
                    return false;
                }

                const qint64 value_size = it.value().toInteger(-1);
                if (value_size < 0 || quint64(value_size) > std::numeric_limits<uint32_t>::max())
                {
                    if (error_message != nullptr)
                    {
                        *error_message = QStringLiteral("FS status response contains an invalid len field.");
                    }
                    return false;
                }

                *file_size = uint32_t(value_size);
                *found = true;
                return true;
            }

            if ((it.value().isMap() || it.value().isArray()) &&
                !find_file_size(it.value(), file_size, found, error_message))
            {
                return false;
            }
            if (*found)
            {
                return true;
            }
        }
        return true;
    }

    if (value.isArray())
    {
        const QCborArray array = value.toArray();
        for (const QCborValue &item : array)
        {
            if (!find_file_size(item, file_size, found, error_message))
            {
                return false;
            }
            if (*found)
            {
                return true;
            }
        }
    }

    return true;
}
}

bool smp_fs_status_response_parser::parse_file_size(const QByteArray &data,
                                                    uint32_t *file_size,
                                                    QString *error_message)
{
    if (file_size == nullptr)
    {
        if (error_message != nullptr)
        {
            *error_message = QStringLiteral("No destination was provided for the FS status size.");
        }
        return false;
    }

    QCborParserError parser_error;
    const QCborValue root = QCborValue::fromCbor(data, &parser_error);
    if (parser_error.error != QCborError::NoError)
    {
        if (error_message != nullptr)
        {
            *error_message = QStringLiteral("Could not decode FS status response: %1")
                                 .arg(parser_error.errorString());
        }
        return false;
    }

    uint32_t parsed_size = 0;
    bool found = false;
    if (!find_file_size(root, &parsed_size, &found, error_message))
    {
        return false;
    }
    if (!found)
    {
        if (error_message != nullptr)
        {
            *error_message = QStringLiteral("FS status response does not contain len.");
        }
        return false;
    }

    *file_size = parsed_size;
    if (error_message != nullptr)
    {
        error_message->clear();
    }
    return true;
}
