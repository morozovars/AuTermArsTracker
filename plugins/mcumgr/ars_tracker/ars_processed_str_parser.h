#ifndef ARS_PROCESSED_STR_PARSER_H
#define ARS_PROCESSED_STR_PARSER_H

#include <QString>
#include <QList>
#include <vector>
#include <cstdint>

// TODO(ars): replace with real Algorithms.h include when it is available in this repository.
struct IntegralState
{
		uint32_t timestamp = 0;
		double distance = 0.0;
		double ax = 0.0;
		double ay = 0.0;
		double az = 0.0;
		double yaw = 0.0;
		double pitch = 0.0;
		double roll = 0.0;
		uint32_t step = 0;
		uint32_t load = 0;
};

struct SplashData
{
		uint32_t timestamp = 0;
		uint32_t tPeak = 0;
		double maxAccel = 0.0;
		double energy = 0.0;
		double delta = 0.0;
		double abp = 0.0;
		uint32_t tStart = 0;
		uint32_t tFootStart = 0;
		uint32_t tTouch = 0;
		double postAmp = 0.0;
		double integral = 0.0;
		uint32_t duration = 0;
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
		uint8_t shotType = 0;
};

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
