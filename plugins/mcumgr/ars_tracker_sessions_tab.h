#ifndef ARS_TRACKER_SESSIONS_TAB_H
#define ARS_TRACKER_SESSIONS_TAB_H

#include <QWidget>
#include <QString>
#include <QList>
#include <QTime>
#include <QStringList>

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
		bool validateSessionInfo(const ArsSessionInfo &info, QStringList *problems) const;
		bool saveSessionInfoJson(const QString &sessionPath, const ArsSessionInfo &info, QString *errorMessage) const;
		void startSessionProcessingFlow();
		void processNextSessionPair();
		void finishSessionProcessingFlow();

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

		QDialog *m_processDialog = nullptr;
		QLabel *m_processStatusLabel = nullptr;
		QProgressBar *m_processProgressBar = nullptr;
		QPlainTextEdit *m_processResultText = nullptr;
		QPushButton *m_processCloseButton = nullptr;
		QList<ArsSessionPairInput> m_processPairInputs;
		int m_processPairIndex = 0;
		QStringList m_processProblems;
};

#endif // ARS_TRACKER_SESSIONS_TAB_H
