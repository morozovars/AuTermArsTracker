#ifndef ARS_SESSION_DURATION_SCANNER_H
#define ARS_SESSION_DURATION_SCANNER_H

#include <QString>
#include <QStringList>
#include <cstdint>

class ArsSessionDurationScanner
{
public:
		static bool scanSessionDuration(const QString &sessionPath,
																		uint32_t *outMaxTimestamp100ms,
																		bool *outHasTimestamp,
																		QStringList *warnings = nullptr);
};

#endif // ARS_SESSION_DURATION_SCANNER_H
