#pragma once

#include <QDateTime>
#include <QString>

struct ArsTrackerPlayerBinding
{
    int teamId = -1;
    QString playerId;
    QString pairId;
    QString leftTrackerSerial;
    QString rightTrackerSerial;
    QDateTime updatedAt;
};
