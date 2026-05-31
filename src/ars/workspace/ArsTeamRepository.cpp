#include "ArsTeamRepository.h"
#include "ArsAppSettings.h"
#include "ArsTargetsByPosition.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace
{
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

bool ArsTeamRepository::ensureInitialized()
{
    return ensureTeamsDir(nullptr);
}

QString ArsTeamRepository::filePath() const
{
    return QDir(m_workspacePath).filePath("team.json");
}

QString ArsTeamRepository::teamFilePath(int teamId) const
{
    return QDir(teamsPath()).filePath(QString("team_%1.json").arg(teamId));
}

QString ArsTeamRepository::teamAssetsPath(int teamId) const
{
    return QDir(teamsPath()).filePath(QString("assets/team_%1").arg(teamId));
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
        t.defaultPlannedMetrics = arsPlannedMetricsFromJson(thresholds.value("plannedMetrics").toObject());
        t.targetsByPosition = arsTargetsByPositionFromJson(o.value("targetsByPosition").toObject());
        if (t.targetsByPosition.isEmpty())
        {
            t.targetsByPosition = arsTargetsByPositionFromLegacy(t.defaultPlannedMetrics);
        }
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
    thresholds["plannedMetrics"] = arsPlannedMetricsToJson(team.defaultPlannedMetrics);
    o["default_thresholds"] = thresholds;
    o["targetsByPosition"] = arsTargetsByPositionToJson(team.targetsByPosition);

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

bool ArsTeamRepository::deleteTeam(int teamId, QString *errorMessage) const
{
    if (teamId <= 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Invalid team_id";
        }
        return false;
    }

    const QString jsonPath = teamFilePath(teamId);
    const QString assetsPath = teamAssetsPath(teamId);
    qDebug() << "Ars Team delete begin id=" << teamId << "jsonPath=" << jsonPath << "assetsPath=" << assetsPath;

    const QFileInfo jsonInfo(jsonPath);
    if (jsonInfo.exists())
    {
        if (!QFile::remove(jsonPath))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QString("Failed to delete team JSON: %1").arg(jsonPath);
            }
            qWarning() << "Ars Team delete failed id=" << teamId << "error=" << (errorMessage != nullptr ? *errorMessage : QString());
            return false;
        }
        qDebug() << "Ars Team delete json removed id=" << teamId << "path=" << jsonPath;
    }
    else
    {
        qWarning() << "Ars Team delete warning: JSON file missing id=" << teamId << "path=" << jsonPath;
    }

    QDir assetsDir(assetsPath);
    if (assetsDir.exists())
    {
        if (!assetsDir.removeRecursively())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QString("Failed to delete team assets: %1").arg(assetsPath);
            }
            qWarning() << "Ars Team delete failed id=" << teamId << "error=" << (errorMessage != nullptr ? *errorMessage : QString());
            return false;
        }
        qDebug() << "Ars Team delete assets removed id=" << teamId << "path=" << assetsPath;
    }

    qDebug() << "Ars Team delete done id=" << teamId;
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
    int lastTeamId = -1;
    QString settingsError;
    if (!ArsAppSettings(m_workspacePath).loadLastTeamId(&lastTeamId, &settingsError))
    {
        if (warnings != nullptr && !settingsError.trimmed().isEmpty())
        {
            warnings->append(QString("Ars Team load warning settings error=%1").arg(settingsError));
        }
    }
    maxId = std::max(maxId, lastTeamId);
    return maxId + 1;
}

QString ArsTeamRepository::workspaceRootPath() const
{
    return m_workspacePath;
}

QString ArsTeamRepository::resolveLogoAbsolutePath(const QString &logoPath) const
{
    const QString trimmed = logoPath.trimmed();
    if (trimmed.isEmpty())
    {
        return QString();
    }
    const QFileInfo info(trimmed);
    if (info.isAbsolute())
    {
        return QDir::cleanPath(info.absoluteFilePath());
    }
    return QDir::cleanPath(QDir(m_workspacePath).filePath(trimmed));
}

bool ArsTeamRepository::copyTeamLogoToWorkspace(int teamId,
                                                const QString &sourceLogoPath,
                                                QString *outRelativeLogoPath,
                                                QString *errorMessage) const
{
    if (outRelativeLogoPath != nullptr)
    {
        outRelativeLogoPath->clear();
    }
    if (teamId <= 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Invalid team_id for logo copy";
        }
        return false;
    }

    const QString source = sourceLogoPath.trimmed();
    if (source.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Logo source path is empty";
        }
        return false;
    }

    const QFileInfo sourceInfo(source);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Logo source does not exist: %1").arg(source);
        }
        return false;
    }

    const QString extension = sourceInfo.suffix().toLower();
    const QSet<QString> allowed = {"png", "jpg", "jpeg", "bmp", "svg"};
    if (!allowed.contains(extension))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Unsupported logo extension: .%1").arg(extension);
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

    const QString relativeDir = QString("teams/assets/team_%1").arg(teamId);
    const QString targetDirPath = QDir(m_workspacePath).filePath(relativeDir);
    if (!QDir(targetDirPath).exists() && !QDir().mkpath(targetDirPath))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to create logo dir: %1").arg(targetDirPath);
        }
        return false;
    }

    const QString relativeTarget = QDir::cleanPath(QString("%1/logo.%2").arg(relativeDir, extension));
    const QString absoluteTarget = QDir::cleanPath(QDir(m_workspacePath).filePath(relativeTarget));
    const QString absoluteSource = QDir::cleanPath(sourceInfo.absoluteFilePath());

    if (QString::compare(absoluteSource, absoluteTarget, Qt::CaseInsensitive) == 0)
    {
        if (outRelativeLogoPath != nullptr)
        {
            *outRelativeLogoPath = relativeTarget;
        }
        return true;
    }

    const QStringList oldLogos = {"logo.png", "logo.jpg", "logo.jpeg", "logo.bmp", "logo.svg"};
    for (const QString &oldName : oldLogos)
    {
        const QString oldPath = QDir(targetDirPath).filePath(oldName);
        if (QFileInfo::exists(oldPath) && QString::compare(QDir::cleanPath(oldPath), absoluteTarget, Qt::CaseInsensitive) != 0)
        {
            QFile::remove(oldPath);
        }
    }

    if (QFileInfo::exists(absoluteTarget) && !QFile::remove(absoluteTarget))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to replace old logo file: %1").arg(absoluteTarget);
        }
        return false;
    }

    if (!QFile::copy(absoluteSource, absoluteTarget))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to copy logo to workspace: %1").arg(absoluteTarget);
        }
        return false;
    }

    if (outRelativeLogoPath != nullptr)
    {
        *outRelativeLogoPath = relativeTarget;
    }
    return true;
}
