#include "ars_team_repository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QRegularExpression>
#include <algorithm>

namespace
{
QJsonObject planned_metrics_to_json(const ArsSessionPlannedMetrics &m)
{
    QJsonObject o;
    o["accelerationDistanceM"] = m.accelerationDistanceM;
    o["distanceKm"] = m.distanceKm;
    o["dribbles"] = m.dribbles;
    o["footloadPerLeg"] = m.footloadPerLeg;
    o["loadIntensityGPerMin"] = m.loadIntensityGPerMin;
    o["maxSpeedMps"] = m.maxSpeedMps;
    o["shots"] = m.shots;
    o["touches"] = m.touches;
    return o;
}

ArsSessionPlannedMetrics planned_metrics_from_json(const QJsonObject &o)
{
    ArsSessionPlannedMetrics m;
    m.accelerationDistanceM = o.value("accelerationDistanceM").toInt(0);
    m.distanceKm = o.value("distanceKm").toDouble(0.0);
    m.dribbles = o.value("dribbles").toInt(0);
    m.footloadPerLeg = o.value("footloadPerLeg").toInt(0);
    m.loadIntensityGPerMin = o.value("loadIntensityGPerMin").toDouble(0.0);
    m.maxSpeedMps = o.value("maxSpeedMps").toDouble(0.0);
    m.shots = o.value("shots").toInt(0);
    m.touches = o.value("touches").toInt(0);
    return m;
}

QStringList coaches_from_json(const QJsonValue &v)
{
    QStringList out;
    if (!v.isArray())
    {
        return out;
    }
    const QJsonArray a = v.toArray();
    for (const QJsonValue &entry : a)
    {
        const QString c = entry.toString().trimmed();
        if (!c.isEmpty())
        {
            out.append(c);
        }
    }
    return out;
}
}

ArsTeamRepository::ArsTeamRepository(const QString &workspacePath)
    : m_workspacePath(workspacePath)
{
}

QString ArsTeamRepository::teamsPath() const
{
    return QDir(m_workspacePath).filePath("teams");
}

bool ArsTeamRepository::ensureTeamsDir(QString *errorMessage) const
{
    const QString path = teamsPath();
    if (QDir(path).exists() || QDir().mkpath(path))
    {
        return true;
    }
    if (errorMessage != nullptr)
    {
        *errorMessage = QString("Failed to create teams directory: %1").arg(path);
    }
    return false;
}

QString ArsTeamRepository::teamFilePath(int teamId) const
{
    return QDir(teamsPath()).filePath(QString("team_%1.json").arg(teamId));
}

QList<ArsTeam> ArsTeamRepository::loadTeams(QStringList *warnings) const
{
    QList<ArsTeam> teams;
    QString dirError;
    if (!ensureTeamsDir(&dirError))
    {
        if (warnings != nullptr)
        {
            warnings->append(dirError);
        }
        return teams;
    }

    qDebug() << "Ars Team repository teams path=" << teamsPath();
    const QDir dir(teamsPath());
    const QFileInfoList files = dir.entryInfoList(QStringList() << "team_*.json", QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files)
    {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (warnings != nullptr)
            {
                warnings->append(QString("Ars Team load warning file=%1 error=%2").arg(fi.fileName(), f.errorString()));
            }
            continue;
        }
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject())
        {
            if (warnings != nullptr)
            {
                warnings->append(QString("Ars Team load warning file=%1 error=%2").arg(fi.fileName(), pe.errorString()));
            }
            continue;
        }
        const QJsonObject o = doc.object();
        ArsTeam t;
        t.teamNum = o.value("team_num").toInt(0);
        t.teamId = o.value("team_id").toInt(0);
        if (t.teamId <= 0)
        {
            const QRegularExpression re("^team_(\\d+)\\.json$", QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch m = re.match(fi.fileName());
            if (m.hasMatch())
            {
                t.teamId = m.captured(1).toInt();
            }
        }
        t.name = o.value("name").toString();
        t.ageCategory = o.value("age_category").toString();
        t.defaultCoaches = coaches_from_json(o.value("default_coaches"));
        const QJsonObject thresholds = o.value("default_thresholds").toObject();
        t.defaultPlannedMetrics = planned_metrics_from_json(thresholds.value("plannedMetrics").toObject());
        const QJsonObject logo = o.value("team_logo").toObject();
        t.teamLogoPath = logo.value("path").toString();
        if (t.teamLogoPath.trimmed().isEmpty())
        {
            t.teamLogoPath = o.value("team_logo_path").toString();
        }
        if (t.teamId <= 0)
        {
            if (warnings != nullptr)
            {
                warnings->append(QString("Ars Team load warning file=%1 error=invalid team_id").arg(fi.fileName()));
            }
            continue;
        }
        teams.append(t);
    }

    std::sort(teams.begin(), teams.end(), [](const ArsTeam &a, const ArsTeam &b) { return a.teamId < b.teamId; });
    return teams;
}

bool ArsTeamRepository::loadTeam(int teamId, ArsTeam *outTeam, QString *errorMessage) const
{
    if (outTeam == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Output pointer is null";
        }
        return false;
    }
    const QList<ArsTeam> teams = loadTeams(nullptr);
    for (const ArsTeam &t : teams)
    {
        if (t.teamId == teamId)
        {
            *outTeam = t;
            return true;
        }
    }
    if (errorMessage != nullptr)
    {
        *errorMessage = QString("Team id %1 not found").arg(teamId);
    }
    return false;
}

bool ArsTeamRepository::saveTeam(const ArsTeam &team, QString *errorMessage) const
{
    if (team.teamId <= 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Invalid team_id";
        }
        return false;
    }
    QString dirError;
    if (!ensureTeamsDir(&dirError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = dirError;
        }
        return false;
    }

    QJsonObject o;
    o["team_num"] = team.teamNum;
    o["team_id"] = team.teamId;
    o["name"] = team.name;
    o["age_category"] = team.ageCategory;
    QJsonArray coaches;
    for (const QString &c : team.defaultCoaches)
    {
        coaches.append(c);
    }
    o["default_coaches"] = coaches;

    QJsonObject thresholds;
    thresholds["plannedMetrics"] = planned_metrics_to_json(team.defaultPlannedMetrics);
    o["default_thresholds"] = thresholds;

    QJsonObject logo;
    logo["path"] = team.teamLogoPath;
    o["team_logo"] = logo;

    QFile f(teamFilePath(team.teamId));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to write team file: %1").arg(f.errorString());
        }
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

int ArsTeamRepository::nextTeamId(QStringList *warnings) const
{
    const QList<ArsTeam> teams = loadTeams(warnings);
    int maxId = 0;
    for (const ArsTeam &t : teams)
    {
        maxId = std::max(maxId, t.teamId);
    }
    return maxId + 1;
}
