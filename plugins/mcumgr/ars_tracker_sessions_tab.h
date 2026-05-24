#ifndef ARS_TRACKER_SESSIONS_TAB_H
#define ARS_TRACKER_SESSIONS_TAB_H

#include <QWidget>
#include <QString>
#include <QList>
#include <QTime>
#include <QStringList>

#include "ars_tracker/ars_session_info.h"
#include "ars_tracker/ars_session_processing_loader.h"

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
		void onBackFromSessionDetails();
		void onProcessSessionClicked();

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
		void showSessionsListPage(bool forceReload = false);
		void showSessionDetailsPage(const QString &sessionId);
		void fillSessionsTable(const QList<LocalSessionInfo> &sessions);
		void fillSessionTrackersTable(const QList<SessionTrackerPair> &pairs);
		ArsSessionTargetSettings readTargetSettingsFromUi() const;
		ArsSessionInfo readSessionInfoFromUi() const;
		void resetSessionInformationFieldsToDefaults();
		void applySessionInfoToUi(const ArsSessionInfo &info);
		bool loadSessionInfoJsonIntoUi(const QString &sessionPath,
																 ArsSessionInfo *loadedInfo,
																 bool *fileExists,
																 bool *hasPlannedSessionPeriod,
																 QStringList *warnings = nullptr);
		bool computeRecommendedPlannedSessionPeriod(const QString &sessionPath,
																								const QString &sessionId,
																								const ArsSessionInfo *loadedInfo,
																								QTime *outRecommendedStart,
																								QTime *outRecommendedFinish,
																								QString *sourceTag,
																								QString *errorReason) const;
		bool validateSessionInfo(const ArsSessionInfo &info, QStringList *problems) const;
		bool saveSessionInfoJson(const QString &sessionPath, const ArsSessionInfo &info, QString *errorMessage) const;
		void updateSessionTimeSummaryFromMaxTimestamp(uint32_t maxTimestamp100ms, bool hasTimestamp);
		void setSessionTimeSummaryPlaceholder();
		void setSessionTimeSummary(const QString &startTime, const QString &finishTime, const QString &duration);
		QTime roundUpToNextHalfHour(const QTime &time) const;
		QTime roundDownToPreviousHalfHour(const QTime &time) const;
		void startSessionProcessingFlow();
		void processNextSessionPair();
		void finishSessionProcessingFlow();
		QTime sessionStartTimeFromSessionName(const QString &sessionName, bool *timestampValid = nullptr) const;
		QString formatDurationMs(qint64 durationMs) const;
		void updateSessionTimeSummary();
		uint32_t maxIntegralTimestamp(const std::vector<IntegralState> &states) const;
		void scheduleSessionsListColumnResize();
		void resizeSessionsListColumnsToContent();

		QStackedWidget *pagesStack = nullptr;
		QWidget *listPage = nullptr;
		QWidget *detailsPage = nullptr;
		QPushButton *openFolderButton = nullptr;
		QPushButton *reloadButton = nullptr;
		QPushButton *backButton = nullptr;
		QPushButton *processButton = nullptr;
		QTableWidget *sessionsTable = nullptr;
		QLabel *sessionTitleLabel = nullptr;
		QLabel *detailsStatusLabel = nullptr;
		QLabel *sessionTimeSummaryLabel = nullptr;
		QLabel *sessionDurationSummaryLabel = nullptr;
		QDoubleSpinBox *spinTargetDistanceKm = nullptr;
		QSpinBox *spinTargetAccelerationDistanceM = nullptr;
		QSpinBox *spinTargetFootload10_3g = nullptr;
		QSpinBox *spinTargetTouchesCount = nullptr;
		QDoubleSpinBox *spinTargetFootloadPerMin = nullptr;
		QComboBox *comboSessionType = nullptr;
		QTimeEdit *timeSessionStart = nullptr;
		QTimeEdit *timeSessionFinish = nullptr;
		QLineEdit *editSessionLocation = nullptr;
		QPlainTextEdit *editSessionGoals = nullptr;
		QTableWidget *sessionTrackersTable = nullptr;
		QLabel *sessionTrackersEmptyLabel = nullptr;
		QLabel *statusLabel = nullptr;
		QString currentSessionId;
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
};

#endif // ARS_TRACKER_SESSIONS_TAB_H
