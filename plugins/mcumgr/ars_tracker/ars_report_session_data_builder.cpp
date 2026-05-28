#include "ars_report_session_data_builder.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QTextStream>

#include "ars/workspace/ArsPlayerRepository.h"
#include "ars/workspace/ArsLocalWorkspace.h"
#include "ars/workspace/ArsSessionPlayerBindingResolver.h"
#include "ars/workspace/ArsTeamRepository.h"
#include "ars/workspace/ArsTrackerBindingRepository.h"
#include "ars_tracker/ars_session_info_json.h"

namespace
{
QJsonDocument readJsonDocument(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error != nullptr) *error = QString("Failed to open JSON: %1").arg(path);
        return QJsonDocument();
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        if (error != nullptr) *error = QString("Invalid JSON in %1: %2").arg(path, parseError.errorString());
        return QJsonDocument();
    }
    return doc;
}

bool writeJsonObject(const QString &path, const QJsonObject &obj, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (error != nullptr) *error = QString("Failed to write JSON: %1").arg(path);
        return false;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

QString asString(const QJsonObject &obj, const QString &key)
{
    const QJsonValue value = obj.value(key);
    return value.isString() ? value.toString().trimmed() : QString();
}

double asDouble(const QJsonObject &obj, const QString &key, double def = 0.0)
{
    const QJsonValue value = obj.value(key);
    return value.isDouble() ? value.toDouble(def) : def;
}

int asInt(const QJsonObject &obj, const QString &key, int def = 0)
{
    const QJsonValue value = obj.value(key);
    return value.isDouble() ? value.toInt(def) : def;
}

bool parseUnixSessionId(const QString &sessionId, qint64 *timestampOut)
{
    bool ok = false;
    const qint64 parsed = sessionId.trimmed().toLongLong(&ok);
    if (!ok || parsed <= 0) return false;
    if (timestampOut != nullptr) *timestampOut = parsed;
    return true;
}

QString normalizeTime(const QString &raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) return QString();

    QTime t = QTime::fromString(trimmed, "HH:mm:ss");
    if (t.isValid()) return t.toString("HH:mm:ss");
    t = QTime::fromString(trimmed, "HH:mm");
    if (t.isValid()) return t.toString("HH:mm:ss");

    QDateTime dt = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
    if (!dt.isValid()) dt = QDateTime::fromString(trimmed, Qt::ISODate);
    if (dt.isValid()) return dt.time().toString("HH:mm:ss");

    return QString();
}

QString dateFromIsoDateTime(const QString &raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) return QString();
    QDateTime dt = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
    if (!dt.isValid()) dt = QDateTime::fromString(trimmed, Qt::ISODate);
    if (dt.isValid()) return dt.date().toString(Qt::ISODate);
    return QString();
}

QString workspaceRootFromSessionPath(const QString &sessionPath)
{
    ArsLocalWorkspace workspace;
    if (workspace.initialize())
    {
        const QString root = workspace.rootPath().trimmed();
        if (!root.isEmpty()) return root;
    }

    QDir sessionDir(sessionPath);
    if (!sessionDir.cdUp()) return QString();
    if (!sessionDir.cdUp()) return QString();
    return sessionDir.absolutePath();
}

QString sanitizePairId(QString value)
{
    value = value.trimmed();
    if (value.isEmpty()) return value;
    if (value.size() < 8 && QRegularExpression("^[0-9A-Fa-f]+$").match(value).hasMatch())
    {
        value = value.rightJustified(8, '0');
    }
    return value.toUpper();
}

QMap<QString, ArsSessionPairAssignment> loadSessionAssignmentsMap(const QString &sessionPath, QStringList *warnings)
{
    QMap<QString, ArsSessionPairAssignment> map;
    QList<ArsSessionPairAssignment> assignments;
    QString error;
    if (!ArsSessionInfoJson::readSessionPairAssignments(sessionPath, &assignments, &error))
    {
        if (!error.trimmed().isEmpty() && warnings != nullptr) warnings->append(error);
        return map;
    }
    for (const ArsSessionPairAssignment &a : assignments)
    {
        map.insert(sanitizePairId(a.pairId), a);
    }
    return map;
}

QJsonArray parseGoals(const QJsonValue &goalsValue)
{
    QJsonArray goals;
    if (goalsValue.isArray())
    {
        const QJsonArray arr = goalsValue.toArray();
        for (const QJsonValue &v : arr)
        {
            const QString g = v.toString().trimmed();
            if (!g.isEmpty()) goals.append(g);
        }
    }
    else if (goalsValue.isString())
    {
        const QStringList parts = goalsValue.toString().split('\n');
        for (const QString &part : parts)
        {
            const QString g = part.trimmed();
            if (!g.isEmpty()) goals.append(g);
        }
    }
    return goals;
}

QJsonObject defaultCalculationSettings()
{
    return QJsonObject{
        {"accelerationThresholdG", 2.5},
        {"dribbleSpeedThresholdMps", 1.5},
        {"shotZonesG", QJsonObject{{"lightMax", 20.0}, {"mediumMax", 40.0}, {"strongMin", 40.0}}},
        {"allowedFootloadImbalancePercent", 15.0},
        {"allowedTouchesImbalancePercent", 20.0},
        {"allowedShotsPassesImbalancePercent", 20.0}};
}

QJsonObject buildSessionObject(const QString &sessionPath,
                               const QJsonObject &sessionInfoRoot,
                               const ArsTeam *teamForDefaults)
{
    const QString sessionIdFallback = QFileInfo(sessionPath).fileName();
    const QString sessionId = asString(sessionInfoRoot, "sessionId").isEmpty()
                                  ? sessionIdFallback
                                  : asString(sessionInfoRoot, "sessionId");

    qint64 timestamp = 0;
    if (sessionInfoRoot.value("timestamp").isDouble()) timestamp = static_cast<qint64>(sessionInfoRoot.value("timestamp").toDouble());
    if (timestamp <= 0) parseUnixSessionId(sessionIdFallback, &timestamp);

    QString date = asString(sessionInfoRoot, "date");
    QString startTime = normalizeTime(asString(sessionInfoRoot, "startTime"));
    QString endTime = normalizeTime(asString(sessionInfoRoot, "endTime"));

    const QJsonObject actual = sessionInfoRoot.value("actualTime").toObject();
    if (startTime.isEmpty()) startTime = normalizeTime(asString(actual, "startTime"));
    if (endTime.isEmpty()) endTime = normalizeTime(asString(actual, "finishTime"));

    if (date.isEmpty())
    {
        date = dateFromIsoDateTime(asString(sessionInfoRoot, "startTime"));
    }
    if (date.isEmpty() && timestamp > 0)
    {
        date = QDateTime::fromSecsSinceEpoch(timestamp).date().toString(Qt::ISODate);
    }

    const QString location = asString(sessionInfoRoot, "location").isEmpty()
                                 ? QString("%1 / %2").arg(asString(sessionInfoRoot, "LocationName"), asString(sessionInfoRoot, "SurfaceType")).trimmed()
                                 : asString(sessionInfoRoot, "location");

    QString teamId = asString(sessionInfoRoot, "teamId");
    if (teamId.isEmpty() && sessionInfoRoot.value("TeamId").isDouble())
    {
        teamId = QString::number(sessionInfoRoot.value("TeamId").toInt());
    }

    QJsonObject session;
    session["sessionId"] = sessionId;
    session["timestamp"] = static_cast<double>(timestamp);
    session["date"] = date;
    session["startTime"] = startTime;
    session["endTime"] = endTime;
    session["type"] = asString(sessionInfoRoot, "type").isEmpty() ? QString("training") : asString(sessionInfoRoot, "type");
    session["teamId"] = teamId;
    session["location"] = location;
    session["goals"] = parseGoals(sessionInfoRoot.value("goals"));

    QJsonObject planned = sessionInfoRoot.value("plannedMetrics").toObject();
    if (planned.isEmpty() && teamForDefaults != nullptr)
    {
        planned["distanceKm"] = teamForDefaults->defaultPlannedMetrics.distanceKm;
        planned["accelerationDistanceM"] = teamForDefaults->defaultPlannedMetrics.accelerationDistanceM;
        planned["footloadPerLeg"] = teamForDefaults->defaultPlannedMetrics.footloadPerLeg;
        planned["loadIntensityGPerMin"] = teamForDefaults->defaultPlannedMetrics.loadIntensityGPerMin;
        planned["maxSpeedMps"] = teamForDefaults->defaultPlannedMetrics.maxSpeedMps;
        planned["touches"] = teamForDefaults->defaultPlannedMetrics.touches;
        planned["shots"] = teamForDefaults->defaultPlannedMetrics.shots;
        planned["dribbles"] = teamForDefaults->defaultPlannedMetrics.dribbles;
    }
    session["plannedMetrics"] = planned;

    QJsonObject settings = sessionInfoRoot.value("calculationSettings").toObject();
    if (settings.isEmpty()) settings = defaultCalculationSettings();
    if (!settings.value("shotZonesG").isObject()) settings["shotZonesG"] = defaultCalculationSettings().value("shotZonesG").toObject();
    if (!settings.value("accelerationThresholdG").isDouble()) settings["accelerationThresholdG"] = 2.5;
    if (!settings.value("dribbleSpeedThresholdMps").isDouble())
        settings["dribbleSpeedThresholdMps"] = 4.0;
    if (!settings.value("allowedFootloadImbalancePercent").isDouble()) settings["allowedFootloadImbalancePercent"] = 15.0;
    if (!settings.value("allowedTouchesImbalancePercent").isDouble()) settings["allowedTouchesImbalancePercent"] = 20.0;
    if (!settings.value("allowedShotsPassesImbalancePercent").isDouble()) settings["allowedShotsPassesImbalancePercent"] = 20.0;
    session["calculationSettings"] = settings;
    session["exercises"] = sessionInfoRoot.value("exercises").isArray() ? sessionInfoRoot.value("exercises").toArray() : QJsonArray();
    return session;
}

QJsonObject teamToReporter(const ArsTeam &team)
{
    QJsonArray coaches;
    for (const QString &name : team.defaultCoaches)
    {
        const QString trimmed = name.trimmed();
        if (trimmed.isEmpty()) continue;
        coaches.append(QJsonObject{{"fullName", trimmed}, {"role", "Тренер"}});
    }
    return QJsonObject{
        {"teamId", QString::number(team.teamId)},
        {"name", team.name},
        {"ageCategory", team.ageCategory},
        {"logoPath", team.teamLogoPath},
        {"coaches", coaches}};
}

QJsonObject fallbackTeam(const QString &teamId)
{
    return QJsonObject{{"teamId", teamId}, {"name", "Команда"}, {"ageCategory", ""}, {"logoPath", ""}, {"coaches", QJsonArray()}};
}

QJsonObject playerToReporter(const ArsPlayer &player, const QString &fallbackName)
{
    QString fullName = fallbackName.trimmed();
    if (fullName.isEmpty())
    {
        const QString fromParts = QString("%1 %2").arg(player.surname.trimmed(), player.name.trimmed()).trimmed();
        if (!fromParts.isEmpty()) fullName = fromParts;
    }
    if (fullName.isEmpty()) fullName = QString("Игрок %1").arg(player.playerId);

    QJsonObject personalNorms{
        {"maxSpeedMps", player.personalNorms.maxSpeedMps},
        {"maxShotLoadG", player.personalNorms.maxShotLoadG},
        {"distanceKm", 0.0}};

    return QJsonObject{
        {"playerId", player.playerId},
        {"fullName", fullName},
        {"photoPath", player.photoPath},
        {"number", player.number},
        {"birthDate", player.birthDate.isValid() ? player.birthDate.toString(Qt::ISODate) : QString()},
        {"age", player.birthDate.isValid() ? player.birthDate.daysTo(QDate::currentDate()) / 365 : 0},
        {"position", player.position},
        {"dominantFoot", player.dominantFoot},
        {"heightCm", player.heightCm},
        {"weightKg", player.weightKg},
        {"teamStatus", QString()},
        {"loadRestrictions", QString()},
        {"medicalComments", QString()},
        {"personalNorms", personalNorms}};
}

QJsonObject unresolvedPlayer(const QString &playerId, const QString &pairId)
{
    return QJsonObject{
        {"playerId", playerId},
        {"fullName", QString("Неназначенный игрок %1").arg(pairId)},
        {"photoPath", QString()},
        {"number", 0},
        {"birthDate", QString()},
        {"age", 0},
        {"position", pairId},
        {"dominantFoot", QString("R")},
        {"heightCm", 0},
        {"weightKg", 0},
        {"teamStatus", QString()},
        {"loadRestrictions", QString()},
        {"medicalComments", QString()},
        {"personalNorms", QJsonObject{{"maxSpeedMps", 0.0}, {"maxShotLoadG", 0.0}, {"distanceKm", 0.0}}}};
}

QJsonArray loadTouchIntensity(const QString &csvPath, QStringList *warnings, const QString &pairId)
{
    QJsonArray out;
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        Q_UNUSED(warnings);
        Q_UNUSED(pairId);
        return out;
    }

    QTextStream in(&file);
    if (in.atEnd()) return out;

    const QString headerLine = in.readLine().trimmed();
    const QStringList headers = headerLine.split(',');
    int minuteIdx = headers.indexOf("minuteIndex");
    int tsIdx = headers.indexOf("timestampMs");
    int touchesIdx = headers.indexOf("touchesInMovingMinute");
    if (minuteIdx < 0 || touchesIdx < 0)
    {
        if (warnings != nullptr) warnings->append(QString("touchIntensity csv has unexpected header for pair %1").arg(pairId));
        return out;
    }

    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QStringList cols = line.split(',');
        if (cols.size() <= qMax(minuteIdx, touchesIdx)) continue;
        bool minuteOk = false;
        bool touchesOk = false;
        const int minute = cols.at(minuteIdx).toInt(&minuteOk);
        const int touches = cols.at(touchesIdx).toInt(&touchesOk);
        if (!minuteOk || !touchesOk) continue;
        QJsonObject row{
            {"minute", minute},
            {"touches", touches},
            {"minuteIndex", minute},
            {"touchesInMovingMinute", touches}};
        if (tsIdx >= 0 && cols.size() > tsIdx)
        {
            bool tsOk = false;
            const qint64 ts = cols.at(tsIdx).toLongLong(&tsOk);
            if (tsOk) row["timestampMs"] = static_cast<double>(ts);
        }
        out.append(row);
    }
    return out;
}

void mapMetrics(const QJsonObject &pairMetrics, QJsonObject *dst)
{
    if (dst == nullptr) return;
    if (pairMetrics.value("distanceKm").isDouble()) (*dst)["distanceM"] = pairMetrics.value("distanceKm").toDouble() * 1000.0;
    if (pairMetrics.value("maxSpeedMs").isDouble()) (*dst)["maxSpeedMps"] = pairMetrics.value("maxSpeedMs").toDouble();
    if (pairMetrics.value("avgSpeedMs").isDouble()) (*dst)["avgSpeedMps"] = pairMetrics.value("avgSpeedMs").toDouble();
    if (pairMetrics.value("durationSec").isDouble()) (*dst)["moveTimeSec"] = pairMetrics.value("durationSec").toDouble();
    if (pairMetrics.value("steps").isDouble()) (*dst)["steps"] = pairMetrics.value("steps").toInt();
    if (pairMetrics.value("accelerationCount").isDouble()) (*dst)["accelerations"] = pairMetrics.value("accelerationCount").toInt();
    if (pairMetrics.value("accelerationDistanceM").isDouble()) (*dst)["distanceAccelerationsM"] = pairMetrics.value("accelerationDistanceM").toDouble();
    if (pairMetrics.value("footloadLeft").isDouble()) (*dst)["footloadLeft"] = pairMetrics.value("footloadLeft").toDouble();
    if (pairMetrics.value("footloadRight").isDouble()) (*dst)["footloadRight"] = pairMetrics.value("footloadRight").toDouble();
    if (pairMetrics.value("footloadTotal").isDouble()) (*dst)["footloadTotal"] = pairMetrics.value("footloadTotal").toDouble();
    if (pairMetrics.value("loadDisbalancePercent").isDouble()) (*dst)["footloadImbalancePercent"] = pairMetrics.value("loadDisbalancePercent").toDouble();
    if (pairMetrics.value("loadIntensityLeft").isDouble()) (*dst)["loadIntensityLeftGPerMin"] = pairMetrics.value("loadIntensityLeft").toDouble();
    if (pairMetrics.value("loadIntensityRight").isDouble()) (*dst)["loadIntensityRightGPerMin"] = pairMetrics.value("loadIntensityRight").toDouble();
    if (pairMetrics.value("touchesLeft").isDouble()) (*dst)["touchesLeft"] = pairMetrics.value("touchesLeft").toInt();
    if (pairMetrics.value("touchesRight").isDouble()) (*dst)["touchesRight"] = pairMetrics.value("touchesRight").toInt();
    if (pairMetrics.value("touchesTotal").isDouble()) (*dst)["touchesTotal"] = pairMetrics.value("touchesTotal").toInt();
    if (pairMetrics.value("kicksPassesLeft").isDouble()) (*dst)["shotsLeft"] = pairMetrics.value("kicksPassesLeft").toInt();
    if (pairMetrics.value("kicksPassesRight").isDouble()) (*dst)["shotsRight"] = pairMetrics.value("kicksPassesRight").toInt();
    if (pairMetrics.value("kicksPassesTotal").isDouble()) (*dst)["shotsTotal"] = pairMetrics.value("kicksPassesTotal").toInt();
    if (pairMetrics.value("lightKicksCount").isDouble()) (*dst)["weakShots"] = pairMetrics.value("lightKicksCount").toInt();
    if (pairMetrics.value("mediumKicksCount").isDouble()) (*dst)["mediumShots"] = pairMetrics.value("mediumKicksCount").toInt();
    if (pairMetrics.value("strongKicksCount").isDouble()) (*dst)["strongShots"] = pairMetrics.value("strongKicksCount").toInt();
    if (pairMetrics.value("maxKickForceG").isDouble()) (*dst)["maxShotG"] = pairMetrics.value("maxKickForceG").toDouble();
    if (pairMetrics.value("possessions").isDouble()) (*dst)["possessions"] = pairMetrics.value("possessions").toInt();
    if (pairMetrics.value("highSpeedDribblesCount").isDouble()) (*dst)["speedDribbles"] = pairMetrics.value("highSpeedDribblesCount").toInt();
    if (pairMetrics.value("dribblesWithFinalKickCount").isDouble()) (*dst)["dribblesWithFinish"] = pairMetrics.value("dribblesWithFinalKickCount").toInt();
    if (pairMetrics.value("oneTouchPlays").isDouble()) (*dst)["oneTouchPossessions"] = pairMetrics.value("oneTouchPlays").toInt();
    if (pairMetrics.value("twoThreeTouchPossessions").isDouble()) (*dst)["twoThreeTouchPossessions"] = pairMetrics.value("twoThreeTouchPossessions").toInt();
    if (pairMetrics.value("moreThanThreeTouchDribbles").isDouble())
    {
        (*dst)["moreThanThreeTouchPossessions"] = pairMetrics.value("moreThanThreeTouchDribbles").toInt();
        (*dst)["dribbles"] = pairMetrics.value("moreThanThreeTouchDribbles").toInt();
    }
    if (pairMetrics.value("ballDistanceM").isDouble()) (*dst)["ballDistanceM"] = pairMetrics.value("ballDistanceM").toDouble();
    if (pairMetrics.value("ballTimeSec").isDouble()) (*dst)["ballTimeSec"] = pairMetrics.value("ballTimeSec").toDouble();
}
} // namespace

ArsReportSessionDataBuildResult buildArsReportSessionDataJson(const QString sessionPath)
{
    ArsReportSessionDataBuildResult result;
    const QString sessionInfoPath = QDir(sessionPath).filePath("SessionInfo.json");
    const QString postprocessedPath = QDir(sessionPath).filePath("postprocessed");
    const QString sessionDataPath = QDir(postprocessedPath).filePath("SessionData.json");

    if (!QFileInfo::exists(sessionInfoPath))
    {
        result.error = QString("SessionInfo.json is missing: %1").arg(sessionInfoPath);
        return result;
    }
    if (!QDir(postprocessedPath).exists())
    {
        result.error = QString("postprocessed directory is missing: %1").arg(postprocessedPath);
        return result;
    }

    QString jsonError;
    const QJsonDocument sessionInfoDoc = readJsonDocument(sessionInfoPath, &jsonError);
    if (!sessionInfoDoc.isObject())
    {
        result.error = jsonError.isEmpty() ? QString("Invalid SessionInfo.json: %1").arg(sessionInfoPath) : jsonError;
        return result;
    }
    const QJsonObject sessionInfoRoot = sessionInfoDoc.object();

    int teamIdInt = -1;
    if (sessionInfoRoot.value("teamId").isDouble()) teamIdInt = sessionInfoRoot.value("teamId").toInt(-1);
    if (teamIdInt <= 0 && sessionInfoRoot.value("TeamId").isDouble()) teamIdInt = sessionInfoRoot.value("TeamId").toInt(-1);

    const QString workspaceRoot = workspaceRootFromSessionPath(sessionPath);
    ArsTeam teamModel;
    bool hasTeamModel = false;
    if (teamIdInt > 0 && !workspaceRoot.isEmpty())
    {
        ArsTeamRepository teamRepository(workspaceRoot);
        QString teamError;
        hasTeamModel = teamRepository.loadTeam(teamIdInt, &teamModel, &teamError);
        if (!hasTeamModel && !teamError.trimmed().isEmpty()) result.warnings.append(QString("Team lookup warning: %1").arg(teamError));
    }

    const QJsonObject sessionObject = buildSessionObject(sessionPath, sessionInfoRoot, hasTeamModel ? &teamModel : nullptr);
    const QString reporterTeamId = asString(sessionObject, "teamId");

    QJsonObject teamObject;
    QString teamLogoPath;
    bool teamLogoExists = false;
    const QString reporterTeamInfoPath = QDir(sessionPath).filePath("TeamInfo.json");
    if (QFileInfo::exists(reporterTeamInfoPath))
    {
        QString teamJsonError;
        const QJsonDocument teamDoc = readJsonDocument(reporterTeamInfoPath, &teamJsonError);
        if (teamDoc.isObject()) teamObject = teamDoc.object();
        else result.warnings.append(QString("TeamInfo.json parse warning: %1").arg(teamJsonError));
    }
    if (teamObject.isEmpty() && hasTeamModel)
    {
        teamObject = teamToReporter(teamModel);
    }
    if (teamObject.isEmpty())
    {
        teamObject = fallbackTeam(reporterTeamId);
        result.warnings.append("Team data was not found; fallback team was used.");
    }
    teamLogoPath = asString(teamObject, "logoPath");
    if (!teamLogoPath.isEmpty())
    {
        teamLogoExists = QFileInfo(teamLogoPath).exists()
                         || QFileInfo(QDir(workspaceRoot).filePath(teamLogoPath)).exists();
        if (!teamLogoExists)
        {
            result.warnings.append(QString("Team logo file not found: %1").arg(teamLogoPath));
        }
    }

    const QMap<QString, ArsSessionPairAssignment> sessionAssignments = loadSessionAssignmentsMap(sessionPath, &result.warnings);
    ArsTrackerBindingRepository bindingRepository(workspaceRoot);
    ArsPlayerRepository playerRepository(workspaceRoot);

    const QFileInfoList pairFiles = QDir(postprocessedPath).entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Name);
    QJsonArray playersArray;
    QJsonArray resultsArray;
    QMap<QString, QJsonObject> playersById;

    int foundPairJsonCount = 0;
    int assignedPairCount = 0;
    int skippedUnassignedPairCount = 0;
    int touchIntensityFilesFound = 0;
    int touchIntensitySamplesTotal = 0;
    for (const QFileInfo &pairFile : pairFiles)
    {
        if (pairFile.fileName().compare("SessionData.json", Qt::CaseInsensitive) == 0) continue;
        ++foundPairJsonCount;
        QString pairError;
        const QJsonDocument pairDoc = readJsonDocument(pairFile.absoluteFilePath(), &pairError);
        if (!pairDoc.isObject())
        {
            result.warnings.append(QString("Skip invalid postprocessed JSON %1: %2").arg(pairFile.fileName(), pairError));
            continue;
        }
        const QJsonObject pairObj = pairDoc.object();
        const QString pairId = sanitizePairId(asString(pairObj, "pairSerial").isEmpty() ? pairFile.baseName() : asString(pairObj, "pairSerial"));

        ArsSessionPairAssignment assignment = sessionAssignments.value(pairId);
        bool hasResolvedAssignment = !assignment.playerId.trimmed().isEmpty();
        if (hasResolvedAssignment && !assignment.source.trimmed().isEmpty() &&
            assignment.source.compare("unresolved", Qt::CaseInsensitive) == 0)
        {
            hasResolvedAssignment = false;
        }
        if (!hasResolvedAssignment && teamIdInt > 0)
        {
            ArsTrackerPlayerBinding globalBinding;
            QString bindError;
            if (bindingRepository.findBinding(teamIdInt, pairId, &globalBinding, &bindError))
            {
                assignment.pairId = pairId;
                assignment.playerId = globalBinding.playerId;
                assignment.leftTrackerSerial = globalBinding.leftTrackerSerial;
                assignment.rightTrackerSerial = globalBinding.rightTrackerSerial;
                assignment.teamId = globalBinding.teamId;
                assignment.source = "global";
                hasResolvedAssignment = !assignment.playerId.trimmed().isEmpty();
            }
            else if (!bindError.trimmed().isEmpty())
            {
                result.warnings.append(QString("Global binding lookup warning for pair %1: %2").arg(pairId, bindError));
            }
        }

        if (!hasResolvedAssignment)
        {
            ++skippedUnassignedPairCount;
            const QString warning = QString("Skipped pair %1: no resolved player binding").arg(pairId);
            result.warnings.append(warning);
            continue;
        }

        ++assignedPairCount;
        const QString playerId = assignment.playerId.trimmed();
        const QString fallbackPlayerName = assignment.playerName.trimmed();
        if (!playersById.contains(playerId))
        {
            QJsonObject playerObj;
            ArsPlayer player;
            QString playerError;
            if (playerRepository.loadPlayer(playerId, &player, &playerError))
            {
                playerObj = playerToReporter(player, fallbackPlayerName);
            }
            else
            {
                if (!playerError.trimmed().isEmpty())
                {
                    result.warnings.append(QString("Player file is missing for playerId=%1: %2").arg(playerId, playerError));
                }
                ArsPlayer fallbackPlayer;
                fallbackPlayer.playerId = playerId;
                playerObj = playerToReporter(fallbackPlayer, fallbackPlayerName.isEmpty() ? QString("Игрок %1").arg(playerId) : fallbackPlayerName);
            }
            playersById.insert(playerId, playerObj);
        }
        else
        {
            result.warnings.append(QString("Duplicate playerId reference detected: %1 (pair %2)").arg(playerId, pairId));
        }

        const QJsonObject metricsSrc = pairObj.value("metrics").toObject();
        QJsonObject metricsDst;
        mapMetrics(metricsSrc, &metricsDst);
        qDebug() << "ArsReportBridge pair=" << pairId
                 << "ballDistanceM=" << metricsDst.value("ballDistanceM").toDouble(0.0)
                 << "ballTimeSec=" << metricsDst.value("ballTimeSec").toDouble(0.0);

        const QString touchCsv = QDir(postprocessedPath).filePath(QString("touchIntensity_%1.csv").arg(pairId));
        const bool hasTouchCsv = QFileInfo::exists(touchCsv);
        if (hasTouchCsv) ++touchIntensityFilesFound;
        const QJsonArray touchIntensity = loadTouchIntensity(touchCsv, &result.warnings, pairId);
        touchIntensitySamplesTotal += touchIntensity.size();
        if (hasTouchCsv && touchIntensity.isEmpty())
        {
            result.warnings.append(QString("Touch intensity csv has no samples for pair %1").arg(pairId));
        }
        qDebug() << "Touch intensity pairId:" << pairId << "file found" << hasTouchCsv << "samples=" << touchIntensity.size();

        QString status = asString(pairObj, "status");
        if (status.isEmpty()) status = "notAvailable";
        QString error = asString(pairObj, "error");
        if (error.isEmpty()) error = asString(pairObj, "message");
        if (status.compare("ok", Qt::CaseInsensitive) != 0 && error.isEmpty())
        {
            error = QString("Postprocessing status is %1").arg(status);
        }
        if (!error.isEmpty())
        {
            result.warnings.append(QString("Pair %1: %2").arg(pairId, error));
        }

        QJsonObject resultObj{
            {"playerId", playerId},
            {"trackerL", asString(pairObj.value("input").toObject(), "leftTracker")},
            {"trackerR", asString(pairObj.value("input").toObject(), "rightTracker")},
            {"status", status},
            {"error", error},
            {"metrics", metricsDst},
            {"touchIntensityByMinute", touchIntensity},
            {"finishingDribbles", QJsonArray()},
            {"possessionEvents", QJsonArray()},
            {"shotEvents", QJsonArray()},
            {"exerciseStats", QJsonArray()}};
        resultsArray.append(resultObj);
    }

    if (foundPairJsonCount <= 0)
    {
        result.error = QString("No valid postprocessed pair JSON files found in: %1").arg(postprocessedPath);
        return result;
    }
    if (assignedPairCount <= 0 || resultsArray.isEmpty())
    {
        result.error = "No assigned players found for this session. Assign tracker pairs to players before generating PDF.";
        return result;
    }

    for (auto it = playersById.cbegin(); it != playersById.cend(); ++it)
    {
        playersArray.append(it.value());
    }

    QJsonArray processingErrors;
    for (const QString &w : result.warnings) processingErrors.append(w);

    QJsonObject root{
        {"reportType", "session"},
        {"session", sessionObject},
        {"team", teamObject},
        {"players", playersArray},
        {"results", resultsArray},
        {"processingErrors", processingErrors},
        {"exerciseStats", QJsonArray()}};

    QString writeError;
    if (!writeJsonObject(sessionDataPath, root, &writeError))
    {
        result.error = writeError;
        return result;
    }

    result.ok = true;
    result.sessionDataPath = sessionDataPath;
    qDebug() << "Ars report builder done"
             << "sessionDataPath=" << sessionDataPath
             << "foundPairJsonCount=" << foundPairJsonCount
             << "assignedPairCount=" << assignedPairCount
             << "skippedUnassignedPairCount=" << skippedUnassignedPairCount
             << "playersCount=" << playersArray.size()
             << "resultsCount=" << resultsArray.size()
             << "teamId=" << reporterTeamId
             << "teamLogoPath=" << teamLogoPath
             << "teamLogoExists=" << teamLogoExists
             << "touchIntensityFilesFound=" << touchIntensityFilesFound
             << "touchIntensitySamplesTotal=" << touchIntensitySamplesTotal
             << "warningsCount=" << result.warnings.size();
    return result;
}
