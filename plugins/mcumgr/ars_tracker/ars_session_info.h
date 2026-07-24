#ifndef ARS_SESSION_INFO_H
#define ARS_SESSION_INFO_H

#include <QTime>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <cstdint>
#include <QList>
#include <QMap>
#include "ars/workspace/ArsTargetsByPosition.h"

struct ArsSessionTargetSettings
{
		double targetDistanceKm = 0.0;
		int targetAccelerationDistanceM = 0;
		int targetFootload10_3g = 0;
		int targetTouchesCount = 0;
		double targetFootloadPerMin = 0.0;
		double targetMaxSpeedMps = 0.0;
		int targetShotsCount = 0;
		int targetPossessions = 0; // Stored as plannedMetrics.dribbles by current schema contract.
};

struct ArsSessionParameters
{
		QString type;
		QTime startTime;
		QTime endTime;
		QString location;
		QStringList goals;
};

using ArsSessionPlannedMetrics = ArsPlannedMetrics;
using ArsSessionTargetsByPosition = ArsTargetsByPosition;

// Named part of the session (warm-up, exercise, game). Stored as SessionInfo.json "exercises" entry.
struct ArsSessionSegment
{
		QString name;
		QTime startTime;
		QTime endTime;
};

struct ArsSessionInfo
{
		ArsSessionParameters parameters;
		ArsSessionPlannedMetrics plannedMetrics;
		ArsSessionTargetsByPosition targetsByPosition;
		QList<ArsSessionSegment> segments;
		bool hasPlannedSessionPeriod = false;
		struct ArsSessionActualTime
		{
				bool valid = false;
				QString startTime;
				QString finishTime;
				QString duration;
				qint64 durationMs = 0;
				uint32_t maxIntegralTimestamp100ms = 0;
		} actualTime;
};

#endif // ARS_SESSION_INFO_H
