#pragma once

#include <QDate>
#include <QString>

struct ArsPersonalNorms
{
    double maxSpeedMps = 0.0;
    double maxShotLoadG = 0.0;
};

struct ArsPlayer
{
    QString playerId;
    QString surname;
    QString name;
    QString photoPath;
    int number = 0;
    QDate birthDate;
    QString position;
    QString dominantFoot = "R";
    int heightCm = 0;
    int weightKg = 0;
    int teamId = -1;
    ArsPersonalNorms personalNorms;
};
