#ifndef ARS_TRACKER_SESSIONS_TAB_H
#define ARS_TRACKER_SESSIONS_TAB_H

#include <QWidget>
#include <QString>
#include <QList>

class QPushButton;
class QTableWidget;
class QLabel;
class QStackedWidget;
class QTableWidgetItem;
class QSpinBox;
class QDoubleSpinBox;

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
		QTableWidget *sessionTrackersTable = nullptr;
		QLabel *sessionTrackersEmptyLabel = nullptr;
		QLabel *statusLabel = nullptr;
		QString currentSessionId;
};

#endif // ARS_TRACKER_SESSIONS_TAB_H
