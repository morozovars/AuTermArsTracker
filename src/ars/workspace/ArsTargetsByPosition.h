#pragma once

#include "ArsTeamRepository.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

using ArsTargetsByPosition = QMap<QString, ArsPlannedMetrics>;

QStringList arsTargetPositionKeys();
QString arsTargetPositionLabel(const QString &key);
QString arsNormalizePlayerPositionToTargetKey(const QString &position);

QJsonObject arsPlannedMetricsToJson(const ArsPlannedMetrics &m);
ArsPlannedMetrics arsPlannedMetricsFromJson(const QJsonObject &o);

QJsonObject arsTargetsByPositionToJson(const ArsTargetsByPosition &targets);
ArsTargetsByPosition arsTargetsByPositionFromJson(const QJsonObject &obj);
ArsTargetsByPosition arsTargetsByPositionFromLegacy(const ArsPlannedMetrics &legacy);

ArsPlannedMetrics arsResolveEffectiveTargets(const ArsTargetsByPosition &sessionOverrides,
                                             const ArsTargetsByPosition &teamDefaults,
                                             const QString &positionKey,
                                             const ArsPlannedMetrics &legacyFallback);
