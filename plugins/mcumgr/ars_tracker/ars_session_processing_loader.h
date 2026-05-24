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

struct ArsSessionPairInput
{
		QString pairSerial;
		QString leftTrackerFolderName;
		QString rightTrackerFolderName;
		QString leftProcessedStrPath;
		QString rightProcessedStrPath;
		bool hasLeft = false;
		bool hasRight = false;
};

class ArsSessionProcessingLoader
{
public:
		static QList<ArsSessionPairInput> scanSessionPairs(const QString &sessionPath,
																									 QStringList *warnings = nullptr);
		static ArsPairProcessedData loadPair(const ArsSessionPairInput &pairInput,
																				 QStringList *warnings = nullptr);
		static QList<ArsPairProcessedData> loadSession(const QString &sessionPath,
																							 QStringList *warnings = nullptr);
};

#endif // ARS_SESSION_PROCESSING_LOADER_H
