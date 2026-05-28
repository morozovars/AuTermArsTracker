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
#include <QPixmap>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QFileInfo>
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
#include "ars/workspace/ArsPlayerRepository.h"
#include "ars/workspace/ArsSessionPlayerBindingResolver.h"
#include "ars_tracker/ars_session_duration_scanner.h"
#include "ars_tracker/ars_session_info_json.h"
#include "ars_tracker/ars_session_assignment_dialog.h"
#include "ars_tracker/ars_report_session_data_builder.h"
#include "../../src/ars_tracker_reports/ars_reporter/src/SessionDataWriter.h"
#include "../../src/ars_tracker_reports/ars_reporter/src/DocxReportGenerator.h"

namespace
{
constexpr int kMaxMalformedLinesToLog = 10;
const QTime kDefaultPlannedStart(10, 0, 0);
const QTime kDefaultPlannedFinish(11, 30, 0);
constexpr int kSessionsColumnSession = 0;
constexpr int kSessionsColumnTeam = 1;
constexpr int kSessionsColumnTrackers = 2;
constexpr int kSessionsColumnActions = 3;

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

QString trim_leading_zeros_for_display(QString value)
{
		value = value.trimmed();
		if (value.isEmpty())
		{
				return QString("-");
		}
		while (value.size() > 1 && value.startsWith('0'))
		{
				value.remove(0, 1);
		}
		return value.isEmpty() ? QString("0") : value;
}

QString display_pair_name(const QString &pairId)
{
		const QString raw = pairId.trimmed();
		if (raw.isEmpty())
		{
				return QString("-");
		}
		return trim_leading_zeros_for_display(raw);
}

QString display_pair_sides(const QString &leftTrackerSerial, const QString &rightTrackerSerial)
{
		QStringList sides;
		if (!leftTrackerSerial.trimmed().isEmpty() && leftTrackerSerial != "-")
		{
				sides.append("L");
		}
		if (!rightTrackerSerial.trimmed().isEmpty() && rightTrackerSerial != "-")
		{
				sides.append("R");
		}
		if (sides.isEmpty())
		{
				return QString("-");
		}
		return sides.join(" + ");
}

QString compact_pair_display(const QString &pairId, const QString &leftTrackerSerial, const QString &rightTrackerSerial)
{
		return QString("%1: %2").arg(display_pair_name(pairId), display_pair_sides(leftTrackerSerial, rightTrackerSerial));
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
		sessionsTable->setColumnCount(4);
		sessionsTable->setHorizontalHeaderLabels(QStringList() << "Session" << "Team" << "Players-trackers" << "Actions");
		sessionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
		sessionsTable->setSelectionMode(QAbstractItemView::SingleSelection);
		sessionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
		sessionsTable->setWordWrap(true);
		sessionsTable->setTextElideMode(Qt::ElideNone);
		sessionsTable->verticalHeader()->setVisible(false);
		sessionsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
		sessionsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
		sessionsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
		sessionsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
		sessionsTable->setColumnWidth(1, 220);
		layout->addWidget(sessionsTable, 1, 0, 1, 1);

		statusLabel = new QLabel("No local sessions found", listPage);
		layout->addWidget(statusLabel, 2, 0, 1, 1);

		connect(openFolderButton, &QPushButton::clicked, this, &ArsTrackerSessionsTab::openSessionsFolder);
		connect(reloadButton, &QPushButton::clicked, this, [this]() { reloadSessions("reload"); });
		connect(teamFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ArsTrackerSessionsTab::onTeamFilterChanged);
		connect(sessionsTable, &QTableWidget::cellClicked, this, &ArsTrackerSessionsTab::onSessionTableCellClicked);

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
		generatePdfButton = new QPushButton("Generate pdf", detailsPage);
		openReportButton = new QPushButton("Open report", detailsPage);
		openReportButton->setEnabled(false);
		generatePdfButton->setObjectName("ars_tracker_generate_pdf_button");
		generatePdfButton->setToolTip("Generate PDF report for this session");
		sessionTitleLabel = new QLabel(detailsPage);
		header->addWidget(backButton);
		header->addWidget(rescanButton);
		header->addWidget(processButton);
		header->addWidget(generatePdfButton);
		header->addWidget(openReportButton);
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

		spinTargetMaxSpeedMps = new QDoubleSpinBox(targetBox);
		spinTargetMaxSpeedMps->setObjectName("spin_session_target_max_speed_mps");
		spinTargetMaxSpeedMps->setDecimals(2);
		spinTargetMaxSpeedMps->setRange(0.0, 100.0);
		targetLayout->addRow("Max speed m/s", spinTargetMaxSpeedMps);

		spinTargetShotsCount = new QSpinBox(targetBox);
		spinTargetShotsCount->setObjectName("spin_session_target_shots_count");
		spinTargetShotsCount->setRange(0, 100000);
		targetLayout->addRow("Shots count", spinTargetShotsCount);

		spinTargetPossessions = new QSpinBox(targetBox);
		spinTargetPossessions->setObjectName("spin_session_target_possessions");
		spinTargetPossessions->setRange(0, 100000);
		targetLayout->addRow("Possessions", spinTargetPossessions);
		sessionInfoColumns->addWidget(targetBox, 1);
		layout->addWidget(sessionInfoBox, 1, 0, 1, 1);

		QGroupBox *assignmentsBox = new QGroupBox("Tracker/player assignments", detailsPage);
		QVBoxLayout *assignmentsLayout = new QVBoxLayout(assignmentsBox);
		sessionAssignmentsTable = new QTableWidget(assignmentsBox);
		sessionAssignmentsTable->setColumnCount(6);
		sessionAssignmentsTable->setHorizontalHeaderLabels(QStringList() << "Player photo" << "Player name" << "Pair" << "Left tracker" << "Right tracker" << "Actions");
		sessionAssignmentsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
		sessionAssignmentsTable->setSelectionMode(QAbstractItemView::SingleSelection);
		sessionAssignmentsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
		sessionAssignmentsTable->verticalHeader()->setVisible(false);
		sessionAssignmentsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
		sessionAssignmentsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		sessionAssignmentsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
		sessionAssignmentsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
		sessionAssignmentsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
		sessionAssignmentsTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
		sessionAssignmentsTable->setColumnWidth(0, 72);
		sessionAssignmentsTable->verticalHeader()->setDefaultSectionSize(110);
		assignmentsLayout->addWidget(sessionAssignmentsTable);
		sessionAssignmentsEmptyLabel = new QLabel("No tracker pairs found", assignmentsBox);
		sessionAssignmentsEmptyLabel->setVisible(false);
		assignmentsLayout->addWidget(sessionAssignmentsEmptyLabel);
		layout->addWidget(assignmentsBox, 2, 0, 1, 1);

		detailsStatusLabel = new QLabel(detailsPage);
		layout->addWidget(detailsStatusLabel, 3, 0, 1, 1);

		connect(backButton, &QPushButton::clicked, this, &ArsTrackerSessionsTab::onBackFromSessionDetails);
		connect(rescanButton, &QPushButton::clicked, this, &ArsTrackerSessionsTab::onRescanSessionClicked);
		connect(processButton, &QPushButton::clicked, this, &ArsTrackerSessionsTab::onProcessSessionClicked);
		connect(generatePdfButton, &QPushButton::clicked, this, &ArsTrackerSessionsTab::onGeneratePdfClicked);
		connect(openReportButton, &QPushButton::clicked, this, &ArsTrackerSessionsTab::onOpenReportClicked);
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
		QDir trackersRoot(sessionPath);
		if (!trackersRoot.exists())
		{
				qWarning() << "Sessions tab: session path is not found" << sessionPath;
				return out;
		}
		const QString rawPath = QDir(sessionPath).filePath("raw");
		if (QDir(rawPath).exists())
		{
				trackersRoot = QDir(rawPath);
		}
		const QFileInfoList entries = trackersRoot.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
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

QString ArsTrackerSessionsTab::buildTrackersDisplayTextForSession(const QString &sessionPath,
																																	const QList<SessionTrackerPair> &pairs,
																																	const QList<ArsPlayer> &players) const
{
		struct SessionPairDisplayItem
		{
				QString pairId;
				QString playerDisplay;
				QString text;
				bool hasAssignedPlayer = false;
		};

		QList<ArsSessionPairAssignment> assignments;
		ArsSessionInfoJson::readSessionPairAssignments(sessionPath, &assignments, nullptr);

		auto findAssignmentByPair = [&assignments](const QString &pairId) -> const ArsSessionPairAssignment * {
				for (const ArsSessionPairAssignment &assignment : assignments)
				{
						if (assignment.pairId.compare(pairId, Qt::CaseInsensitive) == 0) return &assignment;
				}
				return nullptr;
		};
		auto findPlayerById = [&players](const QString &playerId) -> const ArsPlayer * {
				for (const ArsPlayer &player : players)
				{
						if (player.playerId.compare(playerId, Qt::CaseInsensitive) == 0) return &player;
				}
				return nullptr;
		};

		QList<SessionPairDisplayItem> items;
		for (const SessionTrackerPair &pair : pairs)
		{
				const QString pairText = compact_pair_display(pair.pairSerial, pair.leftTracker, pair.rightTracker);

				QString playerDisplay = "not assigned";
				bool hasAssignedPlayer = false;
				const ArsSessionPairAssignment *assignment = findAssignmentByPair(pair.pairSerial);
				if (assignment != nullptr)
				{
						if (!assignment->playerId.trimmed().isEmpty())
						{
								hasAssignedPlayer = true;
								const ArsPlayer *player = findPlayerById(assignment->playerId);
								if (player != nullptr)
								{
										playerDisplay = QString("%1 %2").arg(player->surname, player->name).trimmed();
								}
								else if (!assignment->playerName.trimmed().isEmpty())
								{
										playerDisplay = QString("%1 (missing)").arg(assignment->playerName);
								}
								else
								{
										playerDisplay = QString("missing player %1").arg(assignment->playerId);
								}
						}
						else if (!assignment->playerName.trimmed().isEmpty())
						{
								hasAssignedPlayer = true;
								playerDisplay = assignment->playerName.trimmed();
						}
				}

				SessionPairDisplayItem item;
				item.pairId = pair.pairSerial;
				item.playerDisplay = playerDisplay;
				item.hasAssignedPlayer = hasAssignedPlayer;
				item.text = QString("%1 (%2)").arg(playerDisplay, pairText);
				items.append(item);
				qDebug() << "Sessions players-trackers compact display session=" << QFileInfo(sessionPath).fileName()
								 << "pairId=" << pair.pairSerial
								 << "pairDisplay=" << pairText
								 << "left=" << pair.leftTracker
								 << "right=" << pair.rightTracker;
				qDebug() << "Sessions list tracker display row session=" << QFileInfo(sessionPath).fileName()
								 << "pairId=" << pair.pairSerial
								 << "playerDisplay=" << playerDisplay
								 << "left=" << pair.leftTracker
								 << "right=" << pair.rightTracker;
		}

		std::sort(items.begin(), items.end(), [](const SessionPairDisplayItem &a, const SessionPairDisplayItem &b) {
				if (a.hasAssignedPlayer != b.hasAssignedPlayer)
				{
						return a.hasAssignedPlayer && !b.hasAssignedPlayer;
				}
				if (a.hasAssignedPlayer && b.hasAssignedPlayer)
				{
						const int nameCmp = QString::localeAwareCompare(a.playerDisplay, b.playerDisplay);
						if (nameCmp != 0)
						{
								return nameCmp < 0;
						}
				}
				return QString::compare(a.pairId, b.pairId, Qt::CaseInsensitive) < 0;
		});

		int assignedCount = 0;
		int unassignedCount = 0;
		QStringList parts;
		for (const SessionPairDisplayItem &item : items)
		{
				parts.append(item.text);
				if (item.hasAssignedPlayer)
				{
						++assignedCount;
				}
				else
				{
						++unassignedCount;
				}
		}
		const QString inlineText = parts.join(", ");
		qDebug() << "Sessions players-trackers display sorted session=" << QFileInfo(sessionPath).fileName()
						 << "assignedCount=" << assignedCount
						 << "unassignedCount=" << unassignedCount;
		qDebug() << "Sessions players-trackers display wrapped session=" << QFileInfo(sessionPath).fileName()
						 << "assignedCount=" << assignedCount
						 << "unassignedCount=" << unassignedCount
						 << "length=" << inlineText.size();
		return inlineText;
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
		ArsLocalWorkspace workspace;
		const QString workspaceRootPath = workspace.initialize() ? workspace.rootPath() : QString();
		ArsPlayerRepository playersRepository(workspaceRootPath);
		const QList<ArsPlayer> allPlayers = playersRepository.loadPlayers(nullptr);
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
				const QList<SessionTrackerPair> pairs = scanSessionTrackers(local.absolutePath);
				if (pairs.isEmpty())
				{
						local.trackersDisplayText = "no trackers";
				}
				else
				{
						local.trackersDisplayText = buildTrackersDisplayTextForSession(local.absolutePath, pairs, allPlayers);
				}
				qDebug() << "Sessions list trackers display session=" << local.folderName << "pairCount=" << pairs.size();
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
				QTableWidgetItem *sessionItem = new QTableWidgetItem(session.displayName);
				sessionItem->setData(Qt::UserRole, session.folderName);
				sessionItem->setData(Qt::UserRole + 1, session.absolutePath);
				sessionItem->setToolTip(QString("Open session\n%1").arg(session.absolutePath));
				sessionsTable->setItem(row, kSessionsColumnSession, sessionItem);
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
				teamItem->setData(Qt::UserRole, session.folderName);
				teamItem->setData(Qt::UserRole + 1, session.absolutePath);
				sessionsTable->setItem(row, kSessionsColumnTeam, teamItem);
				QString displayText = session.trackersDisplayText.isEmpty() ? "-" : session.trackersDisplayText;
				displayText.replace(QStringLiteral("\r\n"), QStringLiteral(", "));
				displayText.replace(QChar('\n'), QStringLiteral(", "));
				displayText.replace(QChar('\r'), QStringLiteral(", "));
				displayText.replace(QRegularExpression(QStringLiteral("\\s*,\\s*")), QStringLiteral(", "));
				displayText = displayText.simplified();
				if (displayText.isEmpty())
				{
						displayText = "-";
				}
				qDebug() << "Players-trackers contains newline:" << displayText.contains('\n');
				QTableWidgetItem *trackersItem = new QTableWidgetItem(displayText);
				trackersItem->setToolTip(displayText);
				trackersItem->setData(Qt::UserRole, session.folderName);
				trackersItem->setData(Qt::UserRole + 1, session.absolutePath);
				sessionsTable->setItem(row, kSessionsColumnTrackers, trackersItem);
				QPushButton *deleteButton = new QPushButton("Delete", sessionsTable);
				deleteButton->setToolTip("Delete session from disk");
				deleteButton->setProperty("sessionName", session.folderName);
				deleteButton->setProperty("sessionPath", session.absolutePath);
				connect(deleteButton, &QPushButton::clicked, this, &ArsTrackerSessionsTab::onDeleteSessionClicked);
				sessionsTable->setCellWidget(row, kSessionsColumnActions, deleteButton);
				qDebug() << "Sessions session team display session=" << session.folderName << "display=" << session.teamDisplayText;
		}
		sessionsTable->resizeRowsToContents();
}

QList<ArsSessionTrackerPair> ArsTrackerSessionsTab::detectedSessionPairs(const QString &sessionPath) const
{
		QList<ArsSessionTrackerPair> out;
		const QList<SessionTrackerPair> pairs = scanSessionTrackers(sessionPath);
		for (const SessionTrackerPair &pair : pairs)
		{
				ArsSessionTrackerPair value;
				value.pairId = pair.pairSerial;
				value.leftTrackerSerial = (pair.leftTracker == "-" ? QString() : pair.leftTracker);
				value.rightTrackerSerial = (pair.rightTracker == "-" ? QString() : pair.rightTracker);
				out.append(value);
		}
		return out;
}

int ArsTrackerSessionsTab::effectiveSessionTeamIdForPath(const QString &sessionPath) const
{
		bool hasTeamId = false;
		int teamId = -1;
		QString error;
		if (!ArsSessionInfoJson::loadSessionTeamId(sessionPath, &hasTeamId, &teamId, &error))
		{
				qWarning() << "Session team read failed path=" << sessionPath << "error=" << error;
		}
		if (hasTeamId && teamId > 0)
		{
				return teamId;
		}
		if (m_defaultTeamId > 0)
		{
				return m_defaultTeamId;
		}
		return -1;
}

bool ArsTrackerSessionsTab::resolveAndPersistSessionAssignments(const QString &sessionPath,
																														int sessionTeamId,
																														const QList<ArsSessionTrackerPair> &pairs)
{
		QList<ArsSessionPairAssignment> existingAssignments;
		QString readError;
		if (!ArsSessionInfoJson::readSessionPairAssignments(sessionPath, &existingAssignments, &readError))
		{
				qWarning() << "Session assignments read failed session=" << sessionPath << "error=" << readError;
				return false;
		}

		ArsLocalWorkspace workspace;
		const QString workspaceRootPath = workspace.initialize() ? workspace.rootPath() : QString();
		ArsSessionPlayerBindingResolver resolver(workspaceRootPath);
		QStringList warnings;
		const QList<ArsSessionPairAssignment> resolved = resolver.resolveAssignments(sessionPath,
																																								 sessionTeamId,
																																								 pairs,
																																								 existingAssignments,
																																								 &warnings);
		for (const QString &warning : warnings)
		{
				qWarning().noquote() << warning;
		}
		QString writeError;
		if (!ArsSessionInfoJson::writeSessionPairAssignments(sessionPath, resolved, &writeError))
		{
				qWarning() << "Session assignments write failed session=" << sessionPath << "error=" << writeError;
				return false;
		}
		m_currentAssignments = resolved;
		qDebug() << "Session assignments saved session=" << sessionPath << "count=" << resolved.size();
		return true;
}

void ArsTrackerSessionsTab::rebuildCurrentPairRows()
{
		m_currentPairRows.clear();
		ArsLocalWorkspace workspace;
		const QString workspaceRootPath = workspace.initialize() ? workspace.rootPath() : QString();
		ArsPlayerRepository playerRepository(workspaceRootPath);
		const QList<ArsPlayer> players = playerRepository.loadPlayers(nullptr);

		auto findAssignment = [this](const QString &pairId) -> const ArsSessionPairAssignment * {
				for (const ArsSessionPairAssignment &assignment : m_currentAssignments)
				{
						if (assignment.pairId.compare(pairId, Qt::CaseInsensitive) == 0) return &assignment;
				}
				return nullptr;
		};
		auto findPlayer = [&players](const QString &playerId) -> const ArsPlayer * {
				for (const ArsPlayer &player : players)
				{
						if (player.playerId.compare(playerId, Qt::CaseInsensitive) == 0) return &player;
				}
				return nullptr;
		};

		qDebug() << "Session detail unified tracker/player table reload begin session=" << currentSessionId;
		for (const ArsSessionTrackerPair &pair : m_currentDetectedPairs)
		{
				SessionPairPlayerRow row;
				row.pairId = pair.pairId;
				row.leftTrackerSerial = pair.leftTrackerSerial;
				row.rightTrackerSerial = pair.rightTrackerSerial;

				const ArsSessionPairAssignment *assignment = findAssignment(pair.pairId);
				if (assignment == nullptr || assignment->playerId.trimmed().isEmpty())
				{
						row.hasPlayer = false;
						row.playerName = "not assigned";
				}
				else
				{
						row.playerId = assignment->playerId;
						row.assignmentSource = assignment->source;
						row.isOverride = assignment->isOverride;
						row.hasPlayer = true;
						const ArsPlayer *player = findPlayer(assignment->playerId);
						if (player != nullptr)
						{
								row.playerName = QString("%1 %2").arg(player->surname, player->name).trimmed();
								row.playerPhotoPath = player->photoPath;
								row.playerMissing = false;
						}
						else
						{
								row.playerMissing = true;
								if (!assignment->playerName.trimmed().isEmpty())
								{
										row.playerName = QString("%1 (missing)").arg(assignment->playerName);
								}
								else
								{
										row.playerName = QString("missing player %1").arg(assignment->playerId);
								}
						}
				}
				m_currentPairRows.append(row);
				qDebug() << "Session detail unified row pairId=" << row.pairId
								 << "playerId=" << row.playerId
								 << "playerName=" << row.playerName
								 << "left=" << row.leftTrackerSerial
								 << "right=" << row.rightTrackerSerial;
		}

		auto isAssigned = [](const SessionPairPlayerRow &row) {
				return row.hasPlayer || row.playerMissing || !row.playerId.trimmed().isEmpty() ||
							 (!row.playerName.trimmed().isEmpty() && row.playerName.compare("not assigned", Qt::CaseInsensitive) != 0);
		};
		std::sort(m_currentPairRows.begin(), m_currentPairRows.end(), [isAssigned](const SessionPairPlayerRow &a, const SessionPairPlayerRow &b) {
				const bool aAssigned = isAssigned(a);
				const bool bAssigned = isAssigned(b);
				if (aAssigned != bAssigned)
				{
						return aAssigned && !bAssigned;
				}
				if (aAssigned && bAssigned)
				{
						const int cmp = QString::localeAwareCompare(a.playerName, b.playerName);
						if (cmp != 0)
						{
								return cmp < 0;
						}
				}
				return QString::compare(a.pairId, b.pairId, Qt::CaseInsensitive) < 0;
		});

		int assignedCount = 0;
		int unassignedCount = 0;
		for (const SessionPairPlayerRow &row : m_currentPairRows)
		{
				if (isAssigned(row)) ++assignedCount;
				else ++unassignedCount;
		}
		qDebug() << "Session detail assignments sorted assignedCount=" << assignedCount << "unassignedCount=" << unassignedCount;
}

void ArsTrackerSessionsTab::fillSessionAssignmentsTable()
{
		if (sessionAssignmentsTable == nullptr)
		{
				return;
		}
		sessionAssignmentsTable->setSortingEnabled(false);
		sessionAssignmentsTable->clearContents();
		sessionAssignmentsTable->setRowCount(m_currentPairRows.size());
		ArsLocalWorkspace workspace;
		const QString workspaceRootPath = workspace.initialize() ? workspace.rootPath() : QString();
		ArsPlayerRepository playerRepository(workspaceRootPath);
		for (int row = 0; row < m_currentPairRows.size(); ++row)
		{
				const SessionPairPlayerRow &pairRow = m_currentPairRows.at(row);
				const bool assigned = pairRow.hasPlayer || pairRow.playerMissing || !pairRow.playerId.trimmed().isEmpty() ||
															(!pairRow.playerName.trimmed().isEmpty() &&
															 pairRow.playerName.compare("not assigned", Qt::CaseInsensitive) != 0);

				QLabel *photo = new QLabel(sessionAssignmentsTable);
				photo->setAlignment(Qt::AlignCenter);
				if (!pairRow.hasPlayer)
				{
						photo->setText("No photo");
				}
				else if (pairRow.playerPhotoPath.trimmed().isEmpty())
				{
						photo->setText(pairRow.playerMissing ? "No photo" : "No photo");
				}
				else
				{
						const QString absolutePhotoPath = playerRepository.resolvePhotoAbsolutePath(pairRow.playerPhotoPath);
						QPixmap pixmap(absolutePhotoPath);
						if (!QFileInfo::exists(absolutePhotoPath))
						{
								qWarning() << "Session detail player photo missing playerId=" << pairRow.playerId << "path=" << absolutePhotoPath;
								photo->setText("Missing photo");
								photo->setToolTip(absolutePhotoPath);
						}
						else if (pixmap.isNull())
						{
								qWarning() << "Session detail player photo invalid playerId=" << pairRow.playerId << "path=" << absolutePhotoPath;
								photo->setText("Invalid photo");
								photo->setToolTip(absolutePhotoPath);
						}
						else
						{
								photo->setPixmap(pixmap.scaled(60, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation));
						}
				}
				sessionAssignmentsTable->setCellWidget(row, 0, photo);

				QTableWidgetItem *playerItem = new QTableWidgetItem(pairRow.playerName);
				if (!pairRow.assignmentSource.trimmed().isEmpty())
				{
						playerItem->setToolTip(QString("source: %1\noverride: %2")
																		 .arg(pairRow.assignmentSource,
																					pairRow.isOverride ? "true" : "false"));
				}
				sessionAssignmentsTable->setItem(row, 1, playerItem);
				sessionAssignmentsTable->setItem(row, 2, new QTableWidgetItem(pairRow.pairId.trimmed().isEmpty() ? "-" : pairRow.pairId));

				QTableWidgetItem *leftItem = new QTableWidgetItem(pairRow.leftTrackerSerial.trimmed().isEmpty() ? "missing" : pairRow.leftTrackerSerial);
				if (pairRow.leftTrackerSerial.trimmed().isEmpty())
				{
						leftItem->setToolTip("Left tracker data not found in this session");
				}
				sessionAssignmentsTable->setItem(row, 3, leftItem);
				QTableWidgetItem *rightItem = new QTableWidgetItem(pairRow.rightTrackerSerial.trimmed().isEmpty() ? "missing" : pairRow.rightTrackerSerial);
				if (pairRow.rightTrackerSerial.trimmed().isEmpty())
				{
						rightItem->setToolTip("Right tracker data not found in this session");
				}
				sessionAssignmentsTable->setItem(row, 4, rightItem);

				QPushButton *change = new QPushButton(
						(assigned ? "Change" : "Assign"),
						sessionAssignmentsTable);
				change->setProperty("pairId", pairRow.pairId);
				connect(change, &QPushButton::clicked, this, &ArsTrackerSessionsTab::onSessionAssignmentClicked);
				sessionAssignmentsTable->setCellWidget(row, 5, change);
				sessionAssignmentsTable->setRowHeight(row, 110);
				qDebug() << "Session detail assignment row row=" << row << "pairId=" << pairRow.pairId
								 << "playerName=" << pairRow.playerName << "assigned=" << assigned;
		}
		if (sessionAssignmentsEmptyLabel != nullptr)
		{
				sessionAssignmentsEmptyLabel->setVisible(m_currentPairRows.isEmpty());
		}
		qDebug() << "Session detail unified tracker/player table reloaded count=" << m_currentPairRows.size();
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

		m_currentDetectedPairs = detectedSessionPairs(sessionPath);
		const int effectiveTeamId = effectiveSessionTeamIdForPath(sessionPath);
		resolveAndPersistSessionAssignments(sessionPath, effectiveTeamId, m_currentDetectedPairs);
		rebuildCurrentPairRows();
		fillSessionAssignmentsTable();
		refreshSessionDetailsTeamUi();
		updateOpenReportButtonState();
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
		s.targetMaxSpeedMps = spinTargetMaxSpeedMps->value();
		s.targetShotsCount = spinTargetShotsCount->value();
		s.targetPossessions = spinTargetPossessions->value();
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
		info.plannedMetrics.maxSpeedMps = t.targetMaxSpeedMps;
		info.plannedMetrics.shots = t.targetShotsCount;
		info.plannedMetrics.dribbles = t.targetPossessions;
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
		spinTargetMaxSpeedMps->setValue(0.0);
		spinTargetShotsCount->setValue(0);
		spinTargetPossessions->setValue(0);
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
		spinTargetMaxSpeedMps->setValue(info.plannedMetrics.maxSpeedMps);
		spinTargetShotsCount->setValue(info.plannedMetrics.shots);
		spinTargetPossessions->setValue(info.plannedMetrics.dribbles);
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
		m_currentDetectedPairs = detectedSessionPairs(sessionPath);
		const int effectiveTeamId = effectiveSessionTeamIdForPath(sessionPath);
		resolveAndPersistSessionAssignments(sessionPath, effectiveTeamId, m_currentDetectedPairs);
		rebuildCurrentPairRows();
		fillSessionAssignmentsTable();

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

void ArsTrackerSessionsTab::onGeneratePdfClicked()
{
		if (currentSessionId.trimmed().isEmpty())
		{
				if (detailsStatusLabel != nullptr) detailsStatusLabel->setText("Session is not selected");
				return;
		}

		const QString sessionPath = QDir(sessionsPath()).filePath(currentSessionId);
		qDebug() << "Ars report generate PDF clicked session=" << currentSessionId << "path=" << sessionPath;
		if (sessionPath.trimmed().isEmpty() || !QDir(sessionPath).exists())
		{
				if (detailsStatusLabel != nullptr) detailsStatusLabel->setText("Session path is unavailable");
				QMessageBox::warning(this, "Generate PDF", "Session path is unavailable.");
				return;
		}

		const QString outputPdfPath = reportPdfPathForSession(sessionPath);
		const QString reportsDir = QFileInfo(outputPdfPath).absolutePath();
		if (!QDir().mkpath(reportsDir))
		{
				QMessageBox::critical(this, "Generate PDF", QString("Failed to create reports folder:\n%1").arg(reportsDir));
				if (detailsStatusLabel != nullptr) detailsStatusLabel->setText("PDF generation failed: cannot create reports folder");
				return;
		}

		qDebug() << "Ars report generate output=" << outputPdfPath;
		if (QFileInfo::exists(outputPdfPath))
		{
				const QMessageBox::StandardButton answer =
						QMessageBox::question(this,
																	"Generate PDF",
																	QString("Report already exists.\n\n%1\n\nOverwrite?").arg(outputPdfPath),
																	QMessageBox::Yes | QMessageBox::No,
																	QMessageBox::No);
				if (answer != QMessageBox::Yes)
				{
						if (detailsStatusLabel != nullptr) detailsStatusLabel->setText("PDF generation canceled");
						return;
				}
		}

		if (generatePdfButton != nullptr) generatePdfButton->setEnabled(false);
		QApplication::setOverrideCursor(Qt::BusyCursor);
		if (detailsStatusLabel != nullptr) detailsStatusLabel->setText("Generating PDF...");
		QProgressDialog progressDialog("Generating PDF report...", QString(), 0, 0, this);
		progressDialog.setWindowModality(Qt::ApplicationModal);
		progressDialog.setCancelButton(nullptr);
		progressDialog.setMinimumDuration(0);
		progressDialog.setWindowTitle("Generate PDF");
		progressDialog.show();
		QApplication::processEvents();
		const ArsReportSessionDataBuildResult buildResult = buildArsReportSessionDataJson(sessionPath);
		if (!buildResult.ok)
		{
				progressDialog.close();
				QApplication::restoreOverrideCursor();
				if (generatePdfButton != nullptr) generatePdfButton->setEnabled(true);
				if (detailsStatusLabel != nullptr) detailsStatusLabel->setText("PDF generation failed");
				QMessageBox::critical(this, "Generate PDF", buildResult.error);
				return;
		}
		const QString sessionDataPath = buildResult.sessionDataPath;
		qDebug() << "Ars report generate builder"
						 << "sessionPath=" << sessionPath
						 << "sessionInfoPath=" << QDir(sessionPath).filePath("SessionInfo.json")
						 << "sessionDataPath=" << sessionDataPath
						 << "warningsCount=" << buildResult.warnings.size();
		for (const QString &warning : buildResult.warnings)
		{
				qWarning().noquote() << "Ars report builder warning:" << warning;
		}
		qDebug() << "Ars report generate input sessionData=" << sessionDataPath;

		QString errorMessage;
		try
		{
				if (!QFileInfo::exists(sessionDataPath))
				{
						errorMessage = QString("SessionData.json not found:\n%1\n\nRun Process first.").arg(sessionDataPath);
				}
				else
				{
						const SessionData data = readSessionData(sessionDataPath);
						QDir workspaceDir(sessionPath);
						if (workspaceDir.cdUp()) workspaceDir.cdUp();
						const QString assetRoot = workspaceDir.absolutePath();
						generatePdfReport(outputPdfPath, data, assetRoot);
						QFileInfo outInfo(outputPdfPath);
						if (!outInfo.exists() || outInfo.size() <= 0)
						{
								errorMessage = QString("PDF was not created or is empty:\n%1").arg(outputPdfPath);
						}
				}
		}
		catch (const std::exception &e)
		{
				errorMessage = QString::fromUtf8(e.what());
		}
		catch (...)
		{
				errorMessage = "Unknown error during PDF generation.";
		}

		progressDialog.close();
		QApplication::restoreOverrideCursor();
		if (generatePdfButton != nullptr) generatePdfButton->setEnabled(true);

		if (!errorMessage.trimmed().isEmpty())
		{
				if (detailsStatusLabel != nullptr) detailsStatusLabel->setText("PDF generation failed");
				QMessageBox::critical(this, "Generate PDF", errorMessage);
				return;
		}

		if (detailsStatusLabel != nullptr) detailsStatusLabel->setText(QString("PDF generated: %1").arg(outputPdfPath));
		updateOpenReportButtonState();
		QMessageBox successBox(this);
		successBox.setIcon(QMessageBox::Information);
		successBox.setWindowTitle("Generate PDF");
		successBox.setText(QString("PDF report generated successfully:\n%1").arg(outputPdfPath));
		QPushButton *openButton = successBox.addButton("Open", QMessageBox::AcceptRole);
		successBox.addButton("Close", QMessageBox::RejectRole);
		successBox.exec();
		if (successBox.clickedButton() == openButton)
		{
				if (!QDesktopServices::openUrl(QUrl::fromLocalFile(outputPdfPath)))
				{
						QMessageBox::warning(this, "Generate PDF", "Could not open PDF file.");
				}
		}
}

QString ArsTrackerSessionsTab::reportPdfPathForSession(const QString &sessionPath) const
{
		return QDir(QDir(sessionPath).filePath("reports")).filePath(QString("%1_report.pdf").arg(QFileInfo(sessionPath).fileName()));
}

void ArsTrackerSessionsTab::updateOpenReportButtonState()
{
		if (openReportButton == nullptr)
		{
				return;
		}
		if (currentSessionId.trimmed().isEmpty())
		{
				openReportButton->setEnabled(false);
				return;
		}
		const QString sessionPath = QDir(sessionsPath()).filePath(currentSessionId);
		const QString reportPath = reportPdfPathForSession(sessionPath);
		openReportButton->setEnabled(QFileInfo(reportPath).isFile());
}

void ArsTrackerSessionsTab::onOpenReportClicked()
{
		if (currentSessionId.trimmed().isEmpty())
		{
				return;
		}
		const QString sessionPath = QDir(sessionsPath()).filePath(currentSessionId);
		const QString reportPath = reportPdfPathForSession(sessionPath);
		if (!QFileInfo(reportPath).isFile())
		{
				updateOpenReportButtonState();
				QMessageBox::warning(this, "Open report", "PDF report file was not found.");
				return;
		}
		if (!QDesktopServices::openUrl(QUrl::fromLocalFile(reportPath)))
		{
				QMessageBox::warning(this, "Open report", "Could not open PDF report.");
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
		const int leftIntegralRaw = hasLeft ? static_cast<int>(pair.left->data.integralStates.size()) : 0;
		const int rightIntegralRaw = hasRight ? static_cast<int>(pair.right->data.integralStates.size()) : 0;
		const int leftSplashRaw = hasLeft ? static_cast<int>(pair.left->data.splashRecords.size()) : 0;
		const int rightSplashRaw = hasRight ? static_cast<int>(pair.right->data.splashRecords.size()) : 0;
		qDebug().noquote()
				<< QString("ArsPostProcessSplash pair=%1 files leftPath=%2 rightPath=%3")
							 .arg(pairInput.pairSerial, pairInput.leftProcessedStrPath, pairInput.rightProcessedStrPath);
		qDebug().noquote()
				<< QString("ArsPostProcessSplash pair=%1 parsed leftIntegralRaw=%2 rightIntegralRaw=%3 leftSplashRaw=%4 rightSplashRaw=%5 leftMalformed=%6 rightMalformed=%7")
							 .arg(pairInput.pairSerial)
							 .arg(leftIntegralRaw)
							 .arg(rightIntegralRaw)
							 .arg(leftSplashRaw)
							 .arg(rightSplashRaw)
							 .arg(leftMalformed)
							 .arg(rightMalformed);

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
		m_currentDetectedPairs = detectedSessionPairs(sessionPath);
		resolveAndPersistSessionAssignments(sessionPath, teamId, m_currentDetectedPairs);
		rebuildCurrentPairRows();
		fillSessionAssignmentsTable();
		reloadSessions("session-team-saved");
		refreshSessionDetailsTeamUi();
}

void ArsTrackerSessionsTab::onSessionAssignmentClicked()
{
		QPushButton *button = qobject_cast<QPushButton *>(sender());
		if (button == nullptr || currentSessionId.trimmed().isEmpty())
		{
				return;
		}
		const QString pairId = button->property("pairId").toString().trimmed();
		if (pairId.isEmpty())
		{
				return;
		}
		QString action = "Assign";
		for (const SessionPairPlayerRow &row : m_currentPairRows)
		{
				if (row.pairId.compare(pairId, Qt::CaseInsensitive) == 0)
				{
						action = row.hasPlayer ? "Change" : "Assign";
						break;
				}
		}
		qDebug() << "Session detail assignment action clicked pairId=" << pairId << "action=" << action;
		const QString sessionPath = QDir(sessionsPath()).filePath(currentSessionId);
		const int sessionTeamId = effectiveSessionTeamIdForPath(sessionPath);
		if (sessionTeamId <= 0)
		{
				QMessageBox::warning(this, "Session assignment", "Set valid TeamId first.");
				return;
		}
		ArsLocalWorkspace workspace;
		const QString workspaceRootPath = workspace.initialize() ? workspace.rootPath() : QString();
		ArsPlayerRepository playersRepository(workspaceRootPath);
		QStringList warnings;
		QList<ArsPlayer> players = playersRepository.loadPlayersForTeam(sessionTeamId, &warnings);
		for (const QString &warning : warnings)
		{
				qWarning().noquote() << warning;
		}
		if (players.isEmpty())
		{
				QMessageBox::warning(this, "Session assignment", "No players in selected team.");
				return;
		}

		QString selectedPlayerId;
		for (const ArsSessionPairAssignment &assignment : m_currentAssignments)
		{
				if (assignment.pairId.compare(pairId, Qt::CaseInsensitive) == 0)
				{
						selectedPlayerId = assignment.playerId;
						break;
				}
		}

		ArsSessionAssignmentDialog dialog(this);
		dialog.setPairId(pairId);
		dialog.setPlayers(players);
		dialog.setSelectedPlayerId(selectedPlayerId);
		if (dialog.exec() != QDialog::Accepted)
		{
				return;
		}

		ArsSessionPairAssignment manual;
		manual.pairId = pairId;
		for (const ArsSessionTrackerPair &pair : m_currentDetectedPairs)
		{
				if (pair.pairId.compare(pairId, Qt::CaseInsensitive) == 0)
				{
						manual.leftTrackerSerial = pair.leftTrackerSerial;
						manual.rightTrackerSerial = pair.rightTrackerSerial;
						break;
				}
		}
		manual.playerId = dialog.selectedPlayerId();
		manual.playerName = dialog.selectedPlayerName();
		manual.teamId = sessionTeamId;
		manual.source = "manual";
		manual.isOverride = true;

		QString error;
		if (!ArsSessionInfoJson::upsertManualSessionAssignment(sessionPath, manual, &error))
		{
				QMessageBox::warning(this, "Session assignment", QString("Failed to save assignment: %1").arg(error));
				return;
		}
		qDebug() << "Session assignment manual saved pairId=" << pairId << "playerId=" << manual.playerId << "playerName=" << manual.playerName;
		qDebug() << "Session detail assignment action done pairId=" << pairId << "playerId=" << manual.playerId;
		resolveAndPersistSessionAssignments(sessionPath, sessionTeamId, m_currentDetectedPairs);
		rebuildCurrentPairRows();
		fillSessionAssignmentsTable();
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

void ArsTrackerSessionsTab::onSessionTableCellClicked(int row, int column)
{
		if (sessionsTable == nullptr || row < 0 || row >= sessionsTable->rowCount())
		{
				return;
		}
		QTableWidgetItem *item = sessionsTable->item(row, kSessionsColumnSession);
		if (item == nullptr)
		{
				return;
		}
		const QString sessionId = item->data(Qt::UserRole).toString();
		const QString sessionPath = item->data(Qt::UserRole + 1).toString();
		qDebug() << "Sessions list row clicked row=" << row << "column=" << column << "session=" << sessionId << "path=" << sessionPath;
		if (column == kSessionsColumnActions)
		{
				return;
		}
		if (sessionId.trimmed().isEmpty())
		{
				return;
		}
		qDebug() << "Sessions list opening session from row session=" << sessionId;
		showSessionDetailsPage(sessionId);
}

bool ArsTrackerSessionsTab::validateSessionDeletePath(const QString &sessionName,
																											const QString &sessionPath,
																											QString *errorMessage) const
{
		if (sessionName.trimmed().isEmpty())
		{
				if (errorMessage != nullptr) *errorMessage = "Session name is empty.";
				return false;
		}
		if (sessionPath.trimmed().isEmpty())
		{
				if (errorMessage != nullptr) *errorMessage = "Session path is empty.";
				return false;
		}
		const QString sessionsRoot = sessionsPath();
		if (sessionsRoot.trimmed().isEmpty())
		{
				if (errorMessage != nullptr) *errorMessage = "Sessions root path is empty.";
				return false;
		}
		const QFileInfo targetInfo(sessionPath);
		if (!targetInfo.exists() || !targetInfo.isDir())
		{
				if (errorMessage != nullptr) *errorMessage = "Session directory does not exist.";
				return false;
		}
		const QString rootCanonical = QFileInfo(QDir::cleanPath(sessionsRoot)).canonicalFilePath();
		const QString targetCanonical = targetInfo.canonicalFilePath();
		if (rootCanonical.isEmpty() || targetCanonical.isEmpty())
		{
				if (errorMessage != nullptr) *errorMessage = "Failed to resolve canonical path.";
				return false;
		}
		const QString rootClean = QDir::cleanPath(rootCanonical);
		const QString targetClean = QDir::cleanPath(targetCanonical);
		if (QString::compare(rootClean, targetClean, Qt::CaseInsensitive) == 0)
		{
				if (errorMessage != nullptr) *errorMessage = "Refusing to delete sessions root.";
				return false;
		}
		const QString rootPrefix = rootClean + QDir::separator();
		if (!targetClean.startsWith(rootPrefix, Qt::CaseInsensitive))
		{
				if (errorMessage != nullptr) *errorMessage = "Session path is outside sessions root.";
				return false;
		}
		return true;
}

bool ArsTrackerSessionsTab::deleteSessionDirectory(const QString &sessionName,
																									 const QString &sessionPath,
																									 QString *errorMessage) const
{
		QString safetyError;
		if (!validateSessionDeletePath(sessionName, sessionPath, &safetyError))
		{
				if (errorMessage != nullptr) *errorMessage = safetyError;
				qWarning() << "Sessions delete safety check failed session=" << sessionName << "path=" << sessionPath << "error=" << safetyError;
				return false;
		}
		QDir dir(sessionPath);
		if (!dir.removeRecursively())
		{
				if (errorMessage != nullptr) *errorMessage = "Failed to delete session directory recursively.";
				return false;
		}
		return true;
}

void ArsTrackerSessionsTab::onDeleteSessionClicked()
{
		QPushButton *button = qobject_cast<QPushButton *>(sender());
		if (button == nullptr)
		{
				return;
		}
		const QString sessionName = button->property("sessionName").toString();
		const QString sessionPath = button->property("sessionPath").toString();
		qDebug() << "Sessions delete requested session=" << sessionName << "path=" << sessionPath;

		const QMessageBox::StandardButton answer = QMessageBox::question(
				this,
				"Delete session",
				QString("Delete session?\nSession: %1\nPath: %2\n\nThis will permanently delete this session folder and all related files:\n- raw tracker files\n- SessionInfo.json\n- postprocessed artifacts\n- generated reports, if any\n\nThis action cannot be undone.")
						.arg(sessionName, sessionPath),
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::No);
		if (answer != QMessageBox::Yes)
		{
				qDebug() << "Sessions delete cancelled session=" << sessionName;
				return;
		}

		qDebug() << "Sessions delete begin session=" << sessionName << "path=" << sessionPath;
		QString error;
		if (!deleteSessionDirectory(sessionName, sessionPath, &error))
		{
				qWarning() << "Sessions delete failed session=" << sessionName << "path=" << sessionPath << "error=" << error;
				QMessageBox::warning(this, "Sessions", QString("Failed to delete session: %1").arg(error));
				return;
		}
		qDebug() << "Sessions delete done session=" << sessionName << "path=" << sessionPath;

		if (currentSessionId == sessionName && pagesStack != nullptr && pagesStack->currentWidget() == detailsPage)
		{
				showSessionsListPage(false);
		}
		reloadSessions("delete-session");
		qDebug() << "Sessions list reloaded after delete count=" << m_filteredSessions.size();
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

		const int sessionColumn = kSessionsColumnSession;
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
		sessionsTable->resizeRowsToContents();
		qDebug() << "Sessions tab session name column resized"
						 << "viewportWidth=" << viewportWidth
						 << "contentWidth=" << contentWidth
						 << "maxWidth=" << maxWidth
						 << "finalWidth=" << finalWidth;
}
