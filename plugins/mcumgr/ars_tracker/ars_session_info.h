#ifndef ARS_SESSION_INFO_H
#define ARS_SESSION_INFO_H

#include <QTime>
#include <QString>
#include <QStringList>

struct ArsSessionTargetSettings
{
		double targetDistanceKm = 0.0;
		int targetAccelerationDistanceM = 0;
		int targetFootload10_3g = 0;
		int targetTouchesCount = 0;
		double targetFootloadPerMin = 0.0;
};

struct ArsSessionParameters
{
		QString type;
		QTime startTime;
		QTime endTime;
		QString location;
		QStringList goals;
};

struct ArsSessionPlannedMetrics
{
		double distanceKm = 0.0;
		int accelerationDistanceM = 0;
		int footloadPerLeg = 0;
		double loadIntensityGPerMin = 0.0;
		double maxSpeedMps = 0.0;
		int touches = 0;
		int shots = 0;
		int dribbles = 0;
};

struct ArsSessionInfo
{
		ArsSessionParameters parameters;
		ArsSessionPlannedMetrics plannedMetrics;
};

#endif // ARS_SESSION_INFO_H
