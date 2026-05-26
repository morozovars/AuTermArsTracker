#ifndef ARS_PROCESSED_STR_PARSER_H
#define ARS_PROCESSED_STR_PARSER_H

#include <QString>
#include <QList>
#include <vector>
#include <cstdint>
#include "Algorithms.h"

struct ArsMalformedProcessedStrLine
{
		int lineNumber = 0;
		QString prefix;
		QString reason;
		QString text;
};

struct ArsProcessedStrData
{
		std::vector<IntegralState> integralStates;
		std::vector<SplashData> splashRecords;
		int ignoredLines = 0;
		int malformedLines = 0;
		QList<ArsMalformedProcessedStrLine> malformedLineDetails;
};

class ArsProcessedStrParser
{
public:
		static bool parseFile(const QString &filePath,
													ArsProcessedStrData *out,
													QString *errorMessage = nullptr);
};

#endif // ARS_PROCESSED_STR_PARSER_H
