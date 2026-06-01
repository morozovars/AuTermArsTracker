#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct ArsPlayerPositionInfo
{
    QString key;
    QString displayName;
};

QList<ArsPlayerPositionInfo> arsPlayerPositions();

QStringList arsPlayerPositionKeys();

QString arsPlayerPositionDisplayName(const QString &key);

QString arsPlayerPositionKeyFromDisplayName(const QString &displayName);

QString arsNormalizePlayerPositionKey(const QString &value);

QString arsPlayerPositionDisplayNameFromAny(const QString &value);
