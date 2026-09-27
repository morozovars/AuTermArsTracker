#ifndef SMP_RESPONSE_ERROR_DECODER_H
#define SMP_RESPONSE_ERROR_DECODER_H

#include <QByteArray>
#include <QtGlobal>
#include "smp_error.h"

namespace smp_response_error_decoder {

bool decode(const QByteArray &data, quint8 version, quint16 group, quint8 op,
            quint8 command, smp_error_t *error);

} // namespace smp_response_error_decoder

#endif // SMP_RESPONSE_ERROR_DECODER_H
