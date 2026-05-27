#ifndef ARS_SESSION_INFO_JSON_H
#define ARS_SESSION_INFO_JSON_H

#include <QString>

#include "ars_session_info.h"
#include "ars/workspace/ArsSessionPlayerBindingResolver.h"

class ArsSessionInfoJson
{
public:
		static bool saveSessionInfoJson(const QString &sessionPath,
																		const ArsSessionInfo &info,
																		QString *errorMessage = nullptr);
		static bool loadSessionInfoJson(const QString &sessionPath,
																		ArsSessionInfo *outInfo,
																		bool *outFileExists = nullptr,
																		bool *outHasPlannedSessionPeriod = nullptr,
																		QString *errorMessage = nullptr);
		static bool updateSessionInfoActualTimeJson(const QString &sessionPath,
																								const ArsSessionInfo::ArsSessionActualTime &actualTime,
																								QString *errorMessage = nullptr);
		static bool loadSessionTeamId(const QString &sessionPath,
																	bool *outHasTeamId,
																	int *outTeamId,
																	QString *errorMessage = nullptr);
		static bool saveSessionTeamId(const QString &sessionPath,
																	int teamId,
																	QString *errorMessage = nullptr);
		static bool readSessionPairAssignments(const QString &sessionPath,
																 QList<ArsSessionPairAssignment> *outAssignments,
																 QString *errorMessage = nullptr);
		static bool writeSessionPairAssignments(const QString &sessionPath,
																	const QList<ArsSessionPairAssignment> &assignments,
																	QString *errorMessage = nullptr);
		static bool upsertManualSessionAssignment(const QString &sessionPath,
																 const ArsSessionPairAssignment &manualAssignment,
																 QString *errorMessage = nullptr);
};

#endif // ARS_SESSION_INFO_JSON_H
