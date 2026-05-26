#include "ars_session_postprocessor.h"

#include <QDir>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <exception>

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
                                                   uint32_t start100ms,
                                                   uint32_t finish100ms)
{
    std::vector<SplashData> out;
    out.reserve(input.size());
    for (const SplashData &state : input)
    {
        if (state.timestamp >= start100ms && state.timestamp < finish100ms)
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
}

ArsPairPostprocessResult ArsSessionPostprocessor::processPair(const ArsSessionPostprocessRequest &request,
                                                              const ArsPairProcessedData &pairData)
{
    ArsPairPostprocessResult out;
    out.pairSerial = pairData.pairSerial;

    if (!pairData.left.has_value() || !pairData.right.has_value())
    {
        out.problems.append(QString("pair %1: missing left/right tracker data").arg(pairData.pairSerial));
        return out;
    }

    const ArsProcessedStrData &leftData = pairData.left->data;
    const ArsProcessedStrData &rightData = pairData.right->data;

    const std::vector<IntegralState> leftFiltered =
        filter_integral_by_timestamp(leftData.integralStates, request.timeRange.startTimestamp100ms, request.timeRange.finishTimestamp100ms);
    const std::vector<IntegralState> rightFiltered =
        filter_integral_by_timestamp(rightData.integralStates, request.timeRange.startTimestamp100ms, request.timeRange.finishTimestamp100ms);
    const std::vector<SplashData> leftSplashFiltered =
        filter_splash_by_timestamp(leftData.splashRecords, request.timeRange.startTimestamp100ms, request.timeRange.finishTimestamp100ms);
    const std::vector<SplashData> rightSplashFiltered =
        filter_splash_by_timestamp(rightData.splashRecords, request.timeRange.startTimestamp100ms, request.timeRange.finishTimestamp100ms);

    qDebug() << "Sessions tab postprocessing pair filtered"
             << "serial=" << pairData.pairSerial
             << "range=[" << request.timeRange.startTimestamp100ms << "," << request.timeRange.finishTimestamp100ms << ")"
             << "leftIntegral=" << leftData.integralStates.size() << "/" << leftFiltered.size()
             << "rightIntegral=" << rightData.integralStates.size() << "/" << rightFiltered.size()
             << "leftSplash=" << leftData.splashRecords.size() << "/" << leftSplashFiltered.size()
             << "rightSplash=" << rightData.splashRecords.size() << "/" << rightSplashFiltered.size();

    if (leftFiltered.empty() && rightFiltered.empty() && leftSplashFiltered.empty() && rightSplashFiltered.empty())
    {
        out.problems.append(QString("pair %1: no data in planned session period").arg(pairData.pairSerial));
        return out;
    }

    std::vector<IntegralState> leftIntegralRun = leftFiltered;
    std::vector<IntegralState> rightIntegralRun = rightFiltered;
    std::vector<SplashData> leftSplashRun = leftSplashFiltered;
    std::vector<SplashData> rightSplashRun = rightSplashFiltered;

    PostProcessing pp;
    try
    {
        // PostProcessing API expects right data first, then left data.
        pp.processIntegralStateReports(rightIntegralRun, leftIntegralRun);
        pp.processSplashData(rightSplashRun, leftSplashRun);
    }
    catch (const std::exception &e)
    {
        out.problems.append(QString("pair %1: PostProcessing failed: %2").arg(pairData.pairSerial, QString::fromUtf8(e.what())));
        return out;
    }
    catch (...)
    {
        out.problems.append(QString("pair %1: PostProcessing failed with unknown exception").arg(pairData.pairSerial));
        return out;
    }

    const QString safePair = sanitize_file_stem(pairData.pairSerial);
    out.jsonPath = QDir(request.outputPath).filePath(QString("%1.json").arg(safePair));
    out.touchIntensityCsvPath = QDir(request.outputPath).filePath(QString("touchIntensity_%1.csv").arg(safePair));

    const PostProcessingSummary summary = pp.buildSummary();
    const std::vector<TouchIntensityPoint> touchIntensity = pp.buildTouchIntensityPerMinute();

    QJsonObject root;
    root["pairSerial"] = pairData.pairSerial;
    root["sessionName"] = request.sessionName;
    root["status"] = "ok";

    QJsonObject rangeObj;
    rangeObj["plannedStartTime"] = request.timeRange.plannedStartTimeText;
    rangeObj["plannedFinishTime"] = request.timeRange.plannedFinishTimeText;
    rangeObj["startTimestamp100ms"] = static_cast<qint64>(request.timeRange.startTimestamp100ms);
    rangeObj["finishTimestamp100ms"] = static_cast<qint64>(request.timeRange.finishTimestamp100ms);
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
        out.problems.append(QString("pair %1: failed to write JSON: %2").arg(pairData.pairSerial, writeError));
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
        out.problems.append(QString("pair %1: failed to write touchIntensity CSV: %2").arg(pairData.pairSerial, writeError));
        return out;
    }

    out.ok = true;
    return out;
}
