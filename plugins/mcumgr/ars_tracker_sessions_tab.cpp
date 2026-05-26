#include "ars_tracker_sessions_tab.h"

#include <QApplication>
#include <QChar>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimeEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

#include "ars/workspace/ArsAppSettings.h"
#include "ars/workspace/ArsTeamRepository.h"
#include "ars/workspace/ArsLocalWorkspace.h"
#include "ars_tracker/ars_session_duration_scanner.h"
#include "ars_tracker/ars_session_info_json.h"

namespace
{
constexpr int kMaxMalformedLinesToLog = 10;
const QTime kDefaultPlannedStart(10, 0, 0);
const QTime kDefaultPlannedFinish(11, 30, 0);

QString normalize_pair_serial(const QString &serial)
{
		return serial.size() >= 8 ? serial : serial.rightJustified(8, '0');
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

qint64 session_sort_key_seconds(const QFileInfo &sessionInfo)
{
		QDateTime ts;
		if (parse_unix_timestamp_session_name(sessionInfo.fileName(), &ts))
		{
				return ts.toSecsSinceEpoch();
		}
		return sessionInfo.lastModified().toSecsSinceEpoch();
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

		QHBoxLayout *actions = new QHBoxLayout();
		openFolderButton = new QPushButton("Open folder", listPage);
		reloadButton = new QPushButton("Reload", listPage);
		QLabel *teamFilterLabel = new QLabel("Team filter:", listPage);
		teamFilterCombo = new QComboBox(listPage);
		actions->addWidget(openFolderButton);
		actions->addWidget(reloadButton);
		actions->addSpacing(12);
		actions->addWidget(teamFilterLabel);
		actions->addWidget(teamFilterCombo);
		actions->addStretch(1);
		layout->addLayout(actions, 0, 0, 1, 1);

		sessionsTable = new QTableWidget(listPage);
		sessionsTable->setColumnCount(3);
		sessionsTable->setHorizontalHeaderLabels(QStringList() << "Session" << "Team" << "Trackers");
		sessionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
		sessionsTable->setSelectionMode(QAbstractItemView::SingleSelection);
		sessionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
		sessionsTable->verticalHeader()->setVisible(false);
		sessionsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
		sessionsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
		sessionsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
		sessionsTable->setColumnWidth(1, 220);
		layout->addWidget(sessionsTable, 1, 0, 1, 1);

		statusLabel = new QLabel("No local sessions found", listPage);
		layout->addWidget(statusLabel, 2, 0, 1, 1);

		connect(openFolderButton, &QPushButton::clicked, this, &ArsTrackerSessionsTab::openSessionsFolder);
		connect(reloadButton, &QPushButton::clicked, this, [this]() { reloadSessions("reload"); });
		connect(teamFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ArsTrackerSessionsTab::onTeamFilterChanged);

		pagesStack->addWidget(listPage);
		scheduleSessionsListColumnResize();
}

void ArsTrackerSessionsTab::buildDetailsPage()
{
		detailsPage = new QWidget(this);
		QGridLayout *layout = new QGridLayout(detailsPage);
		layout->setContentsMargins(0, 0, 0, 0);

		QHBoxLayout *header = new QHBoxLayout();
		backButton = new QPushButton("Back", detailsPage);
		rescanButton = new QPushButton("Rescan", detailsPage);
		rescanButton->setObjectName("button_session_rescan");
		processButton = new QPushButton("Process", detailsPage);
		sessionTitleLabel = new QLabel(detailsPage);
		header->addWidget(backButton);
		header->addWidget(rescanButton);
		header->addWidget(processButton);
		header->addWidget(sessionTitleLabel, 1);
		layout->addLayout(header, 0, 0, 1, 1);

		QGroupBox *sessionInfoBox = new QGroupBox("Session Information", detailsPage);
		QVBoxLayout *sessionInfoRoot = new QVBoxLayout(sessionInfoBox);
		sessionTimeSummaryLabel = new QLabel(sessionInfoBox);
		sessionDurationSummaryLabel = new QLabel(sessionInfoBox);
		sessionInfoRoot->addWidget(sessionTimeSummaryLabel);
		sessionInfoRoot->addWidget(sessionDurationSummaryLabel);

		QHBoxLayout *sessionInfoColumns = new QHBoxLayout();
		sessionInfoRoot->addLayout(sessionInfoColumns);

		QGroupBox *parametersBox = new QGroupBox("Parameters", sessionInfoBox);
		QFormLayout *parametersLayout = new QFormLayout(parametersBox);
		sessionTeamCombo = new QComboBox(parametersBox);
		saveSessionTeamButton = new QPushButton("Save team", parametersBox);
		QHBoxLayout *teamRow = new QHBoxLayout();
		teamRow->addWidget(sessionTeamCombo, 1);
		teamRow->addWidget(saveSessionTeamButton);
		parametersLayout->addRow("Team", teamRow);
		sessionTeamStatusLabel = new QLabel(parametersBox);
		parametersLayout->addRow("", sessionTeamStatusLabel);

		comboSessionType = new QComboBox(parametersBox);
		comboSessionType->setObjectName("combo_session_type");
		comboSessionType->addItem("Training session", "training");
		comboSessionType->addItem("Match", "match");
		parametersLayout->addRow("Type", comboSessionType);

		QHBoxLayout *periodLayout = new QHBoxLayout();
		timeSessionStart = new QTimeEdit(parametersBox);
		timeSessionStart->setObjectName("time_session_start");
		timeSessionStart->setDisplayFormat("HH:mm");
		timeSessionStart->setTime(kDefaultPlannedStart);
		timeSessionFinish = new QTimeEdit(parametersBox);
		timeSessionFinish->setObjectName("time_session_finish");
		timeSessionFinish->setDisplayFormat("HH:mm");
		timeSessionFinish->setTime(kDefaultPlannedFinish);
		periodLayout->addWidget(timeSessionStart);
		periodLayout->addWidget(new QLabel("-", parametersBox));
		periodLayout->addWidget(timeSessionFinish);
		parametersLayout->addRow("Planned session period", periodLayout);

		editSessionLocation = new QLineEdit(parametersBox);
		editSessionLocation->setObjectName("edit_session_location");
		parametersLayout->addRow("Location", editSessionLocation);

		editSessionGoals = new QPlainTextEdit(parametersBox);
		editSessionGoals->setObjectName("edit_session_goals");
		editSessionGoals->setFixedHeight(110);
		parametersLayout->addRow("Goals", editSessionGoals);
		sessionInfoColumns->addWidget(parametersBox, 1);

		QGroupBox *targetBox = new QGroupBox("Target", sessionInfoBox);
		QFormLayout *targetLayout = new QFormLayout(targetBox);
		spinTargetDistanceKm = new QDoubleSpinBox(targetBox);
		spinTargetDistanceKm->setObjectName("spin_session_target_distance_km");
		spinTargetDistanceKm->setDecimals(3);
		spinTargetDistanceKm->setRange(0.0, 1000.0);
		targetLayout->addRow("Target distance, km", spinTargetDistanceKm);

		spinTargetAccelerationDistanceM = new QSpinBox(targetBox);
		spinTargetAccelerationDistanceM->setObjectName("spin_session_target_acceleration_distance_m");
		spinTargetAccelerationDistanceM->setRange(0, 100000);
		targetLayout->addRow("Target acceleration distance, m", spinTargetAccelerationDistanceM);

		spinTargetFootload10_3g = new QSpinBox(targetBox);
		spinTargetFootload10_3g->setObjectName("spin_session_target_footload_10_3g");
		spinTargetFootload10_3g->setRange(0, 1000000);
		targetLayout->addRow("Target footload, 10^3g", spinTargetFootload10_3g);

		spinTargetTouchesCount = new QSpinBox(targetBox);
		spinTargetTouchesCount->setObjectName("spin_session_target_touches_count");
		spinTargetTouchesCount->setRange(0, 100000);
		targetLayout->addRow("Target touches count", spinTargetTouchesCount);

		spinTargetFootloadPerMin = new QDoubleSpinBox(targetBox);
		spinTargetFootloadPerMin->setObjectName("spin_session_target_footload_per_min");
		spinTargetFootloadPerMin->setDecimals(2);
		spinTargetFootloadPerMin->setRange(0.0, 100000.0);
		targetLayout->addRow("Target footload intensity footload/min", spinTargetFootloadPerMin);
		sessionInfoColumns->addWidget(targetBox, 1);
		layout->addWidget(sessionInfoBox, 1, 0, 1, 1);

		sessionTrackersTable = new QTableWidget(detailsPage);
		sessionTrackersTable->setColumnCount(3);
		sessionTrackersTable->setHorizontalHeaderLabels(QStringList() << "Pair serial" << "Left tracker" << "Right tracker");
		sessionTrackersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
		sessionTrackersTable->setSelectionMode(QAbstractItemView::SingleSelection);
		sessionTrackersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
		sessionTrackersTable->verticalHeader()->setVisible(false);
		sessionTrackersTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		sessionTrackersTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		sessionTrackersTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
		layout->addWidget(sessionTrackersTable, 2, 0, 1, 1);

		sessionTrackersEmptyLabel = new QLabel("No trackers found", detailsPage);
		sessionTrackersEmptyLabel->setVisible(false);
		layout->addWidget(sessionTrackersEmptyLabel, 3, 0, 1, 1);

		detailsStatusLabel = new QLabel(detailsPage);
		layout->addWidget(detailsStatusLabel, 4, 0, 1, 1);

		connect(backButton, &QPushButton::clicked, this, &ArsTrackerSessionsTab::onBackFromSessionDetails);
		connect(rescanButton, &QPushButton::clicked, this, &ArsTrackerSessionsTab::onRescanSessionClicked);
		connect(processButton, &QPushButton::clicked, this, &ArsTrackerSessionsTab::onProcessSessionClicked);
		connect(saveSessionTeamButton, &QPushButton::clicked, this, &ArsTrackerSessionsTab::onSaveSessionTeamClicked);
		connect(sessionTeamCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
				if (saveSessionTeamButton == nullptr || sessionTeamCombo == nullptr)
				{
						return;
				}
				const int selectedTeamId = sessionTeamCombo->currentData().toInt();
				saveSessionTeamButton->setEnabled(selectedTeamId > 0 && m_teamsById.contains(selectedTeamId));
		});

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
		const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::toNativeSeparators(sessions_path)));
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
		return QString("%1 - %2").arg(sessionFolderName, ts.toLocalTime().toString("dd MMM yyyy HH:mm"));
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
		const QFileInfoList entries = sessionDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
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
				return QString::compare(a.pairSerial, b.pairSerial, Qt::CaseInsensitive) < 0;
		});
		return out;
}

QString ArsTrackerSessionsTab::buildTrackersDisplayText(const QList<SessionTrackerPair> &pairs) const
{
		QStringList parts;
		for (const SessionTrackerPair &pair : pairs)
		{
				const bool hasL = pair.leftTracker != "-";
				const bool hasR = pair.rightTracker != "-";
				const QString sides = hasL && hasR ? "L+R" : (hasL ? "L" : (hasR ? "R" : "-"));
				parts.append(QString("%1: %2").arg(pair.pairSerial, sides));
		}
		return parts.join(", ");
}

QString ArsTrackerSessionsTab::teamDisplayName(const ArsTeam &team) const
{
		if (!team.ageCategory.trimmed().isEmpty())
		{
				return QString("%1 (%2)").arg(team.name, team.ageCategory);
		}
		return team.name;
}

void ArsTrackerSessionsTab::applyTeamContextToSession(LocalSessionInfo *session) const
{
		if (session == nullptr)
		{
				return;
		}
		session->effectiveTeamId = -1;
		session->effectiveTeamDisplayText.clear();
		if (session->hasExplicitTeamId)
		{
				if (m_teamsById.contains(session->explicitTeamId))
				{
						const ArsTeam team = m_teamsById.value(session->explicitTeamId);
						session->teamDisplayText = teamDisplayName(team);
						session->effectiveTeamId = session->explicitTeamId;
						session->effectiveTeamDisplayText = session->teamDisplayText;
				}
				else
				{
						session->teamDisplayText = QString("Missing team id %1").arg(session->explicitTeamId);
				}
				return;
		}
		session->teamDisplayText = "not configured";
		if (m_defaultTeamId > 0 && m_teamsById.contains(m_defaultTeamId))
		{
				session->effectiveTeamId = m_defaultTeamId;
				session->effectiveTeamDisplayText = teamDisplayName(m_teamsById.value(m_defaultTeamId));
		}
}

QList<LocalSessionInfo> ArsTrackerSessionsTab::scanLocalSessions(const QString &sessionsPath) const
{
		QList<LocalSessionInfo> out;
		const QDir sessionsDir(sessionsPath);
		if (!sessionsDir.exists())
		{
				return out;
		}
		const QFileInfoList sessionDirs = sessionsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
		QFileInfo newestInfo;
		QFileInfo oldestInfo;
		bool hasAny = false;
		for (const QFileInfo &sessionInfo : sessionDirs)
		{
				LocalSessionInfo local;
				local.folderName = sessionInfo.fileName();
				local.displayName = formatSessionDisplayName(local.folderName);
				local.absolutePath = sessionInfo.absoluteFilePath();
				local.trackersDisplayText = buildTrackersDisplayText(scanSessionTrackers(local.absolutePath));
				QString teamReadError;
				int teamId = -1;
				bool hasTeamId = false;
				if (!ArsSessionInfoJson::loadSessionTeamId(local.absolutePath, &hasTeamId, &teamId, &teamReadError))
				{
						qWarning() << "SessionInfo team read failed session=" << local.folderName << "error=" << teamReadError;
				}
				local.hasExplicitTeamId = hasTeamId;
				local.explicitTeamId = hasTeamId ? teamId : -1;
				qDebug() << "Sessions session team read session=" << local.folderName
								 << "hasTeamId=" << local.hasExplicitTeamId
								 << "teamId=" << local.explicitTeamId;
				out.append(local);
				if (!hasAny || session_sort_key_seconds(sessionInfo) > session_sort_key_seconds(newestInfo))
				{
						newestInfo = sessionInfo;
				}
				if (!hasAny || session_sort_key_seconds(sessionInfo) < session_sort_key_seconds(oldestInfo))
				{
						oldestInfo = sessionInfo;
				}
				hasAny = true;
		}
		std::sort(out.begin(), out.end(), [](const LocalSessionInfo &a, const LocalSessionInfo &b) {
				const QFileInfo aInfo(a.absolutePath);
				const QFileInfo bInfo(b.absolutePath);
				const qint64 aKey = session_sort_key_seconds(aInfo);
				const qint64 bKey = session_sort_key_seconds(bInfo);
				if (aKey != bKey)
				{
						return aKey > bKey;
				}
				return a.folderName.compare(b.folderName, Qt::CaseInsensitive) > 0;
		});
		qDebug() << "Sessions tab list sorted"
						 << "count=" << out.size()
						 << "newest=" << (hasAny ? newestInfo.fileName() : QString())
						 << "oldest=" << (hasAny ? oldestInfo.fileName() : QString());
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
				connect(sessionLink, &QPushButton::clicked, this, &ArsTrackerSessionsTab::onSessionNameClicked);
				sessionsTable->setCellWidget(row, 0, sessionLink);
				QTableWidgetItem *teamItem = new QTableWidgetItem(session.teamDisplayText);
				if (session.hasExplicitTeamId)
				{
						if (session.effectiveTeamId > 0)
						{
								teamItem->setToolTip(QString("TeamId: %1").arg(session.explicitTeamId));
						}
						else
						{
								teamItem->setToolTip(QString("TeamId: %1 is configured in SessionInfo.json but team was not found").arg(session.explicitTeamId));
						}
				}
				else
				{
						teamItem->setToolTip("TeamId is not configured in SessionInfo.json");
				}
				sessionsTable->setItem(row, 1, teamItem);
				sessionsTable->setItem(row, 2, new QTableWidgetItem(session.trackersDisplayText.isEmpty() ? "-" : session.trackersDisplayText));
				qDebug() << "Sessions session team display session=" << session.folderName << "display=" << session.teamDisplayText;
		}
}

void ArsTrackerSessionsTab::fillSessionTrackersTable(const QList<SessionTrackerPair> &pairs)
{
		sessionTrackersTable->setSortingEnabled(false);
		sessionTrackersTable->clearContents();
		sessionTrackersTable->setRowCount(pairs.size());
		for (int row = 0; row < pairs.size(); ++row)
		{
				sessionTrackersTable->setItem(row, 0, new QTableWidgetItem(pairs.at(row).pairSerial));
				sessionTrackersTable->setItem(row, 1, new QTableWidgetItem(pairs.at(row).leftTracker));
				sessionTrackersTable->setItem(row, 2, new QTableWidgetItem(pairs.at(row).rightTracker));
		}
		sessionTrackersEmptyLabel->setVisible(pairs.isEmpty());
}

void ArsTrackerSessionsTab::reloadSessions(const QString &reason)
{
		const QString sessions_path = sessionsPath();
		ArsLocalWorkspace workspace;
		const QString workspaceRootPath = workspace.initialize() ? workspace.rootPath() : QString();
		qDebug() << "Sessions team filter reload begin workspace=" << workspaceRootPath;
		m_teams.clear();
		m_teamsById.clear();
		m_defaultTeamId = -1;
		if (!workspaceRootPath.trimmed().isEmpty())
		{
				ArsTeamRepository teamRepository(workspaceRootPath);
				QStringList teamWarnings;
				m_teams = teamRepository.loadTeams(&teamWarnings);
				for (const QString &warning : teamWarnings)
				{
						qWarning().noquote() << warning;
				}
				for (const ArsTeam &team : m_teams)
				{
						m_teamsById.insert(team.teamId, team);
				}
				qDebug() << "Sessions team repository loaded count=" << m_teams.size();
				QString settingsError;
				if (!ArsAppSettings(workspaceRootPath).loadDefaultTeamId(&m_defaultTeamId, &settingsError))
				{
						qWarning() << "Ars Team settings load failed error=" << settingsError;
						m_defaultTeamId = -1;
				}
				qDebug() << "Sessions default team id=" << m_defaultTeamId;
		}

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
		m_allSessions = scanLocalSessions(sessions_path);
		for (LocalSessionInfo &session : m_allSessions)
		{
				applyTeamContextToSession(&session);
		}
		rebuildTeamFilterCombo(!m_teamFilterInitialized);
		applySessionsFilterAndRefreshTable();
		statusLabel->setText(m_filteredSessions.isEmpty() ? "No local sessions found" : QString("Local sessions: %1").arg(m_filteredSessions.size()));
		qDebug() << "Sessions tab list loaded" << "reason=" << reason << "path=" << sessions_path << "count=" << m_filteredSessions.size();
}

void ArsTrackerSessionsTab::rebuildTeamFilterCombo(bool resetToDefaultSelection)
{
		if (teamFilterCombo == nullptr)
		{
				return;
		}
		const int currentData = teamFilterCombo->currentData().isValid() ? teamFilterCombo->currentData().toInt() : -999;
		const QSignalBlocker blocker(teamFilterCombo);
		teamFilterCombo->clear();
		teamFilterCombo->addItem("All teams", -999);
		teamFilterCombo->addItem("Not configured", -1);
		for (const ArsTeam &team : m_teams)
		{
				teamFilterCombo->addItem(teamDisplayName(team), team.teamId);
		}

		int selection = currentData;
		if (resetToDefaultSelection)
		{
				selection = -999;
				if (m_defaultTeamId > 0 && m_teamsById.contains(m_defaultTeamId))
				{
						selection = m_defaultTeamId;
				}
				m_teamFilterInitialized = true;
		}
		int index = teamFilterCombo->findData(selection);
		if (index < 0)
		{
				index = teamFilterCombo->findData(-999);
		}
		teamFilterCombo->setCurrentIndex(index);
		m_selectedTeamFilterData = teamFilterCombo->currentData().isValid() ? teamFilterCombo->currentData().toInt() : -999;
		qDebug() << "Sessions filter selected id=" << m_selectedTeamFilterData
						 << "mode=" << (m_selectedTeamFilterData == -999 ? "all" : (m_selectedTeamFilterData == -1 ? "not-configured" : "team"));
}

void ArsTrackerSessionsTab::applySessionsFilterAndRefreshTable()
{
		const int mode = teamFilterCombo != nullptr && teamFilterCombo->currentData().isValid() ? teamFilterCombo->currentData().toInt() : -999;
		m_filteredSessions.clear();
		for (const LocalSessionInfo &session : m_allSessions)
		{
				bool keep = false;
				if (mode == -999)
				{
						keep = true;
				}
				else if (mode == -1)
				{
						keep = !session.hasExplicitTeamId;
				}
				else
				{
						keep = session.hasExplicitTeamId && session.explicitTeamId == mode;
				}
				qDebug() << "Sessions filter result session=" << session.folderName
								 << "hasExplicitTeamId=" << (session.hasExplicitTeamId ? 1 : 0)
								 << "explicitTeamId=" << session.explicitTeamId
								 << "mode=" << (mode == -999 ? "all" : (mode == -1 ? "not-configured" : "specific"))
								 << "selectedTeamId=" << mode
								 << "accepted=" << (keep ? 1 : 0);
				if (keep)
				{
						m_filteredSessions.append(session);
				}
		}
		fillSessionsTable(m_filteredSessions);
		scheduleSessionsListColumnResize();
		qDebug() << "Sessions filtered sessions count=" << m_filteredSessions.size()
						 << "total=" << m_allSessions.size()
						 << "mode=" << (mode == -999 ? "all" : (mode == -1 ? "not-configured" : "specific"))
						 << "selectedTeamId=" << mode;
}

void ArsTrackerSessionsTab::showSessionsListPage(bool forceReload)
{
		if (forceReload)
		{
				reloadSessions("back-to-list");
		}
		pagesStack->setCurrentWidget(listPage);
		scheduleSessionsListColumnResize();
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
		m_currentSessionStartTime = QTime(0, 0, 0);
		m_currentSessionStartTimestampValid = false;
		m_currentSessionFinishKnown = false;
		m_currentSessionDurationMs = -1;
		setSessionTimeSummaryPlaceholder();
		resetSessionInformationFieldsToDefaults();
		qDebug() << "Sessions tab session info reset defaults"
						 << "session=" << sessionId;
		ArsSessionInfo loadedInfo;
		bool fileExists = false;
		bool hasPlannedSessionPeriod = false;
		QStringList loadWarnings;
		const bool loaded = loadSessionInfoJsonIntoUi(sessionPath, &loadedInfo, &fileExists, &hasPlannedSessionPeriod, &loadWarnings);
		for (const QString &w : loadWarnings)
		{
				qWarning() << "Sessions tab SessionInfo.json load warning" << w;
		}
		qDebug() << "Sessions tab SessionInfo planned period state"
						 << "fileExists=" << fileExists
						 << "hasPlannedSessionPeriod=" << hasPlannedSessionPeriod
						 << "reason=" << (loaded ? "loaded" : (fileExists ? "load failed" : "missing file"));
		if (!loaded)
		{
				const QString reason = QFileInfo::exists(QDir(sessionPath).filePath("SessionInfo.json"))
															 ? "actualTime unavailable in SessionInfo.json"
															 : "no SessionInfo.json";
				qDebug() << "Sessions tab session actual time placeholder" << "session=" << sessionId << "reason=" << reason;
		}
		if (loaded && hasPlannedSessionPeriod)
		{
				qDebug() << "Sessions tab planned period loaded from SessionInfo"
								 << "start=" << timeSessionStart->time().toString("HH:mm:ss")
								 << "finish=" << timeSessionFinish->time().toString("HH:mm:ss");
		}
		else if (loaded && loadedInfo.actualTime.valid)
		{
				const QTime actualStart = QTime::fromString(loadedInfo.actualTime.startTime, "HH:mm:ss");
				const QTime actualFinish = QTime::fromString(loadedInfo.actualTime.finishTime, "HH:mm:ss");
				if (actualStart.isValid() && actualFinish.isValid())
				{
						QTime recommendedStart = roundUpToNextHalfHour(actualStart);
						QTime recommendedFinish = roundDownToPreviousHalfHour(actualFinish);
						if (recommendedFinish <= recommendedStart)
						{
								qWarning() << "Sessions tab planned period recommendation invalid interval"
													 << "start=" << recommendedStart.toString("HH:mm:ss")
													 << "finish=" << recommendedFinish.toString("HH:mm:ss")
													 << ", fallback applied";
								recommendedFinish = recommendedStart.addSecs(90 * 60);
						}
						timeSessionStart->setTime(QTime(recommendedStart.hour(), recommendedStart.minute(), 0));
						timeSessionFinish->setTime(QTime(recommendedFinish.hour(), recommendedFinish.minute(), 0));
						qDebug() << "Sessions tab planned period recommended"
										 << "start=" << timeSessionStart->time().toString("HH:mm:ss")
										 << "finish=" << timeSessionFinish->time().toString("HH:mm:ss")
										 << "actualStart=" << actualStart.toString("HH:mm:ss")
										 << "actualFinish=" << actualFinish.toString("HH:mm:ss");
				}
		}

		fillSessionTrackersTable(scanSessionTrackers(sessionPath));
		refreshSessionDetailsTeamUi();
		detailsStatusLabel->setText("Ready to process");
		pagesStack->setCurrentWidget(detailsPage);

		const bool needsActualTimeScan = !loaded || !loadedInfo.actualTime.valid;
		const bool needsRecommendationScan = (!loaded || !hasPlannedSessionPeriod) && !loadedInfo.actualTime.valid;
		if (needsActualTimeScan || needsRecommendationScan)
		{
				const QString reason = !loaded ? "missingSessionInfo" : (!loadedInfo.actualTime.valid ? "missingActualTime" : "missingPlannedPeriod");
				qDebug() << "Sessions tab initial duration scan requested" << "sessionPath=" << sessionPath << "reason=" << reason;
				startSessionDurationScan(true, true, needsRecommendationScan, false);
		}
}

ArsSessionTargetSettings ArsTrackerSessionsTab::readTargetSettingsFromUi() const
{
		ArsSessionTargetSettings s;
		s.targetDistanceKm = spinTargetDistanceKm->value();
		s.targetAccelerationDistanceM = spinTargetAccelerationDistanceM->value();
		s.targetFootload10_3g = spinTargetFootload10_3g->value();
		s.targetTouchesCount = spinTargetTouchesCount->value();
		s.targetFootloadPerMin = spinTargetFootloadPerMin->value();
		return s;
}

ArsSessionInfo ArsTrackerSessionsTab::readSessionInfoFromUi() const
{
		ArsSessionInfo info;
		info.parameters.type = comboSessionType->currentData().toString();
		info.parameters.startTime = timeSessionStart->time();
		info.parameters.endTime = timeSessionFinish->time();
		info.parameters.location = editSessionLocation->text().trimmed();
		for (const QString &line : editSessionGoals->toPlainText().split('\n'))
		{
				const QString trimmed = line.trimmed();
				if (!trimmed.isEmpty())
				{
						info.parameters.goals.append(trimmed);
				}
		}
		const ArsSessionTargetSettings t = readTargetSettingsFromUi();
		info.plannedMetrics.distanceKm = t.targetDistanceKm;
		info.plannedMetrics.accelerationDistanceM = t.targetAccelerationDistanceM;
		info.plannedMetrics.footloadPerLeg = t.targetFootload10_3g;
		info.plannedMetrics.loadIntensityGPerMin = t.targetFootloadPerMin;
		info.plannedMetrics.touches = t.targetTouchesCount;
		info.plannedMetrics.maxSpeedMps = 0.0;
		info.plannedMetrics.shots = 0;
		info.plannedMetrics.dribbles = 0;
		return info;
}

bool ArsTrackerSessionsTab::validateSessionInfo(const ArsSessionInfo &info, QStringList *problems) const
{
		if (info.parameters.endTime <= info.parameters.startTime)
		{
				if (problems != nullptr)
				{
						problems->append("Planned session finish time must be later than start time.");
				}
				return false;
		}
		return true;
}

void ArsTrackerSessionsTab::resetSessionInformationFieldsToDefaults()
{
		comboSessionType->setCurrentIndex(0);
		timeSessionStart->setTime(kDefaultPlannedStart);
		timeSessionFinish->setTime(kDefaultPlannedFinish);
		editSessionLocation->clear();
		editSessionGoals->clear();
		spinTargetDistanceKm->setValue(0.0);
		spinTargetAccelerationDistanceM->setValue(0);
		spinTargetFootload10_3g->setValue(0);
		spinTargetTouchesCount->setValue(0);
		spinTargetFootloadPerMin->setValue(0.0);
}

void ArsTrackerSessionsTab::applySessionInfoToUi(const ArsSessionInfo &info)
{
		const QString type = info.parameters.type.trimmed().toLower();
		if (type == "match")
		{
				comboSessionType->setCurrentIndex(1);
		}
		else
		{
				if (!type.isEmpty() && type != "training")
				{
						qWarning() << "Sessions tab SessionInfo.json unknown type value, defaulting to training:" << info.parameters.type;
				}
				comboSessionType->setCurrentIndex(0);
		}
		if (info.parameters.startTime.isValid())
		{
				timeSessionStart->setTime(info.parameters.startTime);
		}
		if (info.parameters.endTime.isValid())
		{
				timeSessionFinish->setTime(info.parameters.endTime);
		}
		editSessionLocation->setText(info.parameters.location);
		editSessionGoals->setPlainText(info.parameters.goals.join("\n"));

		spinTargetDistanceKm->setValue(info.plannedMetrics.distanceKm);
		spinTargetAccelerationDistanceM->setValue(info.plannedMetrics.accelerationDistanceM);
		spinTargetFootload10_3g->setValue(info.plannedMetrics.footloadPerLeg);
		spinTargetTouchesCount->setValue(info.plannedMetrics.touches);
		spinTargetFootloadPerMin->setValue(info.plannedMetrics.loadIntensityGPerMin);
}

bool ArsTrackerSessionsTab::loadSessionInfoJsonIntoUi(const QString &sessionPath,
																											ArsSessionInfo *loadedInfo,
																											bool *fileExists,
																											bool *hasPlannedSessionPeriod,
																											QStringList *warnings)
{
		const QString filePath = QDir(sessionPath).filePath("SessionInfo.json");
		qDebug() << "Sessions tab SessionInfo.json load" << "path=" << filePath;
		if (!QFileInfo::exists(filePath))
		{
				if (fileExists != nullptr)
				{
						*fileExists = false;
				}
				if (hasPlannedSessionPeriod != nullptr)
				{
						*hasPlannedSessionPeriod = false;
				}
				qDebug() << "Sessions tab SessionInfo.json missing" << "path=" << filePath;
				return false;
		}

		ArsSessionInfo info;
		QString error;
		if (!ArsSessionInfoJson::loadSessionInfoJson(sessionPath, &info, fileExists, hasPlannedSessionPeriod, &error))
		{
				qWarning() << "Sessions tab SessionInfo.json load failed" << "path=" << filePath << "error=" << error;
				if (warnings != nullptr)
				{
						warnings->append(error);
				}
				return false;
		}
		applySessionInfoToUi(info);
		if (loadedInfo != nullptr)
		{
				*loadedInfo = info;
		}
		if (info.actualTime.valid)
		{
				setSessionTimeSummary(info.actualTime.startTime, info.actualTime.finishTime, info.actualTime.duration);
				qDebug() << "Sessions tab session actual time loaded"
								 << "start=" << info.actualTime.startTime
								 << "finish=" << info.actualTime.finishTime
								 << "duration=" << info.actualTime.duration;
		}
		else
		{
				setSessionTimeSummaryPlaceholder();
				qDebug() << "Sessions tab session actual time placeholder"
								 << "session=" << currentSessionId
								 << "reason=actualTime missing in SessionInfo.json";
		}
		qDebug() << "Sessions tab SessionInfo.json loaded" << "ok=true";
		return true;
}

bool ArsTrackerSessionsTab::saveSessionInfoJson(const QString &sessionPath, const ArsSessionInfo &info, QString *errorMessage) const
{
		return ArsSessionInfoJson::saveSessionInfoJson(sessionPath, info, errorMessage);
}

void ArsTrackerSessionsTab::updateSessionTimeSummaryFromMaxTimestamp(uint32_t maxTimestamp100ms, bool hasTimestamp)
{
		if (hasTimestamp)
		{
				m_currentSessionDurationMs = static_cast<qint64>(maxTimestamp100ms) * 100;
				const QTime actualStart = sessionStartTimeFromSessionName(currentSessionId, nullptr);
				const QDateTime finish = QDateTime(QDate(2000, 1, 1), actualStart).addMSecs(m_currentSessionDurationMs);
				m_currentSessionFinishTime = finish.time();
				m_currentSessionFinishKnown = true;
				m_currentSessionStartTime = actualStart;
				setSessionTimeSummary(actualStart.toString("HH:mm:ss"),
															m_currentSessionFinishTime.toString("HH:mm:ss"),
															formatDurationMs(m_currentSessionDurationMs));
		}
		else
		{
				m_currentSessionDurationMs = -1;
				m_currentSessionFinishKnown = false;
				setSessionTimeSummaryPlaceholder();
		}
}

void ArsTrackerSessionsTab::onProcessSessionClicked()
{
		if (currentSessionId.trimmed().isEmpty())
		{
				return;
		}
		const QString sessionPath = QDir(sessionsPath()).filePath(currentSessionId);
		if (sessionsPath().trimmed().isEmpty() || !QDir(sessionPath).exists())
		{
				detailsStatusLabel->setText("Session path is unavailable");
				return;
		}

		processButton->setEnabled(false);
		if (rescanButton != nullptr)
		{
				rescanButton->setEnabled(false);
		}
		m_processProblems.clear();
		m_processPairInputs.clear();
		m_processPairIndex = 0;
		m_processMaxIntegralTimestamp = 0;
		m_processHasIntegralTimestamp = false;
		m_processValidationOk = true;

		m_processDialog = new QDialog(this);
		m_processDialog->setWindowTitle("Processing session");
		m_processDialog->setModal(true);
		QVBoxLayout *dialogLayout = new QVBoxLayout(m_processDialog);
		m_processStatusLabel = new QLabel("Preparing session processing...", m_processDialog);
		dialogLayout->addWidget(m_processStatusLabel);
		m_processProgressBar = new QProgressBar(m_processDialog);
		m_processProgressBar->setMinimum(0);
		m_processProgressBar->setMaximum(1);
		m_processProgressBar->setValue(0);
		dialogLayout->addWidget(m_processProgressBar);
		m_processResultText = new QPlainTextEdit(m_processDialog);
		m_processResultText->setReadOnly(true);
		m_processResultText->setVisible(false);
		dialogLayout->addWidget(m_processResultText);
		QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, m_processDialog);
		m_processCloseButton = buttons->button(QDialogButtonBox::Close);
		m_processCloseButton->setVisible(false);
		m_processCloseButton->setEnabled(false);
		connect(m_processCloseButton, &QPushButton::clicked, m_processDialog, &QDialog::accept);
		dialogLayout->addWidget(buttons);

		QTimer::singleShot(0, this, &ArsTrackerSessionsTab::startSessionProcessingFlow);
		m_processDialog->exec();
		m_processDialog->deleteLater();
		m_processDialog = nullptr;
		m_processStatusLabel = nullptr;
		m_processProgressBar = nullptr;
		m_processResultText = nullptr;
		m_processCloseButton = nullptr;
		processButton->setEnabled(true);
		if (rescanButton != nullptr)
		{
				rescanButton->setEnabled(true);
		}
}

void ArsTrackerSessionsTab::onRescanSessionClicked()
{
		if (currentSessionId.trimmed().isEmpty())
		{
				return;
		}
		const QString sessionPath = QDir(sessionsPath()).filePath(currentSessionId);
		if (sessionsPath().trimmed().isEmpty() || !QDir(sessionPath).exists())
		{
				detailsStatusLabel->setText("Session path is unavailable");
				return;
		}
		qDebug() << "Sessions tab rescan requested" << "sessionPath=" << sessionPath;
		startSessionDurationScan(false, true, true, true);
}

void ArsTrackerSessionsTab::startSessionDurationScan(bool isInitial,
																										 bool saveJsonAfterScan,
																										 bool shouldRecommendPlannedPeriod,
																										 bool forceRecommendation)
{
		if (m_scanDialog != nullptr)
		{
				return;
		}
		const QString sessionPath = QDir(sessionsPath()).filePath(currentSessionId);
		if (sessionPath.trimmed().isEmpty() || !QDir(sessionPath).exists())
		{
				return;
		}

		m_scanIsInitial = isInitial;
		m_scanShouldSaveJson = saveJsonAfterScan;
		m_scanShouldRecommendPlannedPeriod = shouldRecommendPlannedPeriod;
		m_scanForceRecommendation = forceRecommendation;
		m_scanIndex = 0;
		m_scanMaxTimestamp100ms = 0;
		m_scanHasTimestamp = false;
		m_scanProblems.clear();
		QStringList inputWarnings;
		m_scanInputs = ArsSessionDurationScanner::scanDurationInputs(sessionPath, &inputWarnings);
		m_scanProblems.append(inputWarnings);
		qDebug() << "Sessions tab duration scan inputs count=" << m_scanInputs.size();

		processButton->setEnabled(false);
		if (rescanButton != nullptr)
		{
				rescanButton->setEnabled(false);
		}

		m_scanDialog = new QDialog(this);
		m_scanDialog->setWindowTitle("Scanning session");
		m_scanDialog->setModal(true);
		QVBoxLayout *layout = new QVBoxLayout(m_scanDialog);
		m_scanStatusLabel = new QLabel("Scanning session data...", m_scanDialog);
		layout->addWidget(m_scanStatusLabel);
		m_scanProgressBar = new QProgressBar(m_scanDialog);
		m_scanProgressBar->setMinimum(0);
		m_scanProgressBar->setMaximum(m_scanInputs.isEmpty() ? 1 : m_scanInputs.size());
		m_scanProgressBar->setValue(0);
		layout->addWidget(m_scanProgressBar);
		m_scanResultText = new QPlainTextEdit(m_scanDialog);
		m_scanResultText->setReadOnly(true);
		m_scanResultText->setVisible(false);
		layout->addWidget(m_scanResultText);
		QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, m_scanDialog);
		m_scanCloseButton = buttons->button(QDialogButtonBox::Close);
		m_scanCloseButton->setVisible(false);
		m_scanCloseButton->setEnabled(false);
		connect(m_scanCloseButton, &QPushButton::clicked, m_scanDialog, &QDialog::accept);
		layout->addWidget(buttons);

		QTimer::singleShot(0, this, &ArsTrackerSessionsTab::processNextDurationScanFile);
		m_scanDialog->exec();
		m_scanDialog->deleteLater();
		m_scanDialog = nullptr;
		m_scanStatusLabel = nullptr;
		m_scanProgressBar = nullptr;
		m_scanResultText = nullptr;
		m_scanCloseButton = nullptr;

		processButton->setEnabled(true);
		if (rescanButton != nullptr)
		{
				rescanButton->setEnabled(true);
		}
}

void ArsTrackerSessionsTab::processNextDurationScanFile()
{
		if (m_scanDialog == nullptr || m_scanProgressBar == nullptr)
		{
				return;
		}
		const int total = m_scanInputs.size();
		if (m_scanIndex >= total)
		{
				finishSessionDurationScan();
				return;
		}

		const ArsDurationScanFileInput input = m_scanInputs.at(m_scanIndex);
		const int index = m_scanIndex + 1;
		m_scanStatusLabel->setText(QString("Scanning tracker %1 of %2: %3").arg(index).arg(total).arg(input.trackerFolderName));
		qDebug() << "Sessions tab duration scan file begin"
						 << "index=" << index
						 << "total=" << total
						 << "tracker=" << input.trackerFolderName
						 << "path=" << input.processedStrPath;

		const ArsDurationScanFileResult result = ArsSessionDurationScanner::scanDurationFile(input);
		for (const QString &w : result.warnings)
		{
				m_scanProblems.append(w);
		}
		if (result.ok && result.hasTimestamp)
		{
				if (!m_scanHasTimestamp || result.maxTimestamp100ms > m_scanMaxTimestamp100ms)
				{
						m_scanMaxTimestamp100ms = result.maxTimestamp100ms;
				}
				m_scanHasTimestamp = true;
		}
		qDebug() << "Sessions tab duration scan file done"
						 << "tracker=" << input.trackerFolderName
						 << "hasTimestamp=" << result.hasTimestamp
						 << "maxTimestamp100ms=" << result.maxTimestamp100ms;

		m_scanIndex++;
		m_scanProgressBar->setValue(m_scanIndex);
		qDebug() << "Sessions tab duration scan progress" << "value=" << m_scanIndex << "total=" << total;
		QTimer::singleShot(0, this, &ArsTrackerSessionsTab::processNextDurationScanFile);
}

void ArsTrackerSessionsTab::finishSessionDurationScan()
{
		const QString sessionPath = QDir(sessionsPath()).filePath(currentSessionId);
		if (m_scanProgressBar != nullptr)
		{
				m_scanProgressBar->setMaximum(m_scanInputs.isEmpty() ? 1 : m_scanInputs.size());
				m_scanProgressBar->setValue(m_scanInputs.isEmpty() ? 1 : m_scanInputs.size());
		}

		if (m_scanHasTimestamp)
		{
				updateSessionTimeSummaryFromMaxTimestamp(m_scanMaxTimestamp100ms, true);
				const QTime actualStart = m_currentSessionStartTime;
				const QTime actualFinish = m_currentSessionFinishTime;
				qDebug() << "Sessions tab duration scan done"
								 << "hasTimestamp=" << true
								 << "maxTimestamp100ms=" << m_scanMaxTimestamp100ms
								 << "durationMs=" << m_currentSessionDurationMs
								 << "saveJson=" << m_scanShouldSaveJson;
				const bool shouldApplyRecommendation = m_scanForceRecommendation || m_scanShouldRecommendPlannedPeriod;
				if (shouldApplyRecommendation)
				{
						QTime recommendedStart = roundUpToNextHalfHour(actualStart);
						QTime recommendedFinish = roundDownToPreviousHalfHour(actualFinish);
						if (recommendedFinish <= recommendedStart)
						{
								qWarning() << "Sessions tab planned period recommendation invalid interval"
													 << "start=" << recommendedStart.toString("HH:mm:ss")
													 << "finish=" << recommendedFinish.toString("HH:mm:ss")
													 << ", fallback applied";
								recommendedFinish = recommendedStart.addSecs(90 * 60);
						}
						timeSessionStart->setTime(QTime(recommendedStart.hour(), recommendedStart.minute(), 0));
						timeSessionFinish->setTime(QTime(recommendedFinish.hour(), recommendedFinish.minute(), 0));
						qDebug() << "Sessions tab planned period recommended"
										 << "start=" << timeSessionStart->time().toString("HH:mm:ss")
										 << "finish=" << timeSessionFinish->time().toString("HH:mm:ss")
										 << "actualStart=" << actualStart.toString("HH:mm:ss")
										 << "actualFinish=" << actualFinish.toString("HH:mm:ss");
				}

				if (m_scanShouldSaveJson)
				{
						ArsSessionInfo::ArsSessionActualTime actualTime;
						actualTime.valid = true;
						actualTime.startTime = actualStart.toString("HH:mm:ss");
						actualTime.finishTime = actualFinish.toString("HH:mm:ss");
						actualTime.duration = formatDurationMs(m_currentSessionDurationMs);
						actualTime.durationMs = m_currentSessionDurationMs;
						actualTime.maxIntegralTimestamp100ms = m_scanMaxTimestamp100ms;
						qDebug() << "Sessions tab duration scan actualTime calculated"
										 << "start=" << actualTime.startTime
										 << "finish=" << actualTime.finishTime
										 << "duration=" << actualTime.duration
										 << "durationMs=" << actualTime.durationMs
										 << "maxTimestamp100ms=" << actualTime.maxIntegralTimestamp100ms;
						QString saveError;
						if (!ArsSessionInfoJson::updateSessionInfoActualTimeJson(sessionPath, actualTime, &saveError))
						{
								m_scanProblems.append(QString("SessionInfo.json actualTime update failed: %1").arg(saveError));
								qWarning() << "Sessions tab SessionInfo actualTime update failed"
													 << "path=" << QDir(sessionPath).filePath("SessionInfo.json")
													 << "error=" << saveError;
						}
						else
						{
								qDebug() << "Sessions tab rescan saved SessionInfo.json path=" << QDir(sessionPath).filePath("SessionInfo.json");
						}
				}
		}
		else
		{
			setSessionTimeSummaryPlaceholder();
			m_scanProblems.append("Unable to calculate actual session time: no integral state timestamps found.");
			qWarning() << "Sessions tab duration scan done"
								 << "hasTimestamp=" << false
								 << "maxTimestamp100ms=0"
								 << "durationMs=-1"
								 << "saveJson=" << m_scanShouldSaveJson;
			if (m_scanShouldSaveJson)
			{
					m_scanProblems.append("SessionInfo.json actualTime was not updated: no integral state timestamps found.");
					qWarning() << "Sessions tab SessionInfo actualTime update skipped reason=no integral timestamps";
			}
		}

		const bool ok = m_scanProblems.isEmpty();
		if (ok)
		{
				m_scanStatusLabel->setText(QString("Session scan completed successfully.\nSessionInfo.json actual time updated.\nDuration: %1\nFinish time: %2")
																	 .arg(formatDurationMs(m_currentSessionDurationMs),
																			 m_currentSessionFinishKnown ? m_currentSessionFinishTime.toString("HH:mm:ss") : "--:--:--"));
				detailsStatusLabel->setText("Session scan completed successfully.");
		}
		else
		{
				m_scanStatusLabel->setText("Session scan completed with warnings.");
				m_scanResultText->setVisible(true);
				QString text = "Problems:\n";
				for (const QString &p : m_scanProblems)
				{
						text += QString("- %1\n").arg(p);
				}
				m_scanResultText->setPlainText(text.trimmed());
				detailsStatusLabel->setText("Session scan completed with warnings.");
		}
		m_scanCloseButton->setVisible(true);
		m_scanCloseButton->setEnabled(true);
}

void ArsTrackerSessionsTab::startSessionProcessingFlow()
{
		QApplication::setOverrideCursor(Qt::WaitCursor);
		const QString sessionPath = QDir(sessionsPath()).filePath(currentSessionId);
		const QString outputPath = QDir(sessionPath).filePath("postprocessed");
		m_processSuccesses.clear();
		qDebug() << "Sessions tab process begin" << "sessionPath=" << sessionPath << "outputPath=" << outputPath;
		qDebug() << "Sessions tab actual time calculation begin" << "session=" << currentSessionId;

		m_pendingProcessSessionInfo = readSessionInfoFromUi();
		if (!validateSessionInfo(m_pendingProcessSessionInfo, &m_processProblems))
		{
				m_processValidationOk = false;
				qWarning() << "Sessions tab process validation failed";
		}

		const QTime plannedStart = timeSessionStart->time();
		const QTime plannedFinish = timeSessionFinish->time();
		if (plannedFinish <= plannedStart)
		{
				m_processProblems.append("Planned session finish time must be later than start time.");
		}
		// TODO: support planned session period crossing midnight.
		const QTime sessionStart = sessionStartTimeFromSessionName(currentSessionId, nullptr);
		const int startOffsetSecRaw = sessionStart.secsTo(plannedStart);
		const int finishOffsetSec = sessionStart.secsTo(plannedFinish);
		int startOffsetSec = startOffsetSecRaw;
		if (startOffsetSec < 0)
		{
				startOffsetSec = 0;
				m_processProblems.append("Planned start is before session start, clamped to 00:00.");
		}
		if (finishOffsetSec <= 0)
		{
				m_processProblems.append("Planned finish is before session start.");
		}
		const qint64 start100ms = static_cast<qint64>(startOffsetSec) * 10;
		const qint64 finish100ms = static_cast<qint64>(finishOffsetSec) * 10;
		m_postprocessRequest.sessionName = currentSessionId;
		m_postprocessRequest.outputPath = outputPath;
		m_postprocessRequest.timeRange.startTimestamp100ms = static_cast<uint32_t>(std::max<qint64>(0, start100ms));
		m_postprocessRequest.timeRange.finishTimestamp100ms = static_cast<uint32_t>(std::max<qint64>(0, finish100ms));
		m_postprocessRequest.timeRange.plannedStartTimeText = plannedStart.toString("HH:mm:ss");
		m_postprocessRequest.timeRange.plannedFinishTimeText = plannedFinish.toString("HH:mm:ss");
		qDebug() << "Sessions tab postprocessing begin"
						 << "sessionPath=" << sessionPath
						 << "outputPath=" << outputPath
						 << "plannedStart=" << m_postprocessRequest.timeRange.plannedStartTimeText
						 << "plannedFinish=" << m_postprocessRequest.timeRange.plannedFinishTimeText
						 << "start100ms=" << m_postprocessRequest.timeRange.startTimestamp100ms
						 << "finish100ms=" << m_postprocessRequest.timeRange.finishTimestamp100ms;

		if (m_processProblems.isEmpty())
		{
				if (!QDir().mkpath(outputPath))
				{
						m_processProblems.append(QString("Failed to create postprocessed directory: %1").arg(outputPath));
				}
		}

		if (m_processProblems.isEmpty())
		{
				QStringList warnings;
				m_processPairInputs = ArsSessionProcessingLoader::scanSessionPairs(sessionPath, &warnings);
				qDebug() << "Sessions tab process pair scan" << "pairs=" << m_processPairInputs.size();
				for (const QString &w : warnings)
				{
						m_processProblems.append(w);
				}
		}
		if (m_processPairInputs.isEmpty() && m_processProblems.isEmpty())
		{
				m_processProblems.append("No tracker pairs found for processing.");
		}

		const int totalPairs = m_processPairInputs.size();
		m_processProgressBar->setMinimum(0);
		m_processProgressBar->setMaximum(totalPairs > 0 ? totalPairs : 1);
		m_processProgressBar->setValue(0);
		m_processPairIndex = 0;
		if (totalPairs == 0 || !m_processProblems.isEmpty())
		{
				if (totalPairs == 0)
				{
						m_processProgressBar->setValue(1);
				}
				finishSessionProcessingFlow();
				return;
		}
		QTimer::singleShot(0, this, &ArsTrackerSessionsTab::processNextSessionPair);
}

void ArsTrackerSessionsTab::processNextSessionPair()
{
		const int total = m_processPairInputs.size();
		if (m_processPairIndex >= total)
		{
				finishSessionProcessingFlow();
				return;
		}
		const ArsSessionPairInput pairInput = m_processPairInputs.at(m_processPairIndex);
		const int index = m_processPairIndex + 1;
		m_processStatusLabel->setText(QString("Processing pair %1 of %2: %3\nParsing and postprocessing...")
																	.arg(index)
																	.arg(total)
																	.arg(pairInput.pairSerial));
		qDebug() << "Sessions tab process pair begin" << "index=" << index << "total=" << total << "serial=" << pairInput.pairSerial;
		qDebug() << "Sessions tab postprocessing pair begin" << "serial=" << pairInput.pairSerial;

		QStringList pairWarnings;
		const ArsPairProcessedData pair = ArsSessionProcessingLoader::loadPair(pairInput, &pairWarnings);
		const bool hasLeft = pair.left.has_value();
		const bool hasRight = pair.right.has_value();
		const int leftMalformed = hasLeft ? pair.left->data.malformedLines : 0;
		const int rightMalformed = hasRight ? pair.right->data.malformedLines : 0;

		if (hasLeft && !pair.left->data.integralStates.empty())
		{
				const uint32_t maxTs = maxIntegralTimestamp(pair.left->data.integralStates);
				if (!m_processHasIntegralTimestamp || maxTs > m_processMaxIntegralTimestamp)
				{
						m_processMaxIntegralTimestamp = maxTs;
				}
				m_processHasIntegralTimestamp = true;
		}
		if (hasRight && !pair.right->data.integralStates.empty())
		{
				const uint32_t maxTs = maxIntegralTimestamp(pair.right->data.integralStates);
				if (!m_processHasIntegralTimestamp || maxTs > m_processMaxIntegralTimestamp)
				{
						m_processMaxIntegralTimestamp = maxTs;
				}
				m_processHasIntegralTimestamp = true;
		}
		if (m_processHasIntegralTimestamp)
		{
				qDebug() << "Sessions tab actual time update pair="
								 << pairInput.pairSerial
								 << "maxTimestamp100ms=" << m_processMaxIntegralTimestamp;
		}

		if (!pairInput.hasLeft)
		{
				m_processProblems.append(QString("pair %1: missing left tracker folder").arg(pairInput.pairSerial));
		}
		if (!pairInput.hasRight)
		{
				m_processProblems.append(QString("pair %1: missing right tracker folder").arg(pairInput.pairSerial));
		}
		if (pairInput.hasLeft && (pairInput.leftProcessedStrPath.trimmed().isEmpty() || !QFileInfo::exists(pairInput.leftProcessedStrPath)))
		{
				m_processProblems.append(QString("pair %1: missing left processedStr.csv").arg(pairInput.pairSerial));
		}
		if (pairInput.hasRight && (pairInput.rightProcessedStrPath.trimmed().isEmpty() || !QFileInfo::exists(pairInput.rightProcessedStrPath)))
		{
				m_processProblems.append(QString("pair %1: missing right processedStr.csv").arg(pairInput.pairSerial));
		}
		if (hasLeft && leftMalformed > 0)
		{
				m_processProblems.append(QString("pair %1 L: malformed lines: %2").arg(pairInput.pairSerial).arg(leftMalformed));
		}
		if (hasRight && rightMalformed > 0)
		{
				m_processProblems.append(QString("pair %1 R: malformed lines: %2").arg(pairInput.pairSerial).arg(rightMalformed));
		}

		for (const QString &w : pairWarnings)
		{
				m_processProblems.append(w);
		}

		ArsPairPostprocessResult postResult;
		if (hasLeft && hasRight)
		{
				postResult = ArsSessionPostprocessor::processPair(m_postprocessRequest, pair);
				if (!postResult.ok)
				{
						for (const QString &p : postResult.problems)
						{
								m_processProblems.append(p);
						}
						qWarning() << "Sessions tab postprocessing pair failed"
											 << "serial=" << pairInput.pairSerial
											 << "errors=" << postResult.problems;
				}
				else
				{
						m_processSuccesses.append(QString("%1: saved %2, %3")
																				.arg(pairInput.pairSerial,
																						 QFileInfo(postResult.jsonPath).fileName(),
																						 QFileInfo(postResult.touchIntensityCsvPath).fileName()));
						qDebug() << "Sessions tab postprocessing pair algorithm done" << "serial=" << pairInput.pairSerial;
						qDebug() << "Sessions tab postprocessing pair artifacts saved"
										 << "serial=" << pairInput.pairSerial
										 << "json=" << postResult.jsonPath
										 << "csv=" << postResult.touchIntensityCsvPath;
				}
		}

		const bool pairOk = pairWarnings.isEmpty() && leftMalformed == 0 && rightMalformed == 0 && pairInput.hasLeft && pairInput.hasRight;
		qDebug() << "Sessions tab process pair done"
						 << "index=" << index
						 << "total=" << total
						 << "serial=" << pairInput.pairSerial
						 << "ok=" << pairOk
						 << "problems=" << m_processProblems.size();

		if (hasLeft && leftMalformed > 0)
		{
				const int count = std::min(static_cast<int>(pair.left->data.malformedLineDetails.size()), kMaxMalformedLinesToLog);
				for (int i = 0; i < count; ++i)
				{
						const ArsMalformedProcessedStrLine &d = pair.left->data.malformedLineDetails.at(i);
						qWarning() << "Sessions tab malformed line"
											 << "pair=" << pairInput.pairSerial
											 << "foot=L"
											 << "line=" << d.lineNumber
											 << "prefix=" << d.prefix
											 << "reason=" << d.reason
											 << "text=" << d.text;
				}
		}
		if (hasRight && rightMalformed > 0)
		{
				const int count = std::min(static_cast<int>(pair.right->data.malformedLineDetails.size()), kMaxMalformedLinesToLog);
				for (int i = 0; i < count; ++i)
				{
						const ArsMalformedProcessedStrLine &d = pair.right->data.malformedLineDetails.at(i);
						qWarning() << "Sessions tab malformed line"
											 << "pair=" << pairInput.pairSerial
											 << "foot=R"
											 << "line=" << d.lineNumber
											 << "prefix=" << d.prefix
											 << "reason=" << d.reason
											 << "text=" << d.text;
				}
		}

		m_processPairIndex++;
		m_processProgressBar->setValue(m_processPairIndex);
		qDebug() << "Sessions tab process progress" << "value=" << m_processPairIndex << "total=" << total;
		// TODO: move per-pair processing to worker thread if single pair parsing becomes slow enough to block UI.
		QTimer::singleShot(0, this, &ArsTrackerSessionsTab::processNextSessionPair);
}

void ArsTrackerSessionsTab::finishSessionProcessingFlow()
{
		const QString sessionPath = QDir(sessionsPath()).filePath(currentSessionId);
		const int totalPairs = m_processPairInputs.size();
		const int processedPairs = m_processPairIndex;
		bool hasActualTime = false;
		if (m_processHasIntegralTimestamp)
		{
				updateSessionTimeSummaryFromMaxTimestamp(m_processMaxIntegralTimestamp, true);
				hasActualTime = true;
				m_pendingProcessSessionInfo.actualTime.valid = true;
				m_pendingProcessSessionInfo.actualTime.startTime = m_currentSessionStartTime.toString("HH:mm:ss");
				m_pendingProcessSessionInfo.actualTime.finishTime = m_currentSessionFinishTime.toString("HH:mm:ss");
				m_pendingProcessSessionInfo.actualTime.duration = formatDurationMs(m_currentSessionDurationMs);
				m_pendingProcessSessionInfo.actualTime.durationMs = m_currentSessionDurationMs;
				m_pendingProcessSessionInfo.actualTime.maxIntegralTimestamp100ms = m_processMaxIntegralTimestamp;
				qDebug() << "Sessions tab actual time calculated"
								 << "start=" << m_pendingProcessSessionInfo.actualTime.startTime
								 << "finish=" << m_pendingProcessSessionInfo.actualTime.finishTime
								 << "duration=" << m_pendingProcessSessionInfo.actualTime.duration
								 << "durationMs=" << m_pendingProcessSessionInfo.actualTime.durationMs
								 << "maxTimestamp100ms=" << m_pendingProcessSessionInfo.actualTime.maxIntegralTimestamp100ms;
				qDebug() << "Sessions tab process duration final"
								 << "maxTimestamp100ms=" << m_processMaxIntegralTimestamp
								 << "durationMs=" << m_currentSessionDurationMs
								 << "finishTime=" << m_currentSessionFinishTime.toString("HH:mm:ss")
								 << "duration=" << formatDurationMs(m_currentSessionDurationMs);
		}
		else
		{
				updateSessionTimeSummaryFromMaxTimestamp(0, false);
				m_pendingProcessSessionInfo.actualTime.valid = false;
				m_processProblems.append("Unable to calculate actual session time: no integral state timestamps found.");
				qWarning() << "Sessions tab actual time unavailable: no integral states";
		}

		if (m_processValidationOk)
		{
				QString saveError;
				if (!saveSessionInfoJson(sessionPath, m_pendingProcessSessionInfo, &saveError))
				{
						m_processProblems.append(QString("SessionInfo.json save failed: %1").arg(saveError));
						qWarning() << "Sessions tab SessionInfo.json save failed" << "path=" << QDir(sessionPath).filePath("SessionInfo.json") << "error=" << saveError;
				}
				else
				{
						if (hasActualTime)
						{
								qDebug() << "Sessions tab SessionInfo.json saved with actualTime" << "path=" << QDir(sessionPath).filePath("SessionInfo.json");
						}
						else
						{
								qDebug() << "Sessions tab SessionInfo.json saved without actualTime"
												 << "path=" << QDir(sessionPath).filePath("SessionInfo.json")
												 << "reason=no integral state timestamps";
						}
				}
		}

		const bool ok = m_processProblems.isEmpty();
		m_processProgressBar->setMaximum(totalPairs > 0 ? totalPairs : 1);
		m_processProgressBar->setValue(totalPairs > 0 ? totalPairs : 1);
		m_processCloseButton->setVisible(true);
		m_processCloseButton->setEnabled(true);
		m_processResultText->setVisible(!ok);
		if (totalPairs == 0)
		{
				m_processStatusLabel->setText("No tracker pairs found for processing.");
		}
		else if (ok)
		{
				m_processStatusLabel->setText(QString("Processing completed successfully.\nPairs processed: %1/%2\nArtifacts saved to:\n%3")
																	 .arg(processedPairs)
																	 .arg(totalPairs)
																	 .arg(m_postprocessRequest.outputPath));
				QString text = QString("Processing completed successfully.\nPairs processed: %1/%2\nArtifacts saved to:\n%3\n")
												 .arg(processedPairs)
												 .arg(totalPairs)
												 .arg(m_postprocessRequest.outputPath);
				if (!m_processSuccesses.isEmpty())
				{
						text += "\nSuccessful pairs:\n";
						for (const QString &s : m_processSuccesses)
						{
								text += QString("- %1\n").arg(s);
						}
				}
				m_processResultText->setVisible(true);
				m_processResultText->setPlainText(text.trimmed());
				detailsStatusLabel->setText(QString("Processing completed successfully. Pairs=%1").arg(totalPairs));
		}
		else
		{
				m_processStatusLabel->setText(QString("Processing completed with errors.\nPairs processed: %1/%2").arg(processedPairs).arg(totalPairs));
				QString text = QString("Processing completed with errors.\nPairs processed: %1/%2\nArtifacts path:\n%3\n")
												 .arg(processedPairs)
												 .arg(totalPairs)
												 .arg(m_postprocessRequest.outputPath);
				if (!m_processSuccesses.isEmpty())
				{
						text += "\nSuccessful pairs:\n";
						for (const QString &s : m_processSuccesses)
						{
								text += QString("- %1\n").arg(s);
						}
						text += "\n";
				}
				text += "Problems:\n";
				for (const QString &p : m_processProblems)
				{
						text += QString("- %1\n").arg(p);
				}
				m_processResultText->setPlainText(text.trimmed());
				detailsStatusLabel->setText("Processing completed with errors. See dialog/logs.");
		}
		qDebug() << "Sessions tab process done"
						 << "ok=" << ok
						 << "processedPairs=" << processedPairs
						 << "totalPairs=" << totalPairs
						 << "outputPath=" << m_postprocessRequest.outputPath
						 << "problems=" << m_processProblems.size();
		qDebug() << "Sessions tab postprocessing done"
						 << "ok=" << ok
						 << "processedPairs=" << processedPairs
						 << "totalPairs=" << totalPairs
						 << "outputPath=" << m_postprocessRequest.outputPath;
		QApplication::restoreOverrideCursor();
}

QTime ArsTrackerSessionsTab::sessionStartTimeFromSessionName(const QString &sessionName, bool *timestampValid) const
{
		QDateTime dt;
		const bool valid = parse_unix_timestamp_session_name(sessionName, &dt);
		if (timestampValid != nullptr)
		{
				*timestampValid = valid;
		}
		return valid ? dt.toLocalTime().time() : QTime(0, 0, 0);
}

QString ArsTrackerSessionsTab::formatDurationMs(qint64 durationMs) const
{
		if (durationMs < 0)
		{
				return "--:--:--";
		}
		const qint64 totalSeconds = durationMs / 1000;
		const qint64 hours = totalSeconds / 3600;
		const qint64 minutes = (totalSeconds % 3600) / 60;
		const qint64 seconds = totalSeconds % 60;
		return QString("%1:%2:%3")
				.arg(hours, 2, 10, QChar('0'))
				.arg(minutes, 2, 10, QChar('0'))
				.arg(seconds, 2, 10, QChar('0'));
}

void ArsTrackerSessionsTab::updateSessionTimeSummary()
{
		if (m_currentSessionFinishKnown && m_currentSessionDurationMs >= 0)
		{
				setSessionTimeSummary(m_currentSessionStartTime.toString("HH:mm:ss"),
															m_currentSessionFinishTime.toString("HH:mm:ss"),
															formatDurationMs(m_currentSessionDurationMs));
				return;
		}
		setSessionTimeSummaryPlaceholder();
}

void ArsTrackerSessionsTab::setSessionTimeSummaryPlaceholder()
{
		setSessionTimeSummary("--:--:--", "--:--:--", "--:--:--");
}

void ArsTrackerSessionsTab::setSessionTimeSummary(const QString &startTime, const QString &finishTime, const QString &duration)
{
		sessionTimeSummaryLabel->setText(QString("Session time: %1 - %2").arg(startTime, finishTime));
		sessionDurationSummaryLabel->setText(QString("Duration: %1").arg(duration));
}

QTime ArsTrackerSessionsTab::roundUpToNextHalfHour(const QTime &time) const
{
		if (!time.isValid())
		{
				return QTime(0, 0, 0);
		}
		const int m = time.minute();
		const int s = time.second();
		if ((m == 0 || m == 30) && s == 0)
		{
				return QTime(time.hour(), m, 0);
		}
		if (m < 30)
		{
				return QTime(time.hour(), 30, 0);
		}
		return time.addSecs((60 - m) * 60 - s);
}

QTime ArsTrackerSessionsTab::roundDownToPreviousHalfHour(const QTime &time) const
{
		if (!time.isValid())
		{
				return QTime(0, 0, 0);
		}
		const int minute = time.minute() < 30 ? 0 : 30;
		return QTime(time.hour(), minute, 0);
}

uint32_t ArsTrackerSessionsTab::maxIntegralTimestamp(const std::vector<IntegralState> &states) const
{
		uint32_t maxTs = 0;
		for (const IntegralState &state : states)
		{
				if (state.timestamp > maxTs)
				{
						maxTs = state.timestamp;
				}
		}
		return maxTs;
}

LocalSessionInfo *ArsTrackerSessionsTab::findSessionById(const QString &sessionId)
{
		for (LocalSessionInfo &session : m_allSessions)
		{
				if (session.folderName == sessionId)
				{
						return &session;
				}
		}
		return nullptr;
}

const LocalSessionInfo *ArsTrackerSessionsTab::findSessionById(const QString &sessionId) const
{
		for (const LocalSessionInfo &session : m_allSessions)
		{
				if (session.folderName == sessionId)
				{
						return &session;
				}
		}
		return nullptr;
}

bool ArsTrackerSessionsTab::saveSessionTeamId(const QString &sessionPath, int teamId, QString *errorMessage) const
{
		if (teamId <= 0)
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = "Selected team id is invalid.";
				}
				return false;
		}
		if (!m_teamsById.contains(teamId))
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = QString("Team id %1 does not exist.").arg(teamId);
				}
				return false;
		}
		return ArsSessionInfoJson::saveSessionTeamId(sessionPath, teamId, errorMessage);
}

void ArsTrackerSessionsTab::refreshSessionDetailsTeamUi()
{
		if (sessionTeamCombo == nullptr || saveSessionTeamButton == nullptr || sessionTeamStatusLabel == nullptr)
		{
				return;
		}
		const LocalSessionInfo *session = findSessionById(currentSessionId);
		const QSignalBlocker blocker(sessionTeamCombo);
		sessionTeamCombo->clear();

		const bool hasDefaultTeam = m_defaultTeamId > 0 && m_teamsById.contains(m_defaultTeamId);
		if (session == nullptr)
		{
				sessionTeamCombo->addItem("Not configured", -1);
				saveSessionTeamButton->setEnabled(false);
				sessionTeamStatusLabel->setText("Session is not available.");
				return;
		}

		if (!session->hasExplicitTeamId)
		{
				if (hasDefaultTeam)
				{
						sessionTeamCombo->addItem(QString("Default: %1").arg(teamDisplayName(m_teamsById.value(m_defaultTeamId))), m_defaultTeamId);
				}
				else
				{
						sessionTeamCombo->addItem("Not configured", -1);
				}
		}
		else if (session->hasExplicitTeamId && !m_teamsById.contains(session->explicitTeamId))
		{
				sessionTeamCombo->addItem(QString("Missing team id %1").arg(session->explicitTeamId), session->explicitTeamId);
		}

		for (const ArsTeam &team : m_teams)
		{
				sessionTeamCombo->addItem(teamDisplayName(team), team.teamId);
		}

		int selectedId = -1;
		if (session->hasExplicitTeamId)
		{
				selectedId = session->explicitTeamId;
		}
		else if (hasDefaultTeam)
		{
				selectedId = m_defaultTeamId;
		}
		int index = sessionTeamCombo->findData(selectedId);
		if (index < 0)
		{
				index = 0;
		}
		sessionTeamCombo->setCurrentIndex(index);

		if (!session->hasExplicitTeamId)
		{
				sessionTeamStatusLabel->setText("Using default team. TeamId is not saved for this session.");
		}
		else if (session->hasExplicitTeamId && !m_teamsById.contains(session->explicitTeamId))
		{
				sessionTeamStatusLabel->setText("Configured TeamId was not found. Select another team and save.");
		}
		else
		{
				sessionTeamStatusLabel->setText("TeamId is configured for this session.");
		}
		const int selectedTeamId = sessionTeamCombo->currentData().isValid() ? sessionTeamCombo->currentData().toInt() : -1;
		saveSessionTeamButton->setEnabled(selectedTeamId > 0 && m_teamsById.contains(selectedTeamId));
		qDebug() << "Session detail team resolved session=" << currentSessionId
						 << "hasExplicitTeamId=" << session->hasExplicitTeamId
						 << "explicitTeamId=" << session->explicitTeamId
						 << "defaultTeamId=" << m_defaultTeamId
						 << "display=" << session->teamDisplayText;
}

void ArsTrackerSessionsTab::onTeamFilterChanged()
{
		if (teamFilterCombo == nullptr)
		{
				return;
		}
		m_selectedTeamFilterData = teamFilterCombo->currentData().isValid() ? teamFilterCombo->currentData().toInt() : -999;
		qDebug() << "Sessions team filter changed mode="
						 << (m_selectedTeamFilterData == -999 ? "All" : (m_selectedTeamFilterData == -1 ? "NotConfigured" : "SpecificTeam"))
						 << "selectedTeamId=" << m_selectedTeamFilterData;
		applySessionsFilterAndRefreshTable();
}

void ArsTrackerSessionsTab::onSaveSessionTeamClicked()
{
		if (currentSessionId.trimmed().isEmpty())
		{
				return;
		}
		const int teamId = (sessionTeamCombo != nullptr && sessionTeamCombo->currentData().isValid()) ? sessionTeamCombo->currentData().toInt() : -1;
		const QString sessionPath = QDir(sessionsPath()).filePath(currentSessionId);
		qDebug() << "Session detail save team begin session=" << currentSessionId << "teamId=" << teamId;
		QString error;
		if (!saveSessionTeamId(sessionPath, teamId, &error))
		{
				qWarning() << "Session detail save team failed session=" << currentSessionId << "error=" << error;
				qWarning() << "SessionInfo team write failed session=" << currentSessionId << "error=" << error;
				QMessageBox::warning(this, "Sessions", QString("Failed to save TeamId: %1").arg(error));
				return;
		}
		qDebug() << "Session detail save team done session=" << currentSessionId << "teamId=" << teamId
						 << "path=" << QDir(sessionPath).filePath("SessionInfo.json");
		reloadSessions("session-team-saved");
		refreshSessionDetailsTeamUi();
}

void ArsTrackerSessionsTab::onSessionNameClicked()
{
		QPushButton *btn = qobject_cast<QPushButton *>(sender());
		if (btn == nullptr)
		{
				return;
		}
		const QString sessionId = btn->property("sessionId").toString();
		if (!sessionId.trimmed().isEmpty())
		{
				showSessionDetailsPage(sessionId);
		}
}

void ArsTrackerSessionsTab::onBackFromSessionDetails()
{
		showSessionsListPage(true);
}

void ArsTrackerSessionsTab::resizeEvent(QResizeEvent *event)
{
		QWidget::resizeEvent(event);
		if (pagesStack != nullptr && pagesStack->currentWidget() == listPage)
		{
				scheduleSessionsListColumnResize();
		}
}

void ArsTrackerSessionsTab::scheduleSessionsListColumnResize()
{
		QTimer::singleShot(0, this, &ArsTrackerSessionsTab::resizeSessionsListColumnsToContent);
		QTimer::singleShot(50, this, &ArsTrackerSessionsTab::resizeSessionsListColumnsToContent);
}

void ArsTrackerSessionsTab::resizeSessionsListColumnsToContent()
{
		if (sessionsTable == nullptr || pagesStack == nullptr || pagesStack->currentWidget() != listPage)
		{
				return;
		}

		const int sessionColumn = 0;
		const int viewportWidth = sessionsTable->viewport() != nullptr ? sessionsTable->viewport()->width() : sessionsTable->width();
		if (viewportWidth <= 0)
		{
				qDebug() << "Sessions tab session name column resize deferred: viewport not ready";
				return;
		}

		int contentWidth = 0;
		for (int row = 0; row < sessionsTable->rowCount(); ++row)
		{
				if (QWidget *w = sessionsTable->cellWidget(row, sessionColumn))
				{
						contentWidth = std::max(contentWidth, w->sizeHint().width() + 16);
				}
		}
		sessionsTable->resizeColumnToContents(sessionColumn);
		contentWidth = std::max(contentWidth, sessionsTable->columnWidth(sessionColumn));

		const int maxWidth = std::max(120, viewportWidth / 2);
		const int finalWidth = std::max(120, std::min(contentWidth, maxWidth));
		sessionsTable->setColumnWidth(sessionColumn, finalWidth);
		qDebug() << "Sessions tab session name column resized"
						 << "viewportWidth=" << viewportWidth
						 << "contentWidth=" << contentWidth
						 << "maxWidth=" << maxWidth
						 << "finalWidth=" << finalWidth;
}
