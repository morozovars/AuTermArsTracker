#include "ArsTrackerBindingRepository.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <algorithm>

namespace
{
QJsonObject binding_to_json(const ArsTrackerPlayerBinding &binding)
{
    QJsonObject o;
    o["teamId"] = binding.teamId;
    o["playerId"] = binding.playerId;
    o["pairId"] = binding.pairId;
    o["leftTrackerSerial"] = binding.leftTrackerSerial;
    o["rightTrackerSerial"] = binding.rightTrackerSerial;
    o["updatedAt"] = binding.updatedAt.isValid() ? binding.updatedAt.toUTC().toString(Qt::ISODate) : QString();
    return o;
}

ArsTrackerPlayerBinding binding_from_json(const QJsonObject &o)
{
    ArsTrackerPlayerBinding binding;
    binding.teamId = o.value("teamId").toInt(-1);
    binding.playerId = o.value("playerId").toString().trimmed();
    binding.pairId = o.value("pairId").toString().trimmed();
    binding.leftTrackerSerial = o.value("leftTrackerSerial").toString().trimmed();
    binding.rightTrackerSerial = o.value("rightTrackerSerial").toString().trimmed();
    binding.updatedAt = QDateTime::fromString(o.value("updatedAt").toString().trimmed(), Qt::ISODate);
    return binding;
}
}

ArsTrackerBindingRepository::ArsTrackerBindingRepository(const QString workspaceRootPath)
    : m_workspaceRootPath(workspaceRootPath)
{
}

QString ArsTrackerBindingRepository::bindingsPath() const
{
    return QDir(m_workspaceRootPath).filePath("tracker_bindings.json");
}

QList<ArsTrackerPlayerBinding> ArsTrackerBindingRepository::loadBindings(QStringList *warnings) const
{
    QList<ArsTrackerPlayerBinding> out;
    QFile file(bindingsPath());
    if (!file.exists())
    {
        qDebug() << "Ars Tracker bindings load path=" << bindingsPath() << "count=" << 0;
        return out;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (warnings != nullptr)
        {
            warnings->append(QString("Tracker bindings read failed path=%1 error=%2").arg(bindingsPath(), file.errorString()));
        }
        return out;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (warnings != nullptr)
        {
            warnings->append(QString("Tracker bindings read failed path=%1 error=%2").arg(bindingsPath(), pe.errorString()));
        }
        return out;
    }

    const QJsonArray arr = doc.object().value("bindings").toArray();
    for (const QJsonValue &value : arr)
    {
        if (!value.isObject())
        {
            continue;
        }
        ArsTrackerPlayerBinding binding = binding_from_json(value.toObject());
        if (binding.teamId <= 0 || binding.pairId.trimmed().isEmpty())
        {
            if (warnings != nullptr)
            {
                warnings->append(QString("Tracker bindings read failed path=%1 error=invalid binding entry").arg(bindingsPath()));
            }
            continue;
        }
        out.append(binding);
    }
    qDebug() << "Ars Tracker bindings load path=" << bindingsPath() << "count=" << out.size();
    return out;
}

bool ArsTrackerBindingRepository::saveBindings(const QList<ArsTrackerPlayerBinding> &bindings, QString *errorMessage) const
{
    const QString rootDir = QFileInfo(bindingsPath()).absolutePath();
    if (!QDir(rootDir).exists() && !QDir().mkpath(rootDir))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to create directory for bindings: %1").arg(rootDir);
        }
        return false;
    }

    QJsonArray arr;
    for (const ArsTrackerPlayerBinding &binding : bindings)
    {
        arr.append(binding_to_json(binding));
    }
    QJsonObject root;
    root["bindings"] = arr;

    QFile file(bindingsPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to write tracker bindings: %1").arg(file.errorString());
        }
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    qDebug() << "Ars Tracker binding saved count=" << bindings.size();
    return true;
}

bool ArsTrackerBindingRepository::findBinding(int teamId,
                                              const QString &pairId,
                                              ArsTrackerPlayerBinding *outBinding,
                                              QString *errorMessage) const
{
    if (outBinding == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Output pointer is null";
        }
        return false;
    }
    QStringList warnings;
    const QList<ArsTrackerPlayerBinding> bindings = loadBindings(&warnings);
    for (const QString &warning : warnings)
    {
        qWarning().noquote() << warning;
    }
    for (const ArsTrackerPlayerBinding &binding : bindings)
    {
        if (binding.teamId == teamId && binding.pairId.compare(pairId.trimmed(), Qt::CaseInsensitive) == 0)
        {
            *outBinding = binding;
            return true;
        }
    }
    if (errorMessage != nullptr)
    {
        *errorMessage = QString("Binding not found teamId=%1 pairId=%2").arg(teamId).arg(pairId);
    }
    return false;
}

bool ArsTrackerBindingRepository::upsertBinding(const ArsTrackerPlayerBinding &binding, QString *errorMessage)
{
    if (binding.teamId <= 0 || binding.pairId.trimmed().isEmpty() || binding.playerId.trimmed().isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Invalid binding input";
        }
        return false;
    }

    QStringList warnings;
    QList<ArsTrackerPlayerBinding> bindings = loadBindings(&warnings);
    for (const QString &warning : warnings)
    {
        qWarning().noquote() << warning;
    }

    bool replaced = false;
    for (ArsTrackerPlayerBinding &existing : bindings)
    {
        if (existing.teamId == binding.teamId && existing.pairId.compare(binding.pairId, Qt::CaseInsensitive) == 0)
        {
            if (existing.playerId != binding.playerId)
            {
                qDebug() << "Ars Tracker binding conflict pairId=" << binding.pairId
                         << "oldPlayerId=" << existing.playerId
                         << "newPlayerId=" << binding.playerId;
            }
            existing = binding;
            replaced = true;
            break;
        }
    }
    if (!replaced)
    {
        bindings.append(binding);
    }

    qDebug() << "Ars Tracker binding upsert teamId=" << binding.teamId << "pairId=" << binding.pairId << "playerId=" << binding.playerId;
    return saveBindings(bindings, errorMessage);
}

bool ArsTrackerBindingRepository::removeBinding(int teamId, const QString &pairId, QString *errorMessage)
{
    QStringList warnings;
    QList<ArsTrackerPlayerBinding> bindings = loadBindings(&warnings);
    for (const QString &warning : warnings)
    {
        qWarning().noquote() << warning;
    }
    const int before = bindings.size();
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [teamId, pairId](const ArsTrackerPlayerBinding &b) {
                       return b.teamId == teamId && b.pairId.compare(pairId.trimmed(), Qt::CaseInsensitive) == 0;
                   }),
                   bindings.end());
    if (before == bindings.size())
    {
        return true;
    }
    return saveBindings(bindings, errorMessage);
}

QList<ArsTrackerPlayerBinding> ArsTrackerBindingRepository::bindingsForTeam(int teamId, QStringList *warnings) const
{
    QList<ArsTrackerPlayerBinding> bindings = loadBindings(warnings);
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [teamId](const ArsTrackerPlayerBinding &b) { return b.teamId != teamId; }),
                   bindings.end());
    return bindings;
}

QList<ArsTrackerPlayerBinding> ArsTrackerBindingRepository::bindingsForPlayer(const QString &playerId, QStringList *warnings) const
{
    const QString id = playerId.trimmed();
    QList<ArsTrackerPlayerBinding> bindings = loadBindings(warnings);
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [id](const ArsTrackerPlayerBinding &b) {
                       return b.playerId.compare(id, Qt::CaseInsensitive) != 0;
                   }),
                   bindings.end());
    return bindings;
}
