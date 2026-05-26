#ifndef ARS_SESSION_POSTPROCESSOR_H
#define ARS_SESSION_POSTPROCESSOR_H

#include <QString>
#include <QStringList>
#include <cstdint>

#include "ars_session_processing_loader.h"

struct ArsPostprocessTimeRange
{
    uint32_t startTimestamp100ms = 0;
    uint32_t finishTimestamp100ms = 0;
    QString plannedStartTimeText;
    QString plannedFinishTimeText;
};

struct ArsPairPostprocessResult
{
    QString pairSerial;
    QString jsonPath;
    QString touchIntensityCsvPath;
    bool ok = false;
    QStringList problems;
};

struct ArsSessionPostprocessRequest
{
    QString sessionName;
    QString outputPath;
    ArsPostprocessTimeRange timeRange;
};

class ArsSessionPostprocessor
{
public:
    static ArsPairPostprocessResult processPair(const ArsSessionPostprocessRequest &request,
                                                const ArsPairProcessedData &pairData);
};

#endif // ARS_SESSION_POSTPROCESSOR_H
