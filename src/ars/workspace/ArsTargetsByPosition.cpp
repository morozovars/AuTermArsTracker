#include "ArsTargetsByPosition.h"

#include <QSet>

namespace
{
ArsPlannedMetrics emptyMetrics()
{
    return ArsPlannedMetrics{};
}
}

QStringList arsTargetPositionKeys()
{
    return {"goalkeeper", "defender", "midfielder", "forward"};
}

QString arsTargetPositionLabel(const QString &key)
{
    if (key == "goalkeeper") return QString::fromUtf8("Вратарь");
    if (key == "defender") return QString::fromUtf8("Защитник");
    if (key == "midfielder") return QString::fromUtf8("Полузащитник");
    if (key == "forward") return QString::fromUtf8("Нападающий");
    return key;
}

QString arsNormalizePlayerPositionToTargetKey(const QString &position)
{
    const QString p = position.trimmed().toLower();
    if (p.isEmpty())
    {
        return "midfielder";
    }

    static const QStringList goalkeeperTokens = {
        QString::fromUtf8("вратарь"), "goalkeeper", "keeper", "goalie"
    };
    static const QStringList defenderTokens = {
        QString::fromUtf8("защитник"), QString::fromUtf8("центрбек"), QString::fromUtf8("левый защитник"),
        QString::fromUtf8("правый защитник"), "defender", "centre-back", "center-back", "fullback", "back"
    };
    static const QStringList midfielderTokens = {
        QString::fromUtf8("полузащитник"), QString::fromUtf8("опорный"), QString::fromUtf8("атакующий полузащитник"),
        QString::fromUtf8("центральный полузащитник"), "midfielder", "midfield", "dm", "cm", "am"
    };
    static const QStringList forwardTokens = {
        QString::fromUtf8("нападающий"), QString::fromUtf8("форвард"), QString::fromUtf8("вингер"),
        QString::fromUtf8("центральный нападающий"), "forward", "striker", "winger", "fw", "st"
    };

    auto containsToken = [&p](const QStringList &tokens) {
        for (const QString &token : tokens)
        {
            if (!token.trimmed().isEmpty() && p.contains(token.toLower()))
            {
                return true;
            }
        }
        return false;
    };

    if (containsToken(goalkeeperTokens)) return "goalkeeper";
    if (containsToken(defenderTokens)) return "defender";
    if (containsToken(forwardTokens)) return "forward";
    if (containsToken(midfielderTokens)) return "midfielder";

    return "midfielder";
}

QJsonObject arsPlannedMetricsToJson(const ArsPlannedMetrics &m)
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

ArsPlannedMetrics arsPlannedMetricsFromJson(const QJsonObject &o)
{
    ArsPlannedMetrics m;
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

QJsonObject arsTargetsByPositionToJson(const ArsTargetsByPosition &targets)
{
    QJsonObject out;
    for (const QString &key : arsTargetPositionKeys())
    {
        const ArsPlannedMetrics m = targets.value(key, emptyMetrics());
        out.insert(key, arsPlannedMetricsToJson(m));
    }
    for (auto it = targets.cbegin(); it != targets.cend(); ++it)
    {
        if (!out.contains(it.key()))
        {
            out.insert(it.key(), arsPlannedMetricsToJson(it.value()));
        }
    }
    return out;
}

ArsTargetsByPosition arsTargetsByPositionFromJson(const QJsonObject &obj)
{
    ArsTargetsByPosition out;
    for (auto it = obj.begin(); it != obj.end(); ++it)
    {
        if (it.value().isObject())
        {
            out.insert(it.key(), arsPlannedMetricsFromJson(it.value().toObject()));
        }
    }
    return out;
}

ArsTargetsByPosition arsTargetsByPositionFromLegacy(const ArsPlannedMetrics &legacy)
{
    ArsTargetsByPosition out;
    for (const QString &key : arsTargetPositionKeys())
    {
        out.insert(key, legacy);
    }
    return out;
}

ArsPlannedMetrics arsResolveEffectiveTargets(const ArsTargetsByPosition &sessionOverrides,
                                             const ArsTargetsByPosition &teamDefaults,
                                             const QString &positionKey,
                                             const ArsPlannedMetrics &legacyFallback)
{
    const QString key = positionKey.trimmed().isEmpty() ? QString("midfielder") : positionKey.trimmed();
    if (sessionOverrides.contains(key))
    {
        return sessionOverrides.value(key);
    }
    if (teamDefaults.contains(key))
    {
        return teamDefaults.value(key);
    }
    return legacyFallback;
}
