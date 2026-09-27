#include "smp_shell_response_parser.h"

#include <QCborMap>
#include <QCborParserError>
#include <QCborValue>
#include <limits>

namespace smp_shell_response_parser {

smp_shell_execute_response_t parse_execute_response(const QByteArray &data)
{
    smp_shell_execute_response_t result;
    QCborParserError parser_error;
    const QCborValue root = QCborValue::fromCbor(data, &parser_error);
    if (parser_error.error != QCborError::NoError || !root.isMap())
    {
        return result;
    }

    const QCborMap map = root.toMap();
    const QCborValue output = map.value(QStringLiteral("o"));
    if (!output.isUndefined())
    {
        if (!output.isString())
        {
            return result;
        }
        result.output = output.toString();
    }

    // Modern Zephyr uses `ret`; legacy shell_mgmt uses top-level `rc`.
    // Only inspect the top-level map so nested `err.rc` remains an SMP error.
    QCborValue return_code = map.value(QStringLiteral("ret"));
    if (return_code.isUndefined())
    {
        return_code = map.value(QStringLiteral("rc"));
    }
    if (!return_code.isUndefined())
    {
        if (!return_code.isInteger())
        {
            return result;
        }
        const qint64 value = return_code.toInteger();
        if (value < std::numeric_limits<qint32>::min() ||
            value > std::numeric_limits<qint32>::max())
        {
            return result;
        }
        result.ret = static_cast<qint32>(value);
        result.ret_valid = true;
    }

    result.valid = true;
    return result;
}

} // namespace smp_shell_response_parser
