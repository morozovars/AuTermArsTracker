#include "ars_session_info_json.h"

#include <QDir>
#include <QFile>
#include <QChar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

#include "ars/workspace/ArsTargetsByPosition.h"

namespace
{
QTime parse_time_value(const QJsonValue &value, bool *ok = nullptr)
{
		const QString raw = value.toString().trimmed();
		QTime t = QTime::fromString(raw, "HH:mm:ss");
		if (!t.isValid())
		{
				t = QTime::fromString(raw, "HH:mm");
		}
		if (ok != nullptr)
		{
				*ok = t.isValid();
		}
		return t;
}

bool load_session_info_root(const QString &filePath,
														bool *outExists,
														QJsonObject *outRoot,
														QString *errorMessage)
{
		if (outExists != nullptr)
		{
				*outExists = false;
		}
		if (outRoot == nullptr)
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = "Output JSON object pointer is null";
				}
				return false;
		}
		*outRoot = QJsonObject();
		QFile file(filePath);
		if (!file.exists())
		{
				return true;
		}
		if (outExists != nullptr)
		{
				*outExists = true;
		}
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = QString("Failed to open file for read: %1").arg(filePath);
				}
				return false;
		}
		QJsonParseError parseError;
		const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
		file.close();
		if (parseError.error != QJsonParseError::NoError || !doc.isObject())
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = QString("Invalid JSON in %1: %2").arg(filePath, parseError.errorString());
				}
				return false;
		}
		*outRoot = doc.object();
		return true;
}

QJsonArray segments_to_json(const QList<ArsSessionSegment> &segments, const QJsonValue &storedValue)
{
		// Session segments are stored as SessionInfo.json "exercises". Only name/startTime/endTime are edited
		// in AuTerm, so extra keys of already stored entries (skill, instructions, targetMetrics) are kept.
		QMap<QString, QJsonObject> storedByName;
		for (const QJsonValue &v : storedValue.toArray())
		{
				const QJsonObject o = v.toObject();
				const QString name = o.value("name").toString().trimmed();
				if (!name.isEmpty() && !storedByName.contains(name))
				{
						storedByName.insert(name, o);
				}
		}

		QJsonArray out;
		for (const ArsSessionSegment &segment : segments)
		{
				QJsonObject o = storedByName.value(segment.name.trimmed());
				o["name"] = segment.name;
				o["startTime"] = segment.startTime.toString("HH:mm:ss");
				o["endTime"] = segment.endTime.toString("HH:mm:ss");
				out.append(o);
		}
		return out;
}

QList<ArsSessionSegment> segments_from_json(const QJsonValue &value)
{
		QList<ArsSessionSegment> segments;
		if (!value.isArray())
		{
				return segments;
		}
		int index = 0;
		for (const QJsonValue &v : value.toArray())
		{
				const QJsonObject o = v.toObject();
				++index;
				bool startOk = false;
				bool endOk = false;
				ArsSessionSegment segment;
				segment.name = o.value("name").toString().trimmed();
				segment.startTime = parse_time_value(o.value("startTime"), &startOk);
				segment.endTime = parse_time_value(o.value("endTime"), &endOk);
				if (!startOk || !endOk)
				{
						qWarning() << "Sessions tab SessionInfo.json segment skipped, invalid time"
											 << "index=" << index
											 << "name=" << segment.name
											 << "startTime=" << o.value("startTime").toString()
											 << "endTime=" << o.value("endTime").toString();
						continue;
				}
				if (segment.name.isEmpty())
				{
						segment.name = QString("Segment %1").arg(index);
				}
				segments.append(segment);
		}
		return segments;
}

QJsonObject assignment_to_json(const ArsSessionPairAssignment &assignment)
{
		QJsonObject o;
		o["pairId"] = assignment.pairId;
		o["leftTrackerSerial"] = assignment.leftTrackerSerial;
		o["rightTrackerSerial"] = assignment.rightTrackerSerial;
		o["playerId"] = assignment.playerId;
		o["playerName"] = assignment.playerName;
		o["teamId"] = assignment.teamId;
		o["source"] = assignment.source;
		o["isOverride"] = assignment.isOverride;
		return o;
}

ArsSessionPairAssignment assignment_from_json(const QJsonObject &o)
{
		ArsSessionPairAssignment assignment;
		assignment.pairId = o.value("pairId").toString().trimmed();
		assignment.leftTrackerSerial = o.value("leftTrackerSerial").toString().trimmed();
		assignment.rightTrackerSerial = o.value("rightTrackerSerial").toString().trimmed();
		assignment.playerId = o.value("playerId").toString().trimmed();
		assignment.playerName = o.value("playerName").toString().trimmed();
		assignment.teamId = o.value("teamId").toInt(-1);
		assignment.source = o.value("source").toString().trimmed();
		assignment.isOverride = o.value("isOverride").toBool(false);
		return assignment;
}
}

bool ArsSessionInfoJson::saveSessionInfoJson(const QString &sessionPath,
																						 const ArsSessionInfo &info,
																						 QString *errorMessage)
{
		const QString filePath = QDir(sessionPath).filePath("SessionInfo.json");
		QJsonObject root;
		bool fileExists = false;
		QString loadError;
		if (!load_session_info_root(filePath, &fileExists, &root, &loadError))
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = loadError;
				}
				return false;
		}
		QJsonObject planned;
		planned["distanceKm"] = info.plannedMetrics.distanceKm;
		planned["accelerationDistanceM"] = info.plannedMetrics.accelerationDistanceM;
		planned["footloadPerLeg"] = info.plannedMetrics.footloadPerLeg;
		planned["loadIntensityGPerMin"] = info.plannedMetrics.loadIntensityGPerMin;
		planned["maxSpeedMps"] = info.plannedMetrics.maxSpeedMps;
		planned["touches"] = info.plannedMetrics.touches;
		planned["shots"] = info.plannedMetrics.shots;
		planned["dribbles"] = info.plannedMetrics.dribbles;

		QJsonArray goals;
		for (const QString &goal : info.parameters.goals)
		{
				goals.append(goal);
		}

		root["plannedMetrics"] = planned;
		root["targetsByPosition"] = arsTargetsByPositionToJson(info.targetsByPosition);
		root["exercises"] = segments_to_json(info.segments, root.value("exercises"));
		root["startTime"] = info.parameters.startTime.toString("HH:mm:ss");
		root["endTime"] = info.parameters.endTime.toString("HH:mm:ss");
		// TODO: rename root startTime/endTime to plannedStartTime/plannedEndTime in a future schema migration.
		root["type"] = info.parameters.type;
		root["location"] = info.parameters.location;
		root["goals"] = goals;
		if (info.actualTime.valid)
		{
				QJsonObject actual;
				actual["startTime"] = info.actualTime.startTime;
				actual["finishTime"] = info.actualTime.finishTime;
				actual["duration"] = info.actualTime.duration;
				actual["durationMs"] = static_cast<qint64>(info.actualTime.durationMs);
				actual["maxIntegralTimestamp100ms"] = static_cast<qint64>(info.actualTime.maxIntegralTimestamp100ms);
				root["actualTime"] = actual;
		}

		QFile file(filePath);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = QString("Failed to open file for write: %1").arg(filePath);
				}
				return false;
		}
		file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
		file.close();
		return true;
}

bool ArsSessionInfoJson::loadSessionInfoJson(const QString &sessionPath,
																						 ArsSessionInfo *outInfo,
																						 bool *outFileExists,
																						 bool *outHasPlannedSessionPeriod,
																						 QString *errorMessage)
{
		if (outFileExists != nullptr)
		{
				*outFileExists = false;
		}
		if (outHasPlannedSessionPeriod != nullptr)
		{
				*outHasPlannedSessionPeriod = false;
		}
		if (outInfo == nullptr)
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = "Output session info pointer is null";
				}
				return false;
		}
		const QString filePath = QDir(sessionPath).filePath("SessionInfo.json");
		QFile file(filePath);
		if (!file.exists())
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = QString("SessionInfo.json is missing: %1").arg(filePath);
				}
				return false;
		}
		if (outFileExists != nullptr)
		{
				*outFileExists = true;
		}
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = QString("Failed to open file for read: %1").arg(filePath);
				}
				return false;
		}
		const QByteArray bytes = file.readAll();
		file.close();

		QJsonParseError parseError;
		const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
		if (parseError.error != QJsonParseError::NoError || !doc.isObject())
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = QString("Invalid JSON in %1: %2").arg(filePath, parseError.errorString());
				}
				return false;
		}

		const QJsonObject root = doc.object();
		ArsSessionInfo info;
		info.parameters.type = root.value("type").toString().trimmed();
		info.parameters.location = root.value("location").toString();

		bool startOk = false;
		bool endOk = false;
		info.parameters.startTime = parse_time_value(root.value("startTime"), &startOk);
		info.parameters.endTime = parse_time_value(root.value("endTime"), &endOk);
		if (!startOk)
		{
				info.parameters.startTime = QTime();
		}
		if (!endOk)
		{
				info.parameters.endTime = QTime();
		}
		info.hasPlannedSessionPeriod = startOk && endOk;
		if (outHasPlannedSessionPeriod != nullptr)
		{
				*outHasPlannedSessionPeriod = info.hasPlannedSessionPeriod;
		}

		const QJsonValue goalsValue = root.value("goals");
		if (goalsValue.isArray())
		{
				for (const QJsonValue &v : goalsValue.toArray())
				{
						const QString s = v.toString().trimmed();
						if (!s.isEmpty())
						{
								info.parameters.goals.append(s);
						}
				}
		}
		else if (goalsValue.isString())
		{
				const QStringList lines = goalsValue.toString().split('\n');
				for (const QString &line : lines)
				{
						const QString trimmed = line.trimmed();
						if (!trimmed.isEmpty())
						{
								info.parameters.goals.append(trimmed);
						}
				}
		}

		const QJsonObject planned = root.value("plannedMetrics").toObject();
		info.plannedMetrics.distanceKm = planned.value("distanceKm").toDouble(0.0);
		info.plannedMetrics.accelerationDistanceM = planned.value("accelerationDistanceM").toInt(0);
		info.plannedMetrics.footloadPerLeg = planned.value("footloadPerLeg").toInt(0);
		info.plannedMetrics.loadIntensityGPerMin = planned.value("loadIntensityGPerMin").toDouble(0.0);
		info.plannedMetrics.maxSpeedMps = planned.value("maxSpeedMps").toDouble(0.0);
		info.plannedMetrics.touches = planned.value("touches").toInt(0);
		info.plannedMetrics.shots = planned.value("shots").toInt(0);
		info.plannedMetrics.dribbles = planned.value("dribbles").toInt(0);
		info.targetsByPosition = arsTargetsByPositionFromJson(root.value("targetsByPosition").toObject());
		if (info.targetsByPosition.isEmpty())
		{
				ArsPlannedMetrics legacy;
				legacy.accelerationDistanceM = info.plannedMetrics.accelerationDistanceM;
				legacy.distanceKm = info.plannedMetrics.distanceKm;
				legacy.dribbles = info.plannedMetrics.dribbles;
				legacy.footloadPerLeg = info.plannedMetrics.footloadPerLeg;
				legacy.loadIntensityGPerMin = info.plannedMetrics.loadIntensityGPerMin;
				legacy.maxSpeedMps = info.plannedMetrics.maxSpeedMps;
				legacy.shots = info.plannedMetrics.shots;
				legacy.touches = info.plannedMetrics.touches;
				const ArsTargetsByPosition fallback = arsTargetsByPositionFromLegacy(legacy);
				for (auto it = fallback.cbegin(); it != fallback.cend(); ++it)
				{
						ArsSessionPlannedMetrics m;
						m.accelerationDistanceM = it.value().accelerationDistanceM;
						m.distanceKm = it.value().distanceKm;
						m.dribbles = it.value().dribbles;
						m.footloadPerLeg = it.value().footloadPerLeg;
						m.loadIntensityGPerMin = it.value().loadIntensityGPerMin;
						m.maxSpeedMps = it.value().maxSpeedMps;
						m.shots = it.value().shots;
						m.touches = it.value().touches;
						info.targetsByPosition.insert(it.key(), m);
				}
		}

		info.segments = segments_from_json(root.value("exercises"));

		const QJsonObject actual = root.value("actualTime").toObject();
		const QString actualStart = actual.value("startTime").toString().trimmed();
		const QString actualFinish = actual.value("finishTime").toString().trimmed();
		QString actualDuration = actual.value("duration").toString().trimmed();
		const qint64 durationMs = actual.value("durationMs").toVariant().toLongLong();
		const uint32_t maxTs = static_cast<uint32_t>(actual.value("maxIntegralTimestamp100ms").toVariant().toULongLong());
		if (!actualStart.isEmpty() && !actualFinish.isEmpty())
		{
				if (actualDuration.isEmpty() && durationMs >= 0)
				{
						const qint64 totalSeconds = durationMs / 1000;
						const qint64 hours = totalSeconds / 3600;
						const qint64 minutes = (totalSeconds % 3600) / 60;
						const qint64 seconds = totalSeconds % 60;
						actualDuration = QString("%1:%2:%3")
								.arg(hours, 2, 10, QChar('0'))
								.arg(minutes, 2, 10, QChar('0'))
								.arg(seconds, 2, 10, QChar('0'));
				}
				if (!actualDuration.isEmpty())
				{
						info.actualTime.valid = true;
						info.actualTime.startTime = actualStart;
						info.actualTime.finishTime = actualFinish;
						info.actualTime.duration = actualDuration;
						info.actualTime.durationMs = durationMs > 0 ? durationMs : 0;
						info.actualTime.maxIntegralTimestamp100ms = maxTs;
				}
		}

		*outInfo = info;
		return true;
}

bool ArsSessionInfoJson::updateSessionInfoActualTimeJson(const QString &sessionPath,
																												 const ArsSessionInfo::ArsSessionActualTime &actualTime,
																												 QString *errorMessage)
{
		if (!actualTime.valid)
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = "actualTime is invalid";
				}
				return false;
		}
		const QString filePath = QDir(sessionPath).filePath("SessionInfo.json");
		qDebug() << "Sessions tab SessionInfo actualTime update begin" << "path=" << filePath;
		QJsonObject root;
		QFile file(filePath);
		if (file.exists())
		{
				qDebug() << "Sessions tab SessionInfo actualTime update merge existing" << "path=" << filePath;
				if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
				{
						if (errorMessage != nullptr)
						{
								*errorMessage = QString("Failed to open JSON for read: %1").arg(filePath);
						}
						return false;
				}
				const QByteArray bytes = file.readAll();
				file.close();
				QJsonParseError parseError;
				const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
				if (parseError.error != QJsonParseError::NoError || !doc.isObject())
				{
						if (errorMessage != nullptr)
						{
								*errorMessage = QString("Invalid JSON in %1: %2").arg(filePath, parseError.errorString());
						}
						return false;
				}
				root = doc.object();
		}
		else
		{
				qDebug() << "Sessions tab SessionInfo actualTime update create new" << "path=" << filePath;
		}

		// TODO: keep planned session period separate from actual processed session time.
		QJsonObject actual;
		actual["startTime"] = actualTime.startTime;
		actual["finishTime"] = actualTime.finishTime;
		actual["duration"] = actualTime.duration;
		actual["durationMs"] = static_cast<qint64>(actualTime.durationMs);
		actual["maxIntegralTimestamp100ms"] = static_cast<qint64>(actualTime.maxIntegralTimestamp100ms);
		root["actualTime"] = actual;

		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = QString("Failed to open JSON for write: %1").arg(filePath);
				}
				return false;
		}
		file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
		file.close();
		qDebug() << "Sessions tab SessionInfo actualTime updated" << "path=" << filePath;
		return true;
}

bool ArsSessionInfoJson::loadSessionTeamId(const QString &sessionPath,
																					 bool *outHasTeamId,
																					 int *outTeamId,
																					 QString *errorMessage)
{
		if (outHasTeamId == nullptr || outTeamId == nullptr)
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = "Output TeamId pointers are null";
				}
				return false;
		}
		*outHasTeamId = false;
		*outTeamId = -1;

		const QString filePath = QDir(sessionPath).filePath("SessionInfo.json");
		QJsonObject root;
		QString loadError;
		bool exists = false;
		if (!load_session_info_root(filePath, &exists, &root, &loadError))
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = loadError;
				}
				return false;
		}
		if (!exists)
		{
				return true;
		}

		const QJsonValue teamIdValue = root.value("TeamId");
		if (teamIdValue.isDouble())
		{
				*outHasTeamId = true;
				*outTeamId = teamIdValue.toInt(-1);
		}
		return true;
}

bool ArsSessionInfoJson::saveSessionTeamId(const QString &sessionPath, int teamId, QString *errorMessage)
{
		const QString filePath = QDir(sessionPath).filePath("SessionInfo.json");
		QJsonObject root;
		bool exists = false;
		QString loadError;
		if (!load_session_info_root(filePath, &exists, &root, &loadError))
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = loadError;
				}
				return false;
		}
		root["TeamId"] = teamId;

		QFile file(filePath);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = QString("Failed to open file for write: %1").arg(filePath);
				}
				return false;
		}
		file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
		file.close();
		return true;
}

bool ArsSessionInfoJson::readSessionPairAssignments(const QString &sessionPath,
																										QList<ArsSessionPairAssignment> *outAssignments,
																										QString *errorMessage)
{
		if (outAssignments == nullptr)
		{
				if (errorMessage != nullptr) *errorMessage = "Output assignments pointer is null";
				return false;
		}
		outAssignments->clear();
		const QString filePath = QDir(sessionPath).filePath("SessionInfo.json");
		QJsonObject root;
		bool exists = false;
		QString loadError;
		if (!load_session_info_root(filePath, &exists, &root, &loadError))
		{
				if (errorMessage != nullptr) *errorMessage = loadError;
				return false;
		}
		if (!exists)
		{
				return true;
		}
		const QJsonArray arr = root.value("trackerPlayerBindings").toArray();
		for (const QJsonValue &value : arr)
		{
				if (!value.isObject()) continue;
				const ArsSessionPairAssignment assignment = assignment_from_json(value.toObject());
				if (assignment.pairId.trimmed().isEmpty()) continue;
				outAssignments->append(assignment);
		}
		return true;
}

bool ArsSessionInfoJson::writeSessionPairAssignments(const QString &sessionPath,
																										 const QList<ArsSessionPairAssignment> &assignments,
																										 QString *errorMessage)
{
		const QString filePath = QDir(sessionPath).filePath("SessionInfo.json");
		QJsonObject root;
		bool exists = false;
		QString loadError;
		if (!load_session_info_root(filePath, &exists, &root, &loadError))
		{
				if (errorMessage != nullptr) *errorMessage = loadError;
				qWarning() << "Session assignments write failed session=" << sessionPath << "error=" << loadError;
				return false;
		}

		QJsonArray arr;
		for (const ArsSessionPairAssignment &assignment : assignments)
		{
				arr.append(assignment_to_json(assignment));
		}
		root["trackerPlayerBindings"] = arr;

		QFile file(filePath);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		{
				if (errorMessage != nullptr) *errorMessage = QString("Failed to open file for write: %1").arg(filePath);
				qWarning() << "Session assignments write failed session=" << sessionPath << "error=" << (errorMessage ? *errorMessage : QString());
				return false;
		}
		file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
		file.close();
		return true;
}

bool ArsSessionInfoJson::upsertManualSessionAssignment(const QString &sessionPath,
																											 const ArsSessionPairAssignment &manualAssignment,
																											 QString *errorMessage)
{
		QList<ArsSessionPairAssignment> assignments;
		if (!readSessionPairAssignments(sessionPath, &assignments, errorMessage))
		{
				return false;
		}
		bool replaced = false;
		for (ArsSessionPairAssignment &assignment : assignments)
		{
				if (assignment.pairId.compare(manualAssignment.pairId, Qt::CaseInsensitive) == 0)
				{
						assignment = manualAssignment;
						assignment.source = "manual";
						assignment.isOverride = true;
						replaced = true;
						break;
				}
		}
		if (!replaced)
		{
				ArsSessionPairAssignment value = manualAssignment;
				value.source = "manual";
				value.isOverride = true;
				assignments.append(value);
		}
		return writeSessionPairAssignments(sessionPath, assignments, errorMessage);
}
