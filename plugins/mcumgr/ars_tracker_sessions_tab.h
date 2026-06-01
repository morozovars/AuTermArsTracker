#ifndef ARS_TRACKER_SESSIONS_TAB_H
#define ARS_TRACKER_SESSIONS_TAB_H

#include <QWidget>
#include <QString>
#include <QList>
#include <QTime>
#include <QStringList>
#include <QHash>

#include "ars_tracker/ars_session_info.h"
#include "ars_tracker/ars_session_duration_scanner.h"
#include "ars_tracker/ars_session_processing_loader.h"
#include "ars_tracker/ars_session_postprocessor.h"
#include "ars/workspace/ArsTeamRepository.h"
#include "ars/workspace/ArsTargetsByPosition.h"
#include "ars/workspace/ArsPlayer.h"
#include "ars/workspace/ArsSessionPlayerBindingResolver.h"

class QPushButton;
class QTableWidget;
class QLabel;
class QStackedWidget;
class QTableWidgetItem;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QTimeEdit;
class QLineEdit;
class QPlainTextEdit;
class QDialog;
class QProgressBar;
class QGroupBox;
class QResizeEvent;
class ArsSessionAssignmentDialog;

struct SessionTrackerPair
{
		QString pairSerial;
		QString leftTracker;
		QString rightTracker;
};

struct LocalSessionInfo
{
		QString folderName;
		QString displayName;
		QString absolutePath;
		QString trackersDisplayText;
		bool hasExplicitTeamId = false;
		int explicitTeamId = -1;
		int effectiveTeamId = -1;
		QString teamDisplayText;
		QString effectiveTeamDisplayText;
};

struct SessionPairPlayerRow
{
		QString pairId;
		QString leftTrackerSerial;
		QString rightTrackerSerial;
		QString playerId;
		QString playerName;
		QString playerPhotoPath;
		QString assignmentSource;
		bool isOverride = false;
		bool hasPlayer = false;
		bool playerMissing = false;
};

class ArsTrackerSessionsTab : public QWidget
{
		Q_OBJECT

public:
		explicit ArsTrackerSessionsTab(QWidget *parent = nullptr);

public slots:
		void reloadSessions(const QString &reason = QString());

private slots:
		void openSessionsFolder();
		void onSessionNameClicked();
		void onSessionTableCellClicked(int row, int column);
		void onBackFromSessionDetails();
		void onRescanSessionClicked();
		void onProcessSessionClicked();
		void onTeamFilterChanged();
		void onSaveSessionTeamClicked();
		void onDeleteSessionClicked();
		void onSessionAssignmentClicked();
		void onGeneratePdfClicked();
		void onOpenReportClicked();

private:
		void resizeEvent(QResizeEvent *event) override;
		void buildUi();
		void buildListPage();
		void buildDetailsPage();
		QString sessionsPath() const;
		QList<LocalSessionInfo> scanLocalSessions(const QString &sessionsPath) const;
		QList<SessionTrackerPair> scanSessionTrackers(const QString &sessionPath) const;
		QString formatSessionDisplayName(const QString &sessionFolderName) const;
		QString buildTrackersDisplayText(const QList<SessionTrackerPair> &pairs) const;
		QString teamDisplayName(const ArsTeam &team) const;
		void applyTeamContextToSession(LocalSessionInfo *session) const;
		void rebuildTeamFilterCombo(bool resetToDefaultSelection);
		void applySessionsFilterAndRefreshTable();
		void refreshSessionDetailsTeamUi();
		bool validateSessionDeletePath(const QString &sessionName,
																 const QString &sessionPath,
																 QString *errorMessage) const;
		bool deleteSessionDirectory(const QString &sessionName,
																const QString &sessionPath,
																QString *errorMessage) const;
		void showSessionsListPage(bool forceReload = false);
		void showSessionDetailsPage(const QString &sessionId);
		void fillSessionsTable(const QList<LocalSessionInfo> &sessions);
		void fillSessionAssignmentsTable();
		QList<ArsSessionTrackerPair> detectedSessionPairs(const QString &sessionPath) const;
		bool resolveAndPersistSessionAssignments(const QString &sessionPath,
																						 int sessionTeamId,
																						 const QList<ArsSessionTrackerPair> &pairs);
		int effectiveSessionTeamIdForPath(const QString &sessionPath) const;
		void rebuildCurrentPairRows();
		QString buildTrackersDisplayTextForSession(const QString &sessionPath,
																							 const QList<SessionTrackerPair> &pairs,
																							 const QList<ArsPlayer> &players) const;
		ArsSessionTargetSettings readTargetSettingsFromUi() const;
		ArsSessionInfo readSessionInfoFromUi() const;
		void resetSessionInformationFieldsToDefaults();
		void applySessionInfoToUi(const ArsSessionInfo &info);
		bool loadSessionInfoJsonIntoUi(const QString &sessionPath,
																 ArsSessionInfo *loadedInfo,
																 bool *fileExists,
																 bool *hasPlannedSessionPeriod,
																 QStringList *warnings = nullptr);
		bool validateSessionInfo(const ArsSessionInfo &info, QStringList *problems) const;
		bool saveSessionInfoJson(const QString &sessionPath, const ArsSessionInfo &info, QString *errorMessage) const;
		void updateSessionTimeSummaryFromMaxTimestamp(uint32_t maxTimestamp100ms, bool hasTimestamp);
		void setSessionTimeSummaryPlaceholder();
		void setSessionTimeSummary(const QString &startTime, const QString &finishTime, const QString &duration);
		QTime roundUpToNextHalfHour(const QTime &time) const;
		QTime roundDownToPreviousHalfHour(const QTime &time) const;
		void startSessionDurationScan(bool isInitial, bool saveJsonAfterScan, bool shouldRecommendPlannedPeriod, bool forceRecommendation);
		void processNextDurationScanFile();
		void finishSessionDurationScan();
		void startSessionProcessingFlow();
		void processNextSessionPair();
		void finishSessionProcessingFlow();
		QTime sessionStartTimeFromSessionName(const QString &sessionName, bool *timestampValid = nullptr) const;
		QString formatDurationMs(qint64 durationMs) const;
		void updateSessionTimeSummary();
		uint32_t maxIntegralTimestamp(const std::vector<IntegralState> &states) const;
		void scheduleSessionsListColumnResize();
		void resizeSessionsListColumnsToContent();
		LocalSessionInfo *findSessionById(const QString &sessionId);
		const LocalSessionInfo *findSessionById(const QString &sessionId) const;
		bool saveSessionTeamId(const QString &sessionPath, int teamId, QString *errorMessage) const;
		QString reportPdfPathForSession(const QString &sessionPath) const;
		void updateOpenReportButtonState();
		void onTargetPositionChanged(int index);
		void onTargetValueEdited();
		void persistCurrentTargetPositionEdits();
		ArsPlannedMetrics effectiveTargetsForPosition(const QString &positionKey) const;
		void applyTargetsToUi(const ArsPlannedMetrics &targets);
		ArsPlannedMetrics targetsFromUi() const;
		void refreshTargetsForSelectedPosition();

		QStackedWidget *pagesStack = nullptr;
		QWidget *listPage = nullptr;
		QWidget *detailsPage = nullptr;
		QPushButton *openFolderButton = nullptr;
		QPushButton *reloadButton = nullptr;
		QComboBox *teamFilterCombo = nullptr;
		QPushButton *backButton = nullptr;
		QPushButton *rescanButton = nullptr;
		QPushButton *processButton = nullptr;
		QPushButton *generatePdfButton = nullptr;
		QPushButton *openReportButton = nullptr;
		QPushButton *saveSessionTeamButton = nullptr;
		QTableWidget *sessionsTable = nullptr;
		QLabel *sessionTitleLabel = nullptr;
		QLabel *detailsStatusLabel = nullptr;
		QLabel *sessionTimeSummaryLabel = nullptr;
		QLabel *sessionDurationSummaryLabel = nullptr;
		QLabel *sessionTeamStatusLabel = nullptr;
		QComboBox *sessionTeamCombo = nullptr;
		QDoubleSpinBox *spinTargetDistanceKm = nullptr;
		QComboBox *comboTargetPosition = nullptr;
		QSpinBox *spinTargetAccelerationDistanceM = nullptr;
		QSpinBox *spinTargetFootload10_3g = nullptr;
		QSpinBox *spinTargetTouchesCount = nullptr;
		QDoubleSpinBox *spinTargetFootloadPerMin = nullptr;
		QDoubleSpinBox *spinTargetMaxSpeedMps = nullptr;
		QSpinBox *spinTargetShotsCount = nullptr;
		QSpinBox *spinTargetPossessions = nullptr;
		QComboBox *comboSessionType = nullptr;
		QTimeEdit *timeSessionStart = nullptr;
		QTimeEdit *timeSessionFinish = nullptr;
		QLineEdit *editSessionLocation = nullptr;
		QPlainTextEdit *editSessionGoals = nullptr;
		QTableWidget *sessionAssignmentsTable = nullptr;
		QLabel *sessionAssignmentsEmptyLabel = nullptr;
		QLabel *statusLabel = nullptr;
		QString currentSessionId;
		QList<LocalSessionInfo> m_allSessions;
		QList<LocalSessionInfo> m_filteredSessions;
		QList<ArsTeam> m_teams;
		QHash<int, ArsTeam> m_teamsById;
		int m_defaultTeamId = -1;
		bool m_teamFilterInitialized = false;
		int m_selectedTeamFilterData = -999;
		QTime m_currentSessionStartTime;
		bool m_currentSessionStartTimestampValid = false;
		bool m_currentSessionFinishKnown = false;
		QTime m_currentSessionFinishTime;
		qint64 m_currentSessionDurationMs = -1;

		QDialog *m_processDialog = nullptr;
		QLabel *m_processStatusLabel = nullptr;
		QProgressBar *m_processProgressBar = nullptr;
		QPlainTextEdit *m_processResultText = nullptr;
		QPushButton *m_processCloseButton = nullptr;
		QList<ArsSessionPairInput> m_processPairInputs;
		int m_processPairIndex = 0;
		QStringList m_processProblems;
		uint32_t m_processMaxIntegralTimestamp = 0;
		bool m_processHasIntegralTimestamp = false;
		bool m_processValidationOk = true;
		ArsSessionInfo m_pendingProcessSessionInfo;
		ArsSessionPostprocessRequest m_postprocessRequest;
		QStringList m_processSuccesses;

		QDialog *m_scanDialog = nullptr;
		QLabel *m_scanStatusLabel = nullptr;
		QProgressBar *m_scanProgressBar = nullptr;
		QPlainTextEdit *m_scanResultText = nullptr;
		QPushButton *m_scanCloseButton = nullptr;
		QList<ArsDurationScanFileInput> m_scanInputs;
		int m_scanIndex = 0;
		uint32_t m_scanMaxTimestamp100ms = 0;
		bool m_scanHasTimestamp = false;
		QStringList m_scanProblems;
		bool m_scanShouldSaveJson = false;
		bool m_scanIsInitial = false;
		bool m_scanShouldRecommendPlannedPeriod = false;
		bool m_scanForceRecommendation = false;
		QList<ArsSessionPairAssignment> m_currentAssignments;
		QList<ArsSessionTrackerPair> m_currentDetectedPairs;
		QList<SessionPairPlayerRow> m_currentPairRows;
		ArsTargetsByPosition m_sessionTargetOverrides;
		QString m_currentTargetPositionKey = "central_midfielder";
		bool m_targetValuesDirty = false;
		bool m_targetUiApplying = false;
};

#endif // ARS_TRACKER_SESSIONS_TAB_H
