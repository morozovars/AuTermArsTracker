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
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimeEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

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
		actions->addWidget(openFolderButton);
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
		sessionsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
		sessionsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		layout->addWidget(sessionsTable, 1, 0, 1, 1);

		statusLabel = new QLabel("No local sessions found", listPage);
		layout->addWidget(statusLabel, 2, 0, 1, 1);

		connect(openFolderButton, &QPushButton::clicked, this, &ArsTrackerSessionsTab::openSessionsFolder);
		connect(reloadButton, &QPushButton::clicked, this, [this]() { reloadSessions("reload"); });

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
		processButton = new QPushButton("Process", detailsPage);
		sessionTitleLabel = new QLabel(detailsPage);
		header->addWidget(backButton);
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
		connect(processButton, &QPushButton::clicked, this, &ArsTrackerSessionsTab::onProcessSessionClicked);

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
				sessionsTable->setItem(row, 1, new QTableWidgetItem(session.trackersDisplayText.isEmpty() ? "-" : session.trackersDisplayText));
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
		scheduleSessionsListColumnResize();
		statusLabel->setText(sessions.isEmpty() ? "No local sessions found" : QString("Local sessions: %1").arg(sessions.size()));
		qDebug() << "Sessions tab list loaded" << "reason=" << reason << "path=" << sessions_path << "count=" << sessions.size();
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
		m_currentSessionStartTime = sessionStartTimeFromSessionName(sessionId, &m_currentSessionStartTimestampValid);
		m_currentSessionFinishKnown = false;
		m_currentSessionDurationMs = -1;
		m_currentSessionDurationCalculating = true;
		updateSessionTimeSummary();
		resetSessionInformationFieldsToDefaults();
		qDebug() << "Sessions tab session info reset defaults"
						 << "session=" << sessionId;
		QStringList loadWarnings;
		loadSessionInfoJsonIntoUi(sessionPath, &loadWarnings);
		for (const QString &w : loadWarnings)
		{
				qWarning() << "Sessions tab SessionInfo.json load warning" << w;
		}
		qDebug() << "Sessions tab session time initial"
						 << "sessionName=" << sessionId
						 << "startTime=" << m_currentSessionStartTime.toString("HH:mm:ss")
						 << "timestampValid=" << m_currentSessionStartTimestampValid;

		fillSessionTrackersTable(scanSessionTrackers(sessionPath));
		detailsStatusLabel->setText("Ready to process");
		pagesStack->setCurrentWidget(detailsPage);
		startInitialSessionDurationScan(sessionPath, sessionId);
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

bool ArsTrackerSessionsTab::loadSessionInfoJsonIntoUi(const QString &sessionPath, QStringList *warnings)
{
		const QString filePath = QDir(sessionPath).filePath("SessionInfo.json");
		qDebug() << "Sessions tab SessionInfo.json load" << "path=" << filePath;
		if (!QFileInfo::exists(filePath))
		{
				qDebug() << "Sessions tab SessionInfo.json missing" << "path=" << filePath;
				return false;
		}

		ArsSessionInfo info;
		QString error;
		if (!ArsSessionInfoJson::loadSessionInfoJson(sessionPath, &info, &error))
		{
				qWarning() << "Sessions tab SessionInfo.json load failed" << "path=" << filePath << "error=" << error;
				if (warnings != nullptr)
				{
						warnings->append(error);
				}
				return false;
		}
		applySessionInfoToUi(info);
		qDebug() << "Sessions tab SessionInfo.json loaded" << "ok=true";
		return true;
}

bool ArsTrackerSessionsTab::saveSessionInfoJson(const QString &sessionPath, const ArsSessionInfo &info, QString *errorMessage) const
{
		return ArsSessionInfoJson::saveSessionInfoJson(sessionPath, info, errorMessage);
}

void ArsTrackerSessionsTab::startInitialSessionDurationScan(const QString &sessionPath, const QString &sessionId)
{
		qDebug() << "Sessions tab session duration scan begin" << "path=" << sessionPath;
		QTimer::singleShot(0, this, [this, sessionPath, sessionId]() {
				scanAndUpdateSessionTime(sessionPath, sessionId);
		});
}

void ArsTrackerSessionsTab::scanAndUpdateSessionTime(const QString &sessionPath, const QString &sessionId)
{
		if (currentSessionId != sessionId)
		{
				return;
		}
		uint32_t maxTimestamp100ms = 0;
		bool hasTimestamp = false;
		QStringList warnings;
		// TODO: move initial session duration scan to worker thread if opening large sessions becomes slow.
		ArsSessionDurationScanner::scanSessionDuration(sessionPath, &maxTimestamp100ms, &hasTimestamp, &warnings);
		for (const QString &w : warnings)
		{
				qWarning() << "Sessions tab session duration scan warning" << w;
		}
		updateSessionTimeSummaryFromMaxTimestamp(maxTimestamp100ms, hasTimestamp);
}

void ArsTrackerSessionsTab::updateSessionTimeSummaryFromMaxTimestamp(uint32_t maxTimestamp100ms, bool hasTimestamp)
{
		m_currentSessionDurationCalculating = false;
		if (hasTimestamp)
		{
				m_currentSessionDurationMs = static_cast<qint64>(maxTimestamp100ms) * 100;
				const QDateTime finish = QDateTime(QDate(2000, 1, 1), m_currentSessionStartTime).addMSecs(m_currentSessionDurationMs);
				m_currentSessionFinishTime = finish.time();
				m_currentSessionFinishKnown = true;
				qDebug() << "Sessions tab session duration scan done"
								 << "hasTimestamp=" << hasTimestamp
								 << "maxTimestamp100ms=" << maxTimestamp100ms
								 << "durationMs=" << m_currentSessionDurationMs
								 << "finishTime=" << m_currentSessionFinishTime.toString("HH:mm:ss")
								 << "duration=" << formatDurationMs(m_currentSessionDurationMs);
		}
		else
		{
				m_currentSessionFinishKnown = false;
				m_currentSessionDurationMs = -1;
				qWarning() << "Sessions tab session duration scan done"
									 << "hasTimestamp=false"
									 << "maxTimestamp100ms=0"
									 << "durationMs=-1"
									 << "finishTime=--:--:--"
									 << "duration=--:--:--";
		}
		updateSessionTimeSummary();
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
		m_processProblems.clear();
		m_processPairInputs.clear();
		m_processPairIndex = 0;
		m_processMaxIntegralTimestamp = 0;
		m_processHasIntegralTimestamp = false;

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
}

void ArsTrackerSessionsTab::startSessionProcessingFlow()
{
		QApplication::setOverrideCursor(Qt::WaitCursor);
		const QString sessionPath = QDir(sessionsPath()).filePath(currentSessionId);
		qDebug() << "Sessions tab process begin" << "sessionPath=" << sessionPath;

		const ArsSessionInfo info = readSessionInfoFromUi();
		if (!validateSessionInfo(info, &m_processProblems))
		{
				qWarning() << "Sessions tab process validation failed";
		}
		else
		{
				QString saveError;
				if (!saveSessionInfoJson(sessionPath, info, &saveError))
				{
						m_processProblems.append(QString("SessionInfo.json save failed: %1").arg(saveError));
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
		m_processStatusLabel->setText(QString("Processing pair %1 of %2: %3").arg(index).arg(total).arg(pairInput.pairSerial));
		qDebug() << "Sessions tab process pair begin" << "index=" << index << "total=" << total << "serial=" << pairInput.pairSerial;

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
				qDebug() << "Sessions tab process duration update"
								 << "pair=" << pairInput.pairSerial
								 << "maxTimestamp100ms=" << m_processMaxIntegralTimestamp
								 << "durationMs=" << (static_cast<qint64>(m_processMaxIntegralTimestamp) * 100);
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
		const int totalPairs = m_processPairInputs.size();
		const int processedPairs = m_processPairIndex;
		if (m_processHasIntegralTimestamp)
		{
				updateSessionTimeSummaryFromMaxTimestamp(m_processMaxIntegralTimestamp, true);
				qDebug() << "Sessions tab process duration final"
								 << "maxTimestamp100ms=" << m_processMaxIntegralTimestamp
								 << "durationMs=" << m_currentSessionDurationMs
								 << "finishTime=" << m_currentSessionFinishTime.toString("HH:mm:ss")
								 << "duration=" << formatDurationMs(m_currentSessionDurationMs);
		}
		else
		{
				updateSessionTimeSummaryFromMaxTimestamp(0, false);
				m_processProblems.append("Unable to calculate session duration: no integral state timestamps found.");
				qWarning() << "Sessions tab process duration unavailable: no integral states";
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
				m_processStatusLabel->setText(QString("Processing completed successfully.\nPairs processed: %1/%2").arg(processedPairs).arg(totalPairs));
				detailsStatusLabel->setText(QString("Processing completed successfully. Pairs=%1").arg(totalPairs));
		}
		else
		{
				m_processStatusLabel->setText(QString("Processing completed with errors.\nPairs processed: %1/%2").arg(processedPairs).arg(totalPairs));
				QString text = QString("Pairs processed: %1/%2\nProblems:\n").arg(processedPairs).arg(totalPairs);
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
						 << "problems=" << m_processProblems.size();
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
		const QString start = m_currentSessionStartTime.toString("HH:mm:ss");
		const QString finish = m_currentSessionDurationCalculating
																 ? "calculating..."
																 : (m_currentSessionFinishKnown ? m_currentSessionFinishTime.toString("HH:mm:ss") : "--:--:--");
		const QString duration = m_currentSessionDurationCalculating
																	 ? "calculating..."
																	 : (m_currentSessionDurationMs >= 0 ? formatDurationMs(m_currentSessionDurationMs) : "--:--:--");
		sessionTimeSummaryLabel->setText(QString("Session time: %1 - %2").arg(start, finish));
		sessionDurationSummaryLabel->setText(QString("Duration: %1").arg(duration));
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
