#ifndef ARS_SESSION_PROCESSING_LOADER_H
#define ARS_SESSION_PROCESSING_LOADER_H

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

#include "ars_processed_str_parser.h"

struct ArsFootProcessedData
{
		QString trackerFolderName;
		QString processedStrPath;
		ArsProcessedStrData data;
};

struct ArsPairProcessedData
{
		QString pairSerial;
		std::optional<ArsFootProcessedData> left;
		std::optional<ArsFootProcessedData> right;
};

class ArsSessionProcessingLoader
{
public:
		static QList<ArsPairProcessedData> loadSession(const QString &sessionPath,
																							 QStringList *warnings = nullptr);
};

#endif // ARS_SESSION_PROCESSING_LOADER_H
