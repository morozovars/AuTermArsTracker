#include "ArsTargetsByPosition.h"

#include "ArsPlayerPosition.h"

namespace
{
ArsPlannedMetrics emptyMetrics()
{
    return ArsPlannedMetrics{};
}
}

QStringList arsTargetPositionKeys()
{
    return arsPlayerPositionKeys();
}

QString arsTargetPositionLabel(const QString &key)
{
    return arsPlayerPositionDisplayName(key);
}

QString arsNormalizePlayerPositionToTargetKey(const QString &position)
{
    return arsNormalizePlayerPositionKey(position);
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
    return out;
}

ArsTargetsByPosition arsTargetsByPositionFromJson(const QJsonObject &obj)
{
    ArsTargetsByPosition out;

    for (const QString &key : arsTargetPositionKeys())
    {
        const QJsonValue value = obj.value(key);
        if (value.isObject())
        {
            out.insert(key, arsPlannedMetricsFromJson(value.toObject()));
        }
    }

    if (obj.value("defender").isObject())
    {
        const ArsPlannedMetrics m = arsPlannedMetricsFromJson(obj.value("defender").toObject());
        if (!out.contains("wide_defender")) out.insert("wide_defender", m);
        if (!out.contains("central_defender")) out.insert("central_defender", m);
    }

    if (obj.value("midfielder").isObject())
    {
        const ArsPlannedMetrics m = arsPlannedMetricsFromJson(obj.value("midfielder").toObject());
        if (!out.contains("wide_midfielder")) out.insert("wide_midfielder", m);
        if (!out.contains("central_midfielder")) out.insert("central_midfielder", m);
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
    const QString normalizedKey = arsNormalizePlayerPositionToTargetKey(positionKey);
    if (sessionOverrides.contains(normalizedKey))
    {
        return sessionOverrides.value(normalizedKey);
    }
    if (teamDefaults.contains(normalizedKey))
    {
        return teamDefaults.value(normalizedKey);
    }
    return legacyFallback;
}
