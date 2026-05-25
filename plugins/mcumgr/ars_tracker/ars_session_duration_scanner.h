#ifndef ARS_SESSION_DURATION_SCANNER_H
#define ARS_SESSION_DURATION_SCANNER_H

#include <QString>
#include <QStringList>
#include <QList>
#include <cstdint>

struct ArsDurationScanFileInput
{
		QString trackerFolderName;
		QString processedStrPath;
};

struct ArsDurationScanFileResult
{
		QString trackerFolderName;
		QString processedStrPath;
		bool ok = false;
		bool hasTimestamp = false;
		uint32_t maxTimestamp100ms = 0;
		QStringList warnings;
};

class ArsSessionDurationScanner
{
public:
		static QList<ArsDurationScanFileInput> scanDurationInputs(const QString &sessionPath,
																															QStringList *warnings = nullptr);
		static ArsDurationScanFileResult scanDurationFile(const ArsDurationScanFileInput &input);
		static bool scanSessionDuration(const QString &sessionPath,
																		uint32_t *outMaxTimestamp100ms,
																		bool *outHasTimestamp,
																		QStringList *warnings = nullptr);
};

#endif // ARS_SESSION_DURATION_SCANNER_H
