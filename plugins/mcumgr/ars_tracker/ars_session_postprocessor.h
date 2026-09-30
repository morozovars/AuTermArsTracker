#ifndef ARS_SESSION_POSTPROCESSOR_H
#define ARS_SESSION_POSTPROCESSOR_H

#include <QList>
#include <QString>
#include <QStringList>
#include <cstdint>

#include "ars_session_processing_loader.h"

// Subfolder of the session postprocessed folder that holds per-segment results.
inline const char *const kArsSegmentsFolderName = "segments";

struct ArsPostprocessTimeRange
{
    uint32_t startTimestamp100ms = 0;
    uint32_t finishTimestamp100ms = 0;
    QString plannedStartTimeText;
    QString plannedFinishTimeText;
};

// Session segment (warm-up, exercise, game) converted to the session-relative tracker time window.
struct ArsSessionSegmentRange
{
    QString name;
    uint32_t startTimestamp100ms = 0;
    uint32_t finishTimestamp100ms = 0;
    QString startTimeText;
    QString finishTimeText;
};

struct ArsPairPostprocessResult
{
    QString pairSerial;
    QString jsonPath;
    QString touchIntensityCsvPath;
    int segmentIndex = -1; // -1 for the whole session period
    QString segmentName;
    bool ok = false;
    QStringList problems;
};

struct ArsSessionPostprocessRequest
{
    QString sessionName;
    QString outputPath;
    ArsPostprocessTimeRange timeRange;
    QList<ArsSessionSegmentRange> segments;
};

class ArsSessionPostprocessor
{
public:
    static ArsPairPostprocessResult processPair(const ArsSessionPostprocessRequest &request,
                                                const ArsPairProcessedData &pairData);
    // Runs the same postprocessing per session segment, reusing already loaded pair data.
    static QList<ArsPairPostprocessResult> processPairSegments(const ArsSessionPostprocessRequest &request,
                                                               const ArsPairProcessedData &pairData);
};

#endif // ARS_SESSION_POSTPROCESSOR_H
