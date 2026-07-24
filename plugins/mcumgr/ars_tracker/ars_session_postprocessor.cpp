#include "ars_session_postprocessor.h"

#include <QChar>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <exception>
#include <limits>

#include "sources/soccer_insole/PostProcessing.h"

namespace
{
QString sanitize_file_stem(const QString &serial)
{
    QString out = serial.trimmed();
    out.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
    return out.isEmpty() ? QString("pair") : out;
}

std::vector<IntegralState> filter_integral_by_timestamp(const std::vector<IntegralState> &input,
                                                        uint32_t start100ms,
                                                        uint32_t finish100ms)
{
    std::vector<IntegralState> out;
    out.reserve(input.size());
    for (const IntegralState &state : input)
    {
        if (state.timestamp >= start100ms && state.timestamp < finish100ms)
        {
            out.push_back(state);
        }
    }
    return out;
}

std::vector<SplashData> filter_splash_by_timestamp(const std::vector<SplashData> &input,
                                                   uint32_t startMs,
                                                   uint32_t finishMs)
{
    std::vector<SplashData> out;
    out.reserve(input.size());
    for (const SplashData &state : input)
    {
        if (state.timestamp >= startMs && state.timestamp < finishMs)
        {
            out.push_back(state);
        }
    }
    return out;
}

bool write_text_file(const QString &path, const QString &text, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (error != nullptr)
        {
            *error = QString("Failed to open file for write: %1").arg(path);
        }
        return false;
    }
    QTextStream stream(&file);
    stream << text;
    return true;
}

QJsonObject summary_to_json(const PostProcessingSummary &summary)
{
    QJsonObject o;
    o["distanceKm"] = summary.distanceKm;
    o["durationSec"] = summary.durationSec;
    o["maxSpeedMs"] = summary.maxSpeedMs;
    o["maxSpeedKmh"] = summary.maxSpeedKmh;
    o["avgSpeedMs"] = summary.avgSpeedMs;
    o["avgSpeedKmh"] = summary.avgSpeedKmh;
    o["accelerationCount"] = summary.accelerationCount;
    o["accelerationDistanceM"] = summary.accelerationDistanceM;
    o["steps"] = summary.steps;
    o["footloadLeft"] = summary.footloadLeft;
    o["footloadRight"] = summary.footloadRight;
    o["footloadTotal"] = summary.footloadTotal;
    o["loadIntensityLeft"] = summary.loadIntensityLeft;
    o["loadIntensityRight"] = summary.loadIntensityRight;
    o["loadDisbalancePercent"] = summary.loadDisbalancePercent;
    o["loadDisbalanceSide"] = QString::fromStdString(summary.loadDisbalanceSide);
    o["kicksPassesLeft"] = summary.kicksPassesLeft;
    o["kicksPassesRight"] = summary.kicksPassesRight;
    o["kicksPassesTotal"] = summary.kicksPassesTotal;
    o["lightKicksCount"] = summary.lightKicksCount;
    o["mediumKicksCount"] = summary.mediumKicksCount;
    o["strongKicksCount"] = summary.strongKicksCount;
    o["maxKickForceG"] = summary.maxKickForceG;
    o["touchesLeft"] = summary.touchesLeft;
    o["touchesRight"] = summary.touchesRight;
    o["touchesTotal"] = summary.touchesTotal;
    o["possessions"] = summary.possessions;
    o["highSpeedDribblesCount"] = summary.highSpeedDribblesCount;
    o["dribblesWithFinalKickCount"] = summary.dribblesWithFinalKickCount;
    o["dribblesWithFinalKickPercent"] = summary.dribblesWithFinalKickPercent;
    o["ballDistanceM"] = summary.ballDistanceM;
    o["ballTimeSec"] = summary.ballTimeSec;
    o["oneTouchPlays"] = summary.oneTouchPlays;
    o["twoThreeTouchPossessions"] = summary.twoThreeTouchPossessions;
    o["moreThanThreeTouchDribbles"] = summary.moreThanThreeTouchDribbles;
    return o;
}

template <typename T, typename F>
QString range_to_text(const std::vector<T> &items, F valueFn)
{
    if (items.empty())
    {
        return "empty";
    }
    uint64_t minValue = std::numeric_limits<uint64_t>::max();
    uint64_t maxValue = 0;
    for (const T &item : items)
    {
        const uint64_t value = static_cast<uint64_t>(valueFn(item));
        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
    }
    return QString("[%1..%2]").arg(minValue).arg(maxValue);
}

// One postprocessing window: either the whole planned session period or a single session segment.
struct ArsPostprocessWindow
{
    QString sessionName;
    QString outputPath;
    QString fileSuffix;    // empty for the session window, "__segNN" for a segment
    QString segmentName;   // empty for the session window
    int segmentIndex = -1; // -1 for the session window
    uint32_t startTimestamp100ms = 0;
    uint32_t finishTimestamp100ms = 0;
    QString startTimeText;
    QString finishTimeText;
};

QString window_label(const ArsPostprocessWindow &window, const QString &pairSerial)
{
    return window.segmentIndex < 0
               ? QString("pair %1").arg(pairSerial)
               : QString("pair %1 segment \"%2\"").arg(pairSerial, window.segmentName);
}

ArsPairPostprocessResult run_postprocess_window(const ArsPostprocessWindow &window,
                                                const ArsPairProcessedData &pairData)
{
    ArsPairPostprocessResult out;
    out.pairSerial = pairData.pairSerial;
    out.segmentIndex = window.segmentIndex;
    out.segmentName = window.segmentName;
    const QString label = window_label(window, pairData.pairSerial);

    if (!pairData.left.has_value() || !pairData.right.has_value())
    {
        out.problems.append(QString("%1: missing left/right tracker data").arg(label));
        return out;
    }

    const ArsProcessedStrData &leftData = pairData.left->data;
    const ArsProcessedStrData &rightData = pairData.right->data;

    // Time unit contract in Process path:
    // - planned window in request is kept in timestamp100ms (session-relative),
    // - integralState.timestamp is timestamp100ms,
    // - splash.timestamp/tPeak is milliseconds (session-relative), same as ALGA splash logic expects.
    // Therefore splash filtering must use ms window derived from 100ms request bounds.
    const uint32_t splashWindowStartMs = window.startTimestamp100ms * 100u;
    const uint32_t splashWindowFinishMs = window.finishTimestamp100ms * 100u;

    const std::vector<IntegralState> leftFiltered =
        filter_integral_by_timestamp(leftData.integralStates, window.startTimestamp100ms, window.finishTimestamp100ms);
    const std::vector<IntegralState> rightFiltered =
        filter_integral_by_timestamp(rightData.integralStates, window.startTimestamp100ms, window.finishTimestamp100ms);
    const std::vector<SplashData> leftSplashFiltered =
        filter_splash_by_timestamp(leftData.splashRecords, splashWindowStartMs, splashWindowFinishMs);
    const std::vector<SplashData> rightSplashFiltered =
        filter_splash_by_timestamp(rightData.splashRecords, splashWindowStartMs, splashWindowFinishMs);

    qDebug().noquote()
        << QString("ArsPostProcessSplash pair=%1 window100ms=[%2,%3) windowMs=[%4,%5) leftIntegralRange100ms=%6 rightIntegralRange100ms=%7 leftSplashRangeMs=%8 rightSplashRangeMs=%9")
               .arg(pairData.pairSerial)
               .arg(window.startTimestamp100ms)
               .arg(window.finishTimestamp100ms)
               .arg(splashWindowStartMs)
               .arg(splashWindowFinishMs)
               .arg(range_to_text(leftData.integralStates, [](const IntegralState &s) { return s.timestamp; }))
               .arg(range_to_text(rightData.integralStates, [](const IntegralState &s) { return s.timestamp; }))
               .arg(range_to_text(leftData.splashRecords, [](const SplashData &s) { return s.timestamp; }))
               .arg(range_to_text(rightData.splashRecords, [](const SplashData &s) { return s.timestamp; }));

    qDebug() << "Sessions tab postprocessing pair filtered"
             << "serial=" << pairData.pairSerial
             << "segment=" << (window.segmentIndex < 0 ? QString("session") : window.segmentName)
             << "range=[" << window.startTimestamp100ms << "," << window.finishTimestamp100ms << ")"
             << "leftIntegral=" << leftData.integralStates.size() << "/" << leftFiltered.size()
             << "rightIntegral=" << rightData.integralStates.size() << "/" << rightFiltered.size()
             << "leftSplash=" << leftData.splashRecords.size() << "/" << leftSplashFiltered.size()
             << "rightSplash=" << rightData.splashRecords.size() << "/" << rightSplashFiltered.size();

    if (leftFiltered.empty() && rightFiltered.empty() && leftSplashFiltered.empty() && rightSplashFiltered.empty())
    {
        out.problems.append(QString("%1: no data in %2")
                                .arg(label, window.segmentIndex < 0 ? QString("planned session period") : QString("segment period")));
        return out;
    }

    std::vector<IntegralState> leftIntegralRun = leftFiltered;
    std::vector<IntegralState> rightIntegralRun = rightFiltered;
    std::vector<SplashData> leftSplashRun = leftSplashFiltered;
    std::vector<SplashData> rightSplashRun = rightSplashFiltered;

    qDebug().noquote()
        << QString("ArsPostProcessSplash pair=%1 passedToPostProcessing leftIntegral=%2 rightIntegral=%3 leftSplash=%4 rightSplash=%5")
               .arg(pairData.pairSerial)
               .arg(leftIntegralRun.size())
               .arg(rightIntegralRun.size())
               .arg(leftSplashRun.size())
               .arg(rightSplashRun.size());

    PostProcessing pp;
    try
    {
        // PostProcessing API expects right data first, then left data.
        pp.processIntegralStateReports(rightIntegralRun, leftIntegralRun);
        pp.processSplashData(rightSplashRun, leftSplashRun);
    }
    catch (const std::exception &e)
    {
        out.problems.append(QString("%1: PostProcessing failed: %2").arg(label, QString::fromUtf8(e.what())));
        return out;
    }
    catch (...)
    {
        out.problems.append(QString("%1: PostProcessing failed with unknown exception").arg(label));
        return out;
    }

    const QString safePair = sanitize_file_stem(pairData.pairSerial) + window.fileSuffix;
    out.jsonPath = QDir(window.outputPath).filePath(QString("%1.json").arg(safePair));
    out.touchIntensityCsvPath = QDir(window.outputPath).filePath(QString("touchIntensity_%1.csv").arg(safePair));

    const PostProcessingSummary summary = pp.buildSummary();
    const std::vector<TouchIntensityPoint> touchIntensity = pp.buildTouchIntensityPerMinute();

    qDebug().noquote()
        << QString("ArsPostProcessSplash pair=%1 result touchesL=%2 touchesR=%3 touchesT=%4 kicksL=%5 kicksR=%6 kicksT=%7 light=%8 medium=%9 strong=%10 maxKickG=%11 possessions=%12 highSpeedDribbles=%13 dribblesFinish=%14 dribblesFinishPct=%15 ballDistanceM=%16 ballTimeSec=%17 oneTouch=%18 twoThree=%19 moreThanThree=%20 touchIntensitySamples=%21")
               .arg(pairData.pairSerial)
               .arg(summary.touchesLeft)
               .arg(summary.touchesRight)
               .arg(summary.touchesTotal)
               .arg(summary.kicksPassesLeft)
               .arg(summary.kicksPassesRight)
               .arg(summary.kicksPassesTotal)
               .arg(summary.lightKicksCount)
               .arg(summary.mediumKicksCount)
               .arg(summary.strongKicksCount)
               .arg(summary.maxKickForceG, 0, 'f', 3)
               .arg(summary.possessions)
               .arg(summary.highSpeedDribblesCount)
               .arg(summary.dribblesWithFinalKickCount)
               .arg(summary.dribblesWithFinalKickPercent, 0, 'f', 3)
               .arg(summary.ballDistanceM, 0, 'f', 3)
               .arg(summary.ballTimeSec, 0, 'f', 3)
               .arg(summary.oneTouchPlays)
               .arg(summary.twoThreeTouchPossessions)
               .arg(summary.moreThanThreeTouchDribbles)
               .arg(touchIntensity.size());

    QJsonObject root;
    root["pairSerial"] = pairData.pairSerial;
    root["sessionName"] = window.sessionName;
    root["status"] = "ok";
    if (window.segmentIndex >= 0)
    {
        root["segmentIndex"] = window.segmentIndex;
        root["segmentName"] = window.segmentName;
    }

    QJsonObject rangeObj;
    rangeObj["plannedStartTime"] = window.startTimeText;
    rangeObj["plannedFinishTime"] = window.finishTimeText;
    rangeObj["startTimestamp100ms"] = static_cast<qint64>(window.startTimestamp100ms);
    rangeObj["finishTimestamp100ms"] = static_cast<qint64>(window.finishTimestamp100ms);
    root["timeRange"] = rangeObj;

    QJsonObject inputObj;
    inputObj["leftTracker"] = pairData.left->trackerFolderName;
    inputObj["rightTracker"] = pairData.right->trackerFolderName;
    inputObj["leftIntegralStates"] = static_cast<qint64>(leftData.integralStates.size());
    inputObj["rightIntegralStates"] = static_cast<qint64>(rightData.integralStates.size());
    inputObj["leftSplashRecords"] = static_cast<qint64>(leftData.splashRecords.size());
    inputObj["rightSplashRecords"] = static_cast<qint64>(rightData.splashRecords.size());
    inputObj["leftIntegralStatesFiltered"] = static_cast<qint64>(leftFiltered.size());
    inputObj["rightIntegralStatesFiltered"] = static_cast<qint64>(rightFiltered.size());
    inputObj["leftSplashRecordsFiltered"] = static_cast<qint64>(leftSplashFiltered.size());
    inputObj["rightSplashRecordsFiltered"] = static_cast<qint64>(rightSplashFiltered.size());
    root["input"] = inputObj;
    root["metrics"] = summary_to_json(summary);

    const QJsonDocument jsonDoc(root);
    QString writeError;
    if (!write_text_file(out.jsonPath, QString::fromUtf8(jsonDoc.toJson(QJsonDocument::Indented)), &writeError))
    {
        out.problems.append(QString("%1: failed to write JSON: %2").arg(label, writeError));
        return out;
    }

    QString csv;
    csv += "minuteIndex,timestampMs,touchesInMovingMinute\n";
    for (const TouchIntensityPoint &row : touchIntensity)
    {
        csv += QString("%1,%2,%3\n")
                   .arg(row.minuteIndex)
                   .arg(row.timestampMs)
                   .arg(row.touchesInMovingMinute);
    }
    if (!write_text_file(out.touchIntensityCsvPath, csv, &writeError))
    {
        out.problems.append(QString("%1: failed to write touchIntensity CSV: %2").arg(label, writeError));
        return out;
    }

    out.ok = true;
    return out;
}
} // namespace

ArsPairPostprocessResult ArsSessionPostprocessor::processPair(const ArsSessionPostprocessRequest &request,
                                                              const ArsPairProcessedData &pairData)
{
    ArsPostprocessWindow window;
    window.sessionName = request.sessionName;
    window.outputPath = request.outputPath;
    window.startTimestamp100ms = request.timeRange.startTimestamp100ms;
    window.finishTimestamp100ms = request.timeRange.finishTimestamp100ms;
    window.startTimeText = request.timeRange.plannedStartTimeText;
    window.finishTimeText = request.timeRange.plannedFinishTimeText;
    return run_postprocess_window(window, pairData);
}

QList<ArsPairPostprocessResult> ArsSessionPostprocessor::processPairSegments(const ArsSessionPostprocessRequest &request,
                                                                            const ArsPairProcessedData &pairData)
{
    QList<ArsPairPostprocessResult> results;
    if (request.segments.isEmpty())
    {
        return results;
    }

    // Segment results live in a subfolder: the report builder scans postprocessed/*.json for pair files.
    const QString segmentsPath = QDir(request.outputPath).filePath(kArsSegmentsFolderName);
    if (!QDir().mkpath(segmentsPath))
    {
        ArsPairPostprocessResult failed;
        failed.pairSerial = pairData.pairSerial;
        failed.problems.append(QString("pair %1: failed to create segments folder: %2").arg(pairData.pairSerial, segmentsPath));
        results.append(failed);
        return results;
    }

    for (int i = 0; i < request.segments.size(); ++i)
    {
        const ArsSessionSegmentRange &segment = request.segments.at(i);
        ArsPostprocessWindow window;
        window.sessionName = request.sessionName;
        window.outputPath = segmentsPath;
        window.fileSuffix = QString("__seg%1").arg(i + 1, 2, 10, QChar('0'));
        window.segmentName = segment.name;
        window.segmentIndex = i;
        window.startTimestamp100ms = segment.startTimestamp100ms;
        window.finishTimestamp100ms = segment.finishTimestamp100ms;
        window.startTimeText = segment.startTimeText;
        window.finishTimeText = segment.finishTimeText;
        qDebug() << "Sessions tab segment postprocessing begin"
                 << "pair=" << pairData.pairSerial
                 << "segmentIndex=" << i
                 << "name=" << segment.name
                 << "range=[" << segment.startTimestamp100ms << "," << segment.finishTimestamp100ms << ")";
        results.append(run_postprocess_window(window, pairData));
    }
    return results;
}
