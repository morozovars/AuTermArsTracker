#include "ars_tracker_sessions_tab.h"

#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QStringList>
#include <algorithm>

#include "ars/workspace/ArsLocalWorkspace.h"

namespace
{
QString normalize_pair_serial(const QString &serial)
{
		if (serial.size() >= 8)
		{
				return serial;
		}
		return serial.rightJustified(8, '0');
}

bool parse_unix_timestamp_session_name(const QString &name, QDateTime *out)
{
		bool ok = false;
		const qint64 seconds = name.trimmed().toLongLong(&ok);
		if (!ok || seconds <= 0)
		{
				return false;
		}

		const QDateTime dt = QDateTime::fromSecsSinceEpoch(seconds);
		if (!dt.isValid())
		{
				return false;
		}

		if (out != nullptr)
		{
				*out = dt;
		}
		return true;
}
}

ArsTrackerSessionsTab::ArsTrackerSessionsTab(QWidget *parent) : QWidget(parent)
{
		buildUi();
		reloadSessions("init");
}

void ArsTrackerSessionsTab::buildUi()
{
		QGridLayout *root = new QGridLayout(this);
		root->setContentsMargins(8, 8, 8, 8);
		root->setHorizontalSpacing(8);
		root->setVerticalSpacing(8);

		pagesStack = new QStackedWidget(this);
		root->addWidget(pagesStack, 0, 0, 1, 1);

		buildListPage();
		buildDetailsPage();
}

void ArsTrackerSessionsTab::buildListPage()
{
		listPage = new QWidget(this);
		QGridLayout *layout = new QGridLayout(listPage);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setHorizontalSpacing(8);
		layout->setVerticalSpacing(8);

		QHBoxLayout *actions = new QHBoxLayout();
		openFolderButton = new QPushButton("Open folder", listPage);
		actions->addWidget(openFolderButton);
		reloadButton = new QPushButton("Reload", listPage);
		actions->addWidget(reloadButton);
		actions->addStretch(1);
		layout->addLayout(actions, 0, 0, 1, 1);

		sessionsTable = new QTableWidget(listPage);
		sessionsTable->setColumnCount(2);
		sessionsTable->setHorizontalHeaderLabels(QStringList() << "Session" << "Trackers");
		sessionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
		sessionsTable->setSelectionMode(QAbstractItemView::SingleSelection);
		sessionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
		sessionsTable->verticalHeader()->setVisible(false);
		sessionsTable->horizontalHeader()->setStretchLastSection(true);
		sessionsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		sessionsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		layout->addWidget(sessionsTable, 1, 0, 1, 1);

		statusLabel = new QLabel("No local sessions found", listPage);
		layout->addWidget(statusLabel, 2, 0, 1, 1);

		connect(openFolderButton, &QPushButton::clicked, this,
						&ArsTrackerSessionsTab::openSessionsFolder);
		connect(reloadButton, &QPushButton::clicked, this, [this]() { reloadSessions("reload"); });

		pagesStack->addWidget(listPage);
}

void ArsTrackerSessionsTab::buildDetailsPage()
{
		detailsPage = new QWidget(this);
		QGridLayout *layout = new QGridLayout(detailsPage);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setHorizontalSpacing(8);
		layout->setVerticalSpacing(8);

		QHBoxLayout *header = new QHBoxLayout();
		backButton = new QPushButton("Back", detailsPage);
		header->addWidget(backButton);
		sessionTitleLabel = new QLabel(detailsPage);
		header->addWidget(sessionTitleLabel, 1);
		layout->addLayout(header, 0, 0, 1, 1);

		sessionTrackersTable = new QTableWidget(detailsPage);
		sessionTrackersTable->setColumnCount(3);
		sessionTrackersTable->setHorizontalHeaderLabels(
				QStringList() << "Pair serial" << "Left tracker" << "Right tracker");
		sessionTrackersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
		sessionTrackersTable->setSelectionMode(QAbstractItemView::SingleSelection);
		sessionTrackersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
		sessionTrackersTable->verticalHeader()->setVisible(false);
		sessionTrackersTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		sessionTrackersTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		sessionTrackersTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
		layout->addWidget(sessionTrackersTable, 1, 0, 1, 1);

		sessionTrackersEmptyLabel = new QLabel("No trackers found", detailsPage);
		sessionTrackersEmptyLabel->setVisible(false);
		layout->addWidget(sessionTrackersEmptyLabel, 2, 0, 1, 1);

		connect(backButton, &QPushButton::clicked, this,
						&ArsTrackerSessionsTab::onBackFromSessionDetails);

		pagesStack->addWidget(detailsPage);
}

QString ArsTrackerSessionsTab::sessionsPath() const
{
		ArsLocalWorkspace workspace;
		if (!workspace.initialize())
		{
				return QString();
		}
		return workspace.sessionsPath();
}

void ArsTrackerSessionsTab::openSessionsFolder()
{
		const QString sessions_path = sessionsPath();
		if (sessions_path.trimmed().isEmpty())
		{
				statusLabel->setText("Workspace sessions path is unavailable");
				qWarning() << "Sessions tab: workspace sessions path is empty";
				return;
		}

		if (!QDir(sessions_path).exists() && !QDir().mkpath(sessions_path))
		{
				statusLabel->setText(QString("Failed to create sessions folder: %1").arg(sessions_path));
				qWarning() << "Sessions tab: failed to create sessions folder" << sessions_path;
				return;
		}

		const bool opened =
				QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::toNativeSeparators(sessions_path)));
		if (!opened)
		{
				statusLabel->setText(QString("Failed to open folder: %1").arg(sessions_path));
				qWarning() << "Sessions tab: failed to open folder in explorer" << sessions_path;
				return;
		}
		statusLabel->setText(QString("Opened: %1").arg(sessions_path));
}

QString ArsTrackerSessionsTab::formatSessionDisplayName(const QString &sessionFolderName) const
{
		QDateTime ts;
		if (!parse_unix_timestamp_session_name(sessionFolderName, &ts))
		{
				return sessionFolderName;
		}
		return QString("%1 - %2")
				.arg(sessionFolderName,
						 ts.toLocalTime().toString("dd MMM yyyy HH:mm"));
}

QList<SessionTrackerPair> ArsTrackerSessionsTab::scanSessionTrackers(const QString &sessionPath) const
{
		QList<SessionTrackerPair> out;
		const QDir sessionDir(sessionPath);
		if (!sessionDir.exists())
		{
				qWarning() << "Sessions tab: session path is not found" << sessionPath;
				return out;
		}

		const QFileInfoList entries =
				sessionDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
																 QDir::Name | QDir::IgnoreCase);
		QMap<QString, SessionTrackerPair> bySerial;
		const QRegularExpression trackerRx("^(.+)([LlRr])$");

		for (const QFileInfo &entry : entries)
		{
				const QString folderName = entry.fileName().trimmed();
				const QRegularExpressionMatch match = trackerRx.match(folderName);
				if (!match.hasMatch())
				{
						continue;
				}

				const QString rawSerial = match.captured(1);
				const QString side = match.captured(2).toUpper();
				if (rawSerial.isEmpty() || (side != "L" && side != "R"))
				{
						continue;
				}

				if (!bySerial.contains(rawSerial))
				{
						SessionTrackerPair pair;
						pair.pairSerial = normalize_pair_serial(rawSerial);
						pair.leftTracker = "-";
						pair.rightTracker = "-";
						bySerial.insert(rawSerial, pair);
				}

				SessionTrackerPair &pair = bySerial[rawSerial];
				if (side == "L")
				{
						pair.leftTracker = folderName;
				}
				else
				{
						pair.rightTracker = folderName;
				}
		}

		for (auto it = bySerial.cbegin(); it != bySerial.cend(); ++it)
		{
				out.append(it.value());
		}

		std::sort(out.begin(), out.end(), [](const SessionTrackerPair &a, const SessionTrackerPair &b) {
				const int cmp = QString::compare(a.pairSerial, b.pairSerial, Qt::CaseInsensitive);
				if (cmp != 0)
				{
						return cmp < 0;
				}
				return QString::compare(a.leftTracker + a.rightTracker, b.leftTracker + b.rightTracker,
																Qt::CaseInsensitive) < 0;
		});

		return out;
}

QString ArsTrackerSessionsTab::buildTrackersDisplayText(const QList<SessionTrackerPair> &pairs) const
{
		QStringList parts;
		for (const SessionTrackerPair &pair : pairs)
		{
				QString sides = "-";
				const bool hasL = pair.leftTracker != "-";
				const bool hasR = pair.rightTracker != "-";
				if (hasL && hasR)
				{
						sides = "L+R";
				}
				else if (hasL)
				{
						sides = "L";
				}
				else if (hasR)
				{
						sides = "R";
				}
				parts.append(QString("%1: %2").arg(pair.pairSerial, sides));
		}
		return parts.join(", ");
}

QList<LocalSessionInfo> ArsTrackerSessionsTab::scanLocalSessions(const QString &sessionsPath) const
{
		QList<LocalSessionInfo> out;
		const QDir sessionsDir(sessionsPath);
		if (!sessionsDir.exists())
		{
				return out;
		}

		const QFileInfoList sessionDirs =
				sessionsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
																	QDir::Name | QDir::IgnoreCase);
		for (const QFileInfo &sessionInfo : sessionDirs)
		{
				LocalSessionInfo local;
				local.folderName = sessionInfo.fileName();
				local.displayName = formatSessionDisplayName(local.folderName);
				local.absolutePath = sessionInfo.absoluteFilePath();
				const QList<SessionTrackerPair> pairs = scanSessionTrackers(local.absolutePath);
				local.trackersDisplayText = buildTrackersDisplayText(pairs);
				out.append(local);
		}

		std::sort(out.begin(), out.end(), [](const LocalSessionInfo &a, const LocalSessionInfo &b) {
				QDateTime aTs;
				QDateTime bTs;
				const bool aHasTs = parse_unix_timestamp_session_name(a.folderName, &aTs);
				const bool bHasTs = parse_unix_timestamp_session_name(b.folderName, &bTs);
				if (aHasTs != bHasTs)
				{
						return aHasTs && !bHasTs;
				}
				if (aHasTs && bHasTs)
				{
						return aTs > bTs;
				}
				return a.folderName.compare(b.folderName, Qt::CaseInsensitive) > 0;
		});

		return out;
}

void ArsTrackerSessionsTab::fillSessionsTable(const QList<LocalSessionInfo> &sessions)
{
		sessionsTable->setSortingEnabled(false);
		sessionsTable->clearContents();
		sessionsTable->setRowCount(sessions.size());

		for (int row = 0; row < sessions.size(); ++row)
		{
				const LocalSessionInfo &session = sessions.at(row);
				QPushButton *sessionLink = new QPushButton(session.displayName, sessionsTable);
				sessionLink->setFlat(true);
				sessionLink->setCursor(Qt::PointingHandCursor);
				sessionLink->setStyleSheet("QPushButton { text-align: left; color: #1e5aa8; border: none; }");
				sessionLink->setProperty("sessionId", session.folderName);
				sessionLink->setToolTip(session.absolutePath);
				connect(sessionLink, &QPushButton::clicked, this,
								&ArsTrackerSessionsTab::onSessionNameClicked);
				sessionsTable->setCellWidget(row, 0, sessionLink);

				sessionsTable->setItem(
						row, 1,
						new QTableWidgetItem(session.trackersDisplayText.isEmpty() ? "-" :
																												session.trackersDisplayText));
		}
}

void ArsTrackerSessionsTab::fillSessionTrackersTable(const QList<SessionTrackerPair> &pairs)
{
		sessionTrackersTable->setSortingEnabled(false);
		sessionTrackersTable->clearContents();
		sessionTrackersTable->setRowCount(pairs.size());

		for (int row = 0; row < pairs.size(); ++row)
		{
				const SessionTrackerPair &pair = pairs.at(row);
				sessionTrackersTable->setItem(row, 0, new QTableWidgetItem(pair.pairSerial));
				sessionTrackersTable->setItem(row, 1, new QTableWidgetItem(pair.leftTracker));
				sessionTrackersTable->setItem(row, 2, new QTableWidgetItem(pair.rightTracker));
		}

		sessionTrackersEmptyLabel->setVisible(pairs.isEmpty());
}

void ArsTrackerSessionsTab::reloadSessions(const QString &reason)
{
		const QString sessions_path = sessionsPath();
		if (sessions_path.trimmed().isEmpty())
		{
				sessionsTable->setRowCount(0);
				statusLabel->setText("Workspace sessions path is unavailable");
				qWarning() << "Sessions tab scan failed: workspace sessions path is empty";
				return;
		}

		if (!QDir(sessions_path).exists() && !QDir().mkpath(sessions_path))
		{
				sessionsTable->setRowCount(0);
				statusLabel->setText(QString("Failed to create sessions folder: %1").arg(sessions_path));
				qWarning() << "Sessions tab scan failed: cannot create path" << sessions_path;
				return;
		}

		const QList<LocalSessionInfo> sessions = scanLocalSessions(sessions_path);
		fillSessionsTable(sessions);

		if (sessions.isEmpty())
		{
				statusLabel->setText("No local sessions found");
		}
		else
		{
				statusLabel->setText(QString("Local sessions: %1").arg(sessions.size()));
		}

		qDebug() << "Sessions tab list loaded"
						 << "reason=" << reason
						 << "path=" << sessions_path
						 << "count=" << sessions.size();
}

void ArsTrackerSessionsTab::showSessionsListPage(bool forceReload)
{
		if (forceReload)
		{
				reloadSessions("back-to-list");
		}
		pagesStack->setCurrentWidget(listPage);
}

void ArsTrackerSessionsTab::showSessionDetailsPage(const QString &sessionId)
{
		const QString baseSessionsPath = sessionsPath();
		const QString sessionPath = QDir(baseSessionsPath).filePath(sessionId);
		if (baseSessionsPath.trimmed().isEmpty() || !QDir(sessionPath).exists())
		{
				qWarning() << "Sessions tab: session path is not found or unavailable" << sessionPath;
				return;
		}

		currentSessionId = sessionId;
		sessionTitleLabel->setText(formatSessionDisplayName(sessionId));

		const QList<SessionTrackerPair> pairs = scanSessionTrackers(sessionPath);
		fillSessionTrackersTable(pairs);

		qDebug() << "Sessions tab session page opened"
						 << "session=" << sessionId
						 << "path=" << sessionPath
						 << "pairs=" << pairs.size();

		pagesStack->setCurrentWidget(detailsPage);
}

void ArsTrackerSessionsTab::onSessionNameClicked()
{
		QPushButton *btn = qobject_cast<QPushButton *>(sender());
		if (btn == nullptr)
		{
				return;
		}
		const QString sessionId = btn->property("sessionId").toString();
		if (sessionId.trimmed().isEmpty())
		{
				return;
		}
		showSessionDetailsPage(sessionId);
}

void ArsTrackerSessionsTab::onBackFromSessionDetails()
{
		showSessionsListPage(true);
}
