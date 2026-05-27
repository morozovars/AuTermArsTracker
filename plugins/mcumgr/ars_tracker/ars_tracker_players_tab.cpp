#include "ars_tracker_players_tab.h"

#include "ars_tracker_player_edit_dialog.h"

#include <QDate>
#include <QDebug>
#include <QDialog>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QSet>
#include <algorithm>

#include "ars/workspace/ArsAppSettings.h"
#include "ars/workspace/ArsLocalWorkspace.h"
#include "ars/workspace/ArsPlayerRepository.h"
#include "ars/workspace/ArsTeamRepository.h"

ArsTrackerPlayersTab::ArsTrackerPlayersTab(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    reloadPlayers("init");
}

QString ArsTrackerPlayersTab::workspacePath() const
{
    ArsLocalWorkspace workspace;
    if (!workspace.initialize())
    {
        return QString();
    }
    return workspace.rootPath();
}

void ArsTrackerPlayersTab::buildUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    QHBoxLayout *top = new QHBoxLayout();
    m_currentTeamLabel = new QLabel("Current team: No teams", this);
    m_teamSelector = new QComboBox(this);
    m_reloadButton = new QPushButton("Reload", this);
    m_createButton = new QPushButton("Create Player", this);
    top->addWidget(m_currentTeamLabel, 1);
    top->addWidget(m_teamSelector);
    top->addWidget(m_reloadButton);
    top->addWidget(m_createButton);
    root->addLayout(top);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(QStringList() << "Photo" << QString::fromUtf8("Фамилия Имя") << QString::fromUtf8("Позиция")
                                                      << QString::fromUtf8("Номер") << QString::fromUtf8("Возраст") << "Actions");
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_table->setColumnWidth(0, 72);
    m_table->verticalHeader()->setDefaultSectionSize(110);
    root->addWidget(m_table, 1);

    connect(m_reloadButton, &QPushButton::clicked, this, [this]() { reloadPlayers("reload-click"); });
    connect(m_teamSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ArsTrackerPlayersTab::onTeamChanged);
    connect(m_createButton, &QPushButton::clicked, this, &ArsTrackerPlayersTab::onCreatePlayer);
}

void ArsTrackerPlayersTab::reloadPlayers(const QString &reason)
{
    Q_UNUSED(reason);
    const QString workspace = workspacePath();
    qDebug() << "Ars Players tab reload begin workspace=" << workspace;
    reloadTeamsAndSelection();
    reloadCurrentTeamPlayers();
}

void ArsTrackerPlayersTab::reloadTeamsAndSelection()
{
    const QString workspace = workspacePath();
    if (workspace.trimmed().isEmpty())
    {
        m_teams.clear();
        m_teamSelector->clear();
        updateCurrentTeamLabel();
        m_createButton->setEnabled(false);
        return;
    }

    ArsTeamRepository teamRepository(workspace);
    QStringList warnings;
    m_teams = teamRepository.loadTeams(&warnings);
    qDebug() << "Ars Players teams loaded count=" << m_teams.size();
    for (const QString &warning : warnings)
    {
        qWarning().noquote() << warning;
    }

    int preferredTeamId = -1;
    QString settingsError;
    ArsAppSettings settings(workspace);
    settings.loadDefaultTeamId(&preferredTeamId, &settingsError);
    if (preferredTeamId <= 0 && !m_teams.isEmpty())
    {
        preferredTeamId = m_teams.first().teamId;
    }

    m_teamSelector->blockSignals(true);
    m_teamSelector->clear();
    for (const ArsTeam &team : m_teams)
    {
        m_teamSelector->addItem(QString("%1 (%2)").arg(team.name, team.ageCategory), team.teamId);
    }
    int idx = m_teamSelector->findData(preferredTeamId);
    if (idx < 0 && m_teamSelector->count() > 0)
    {
        idx = 0;
    }
    if (idx >= 0)
    {
        m_teamSelector->setCurrentIndex(idx);
    }
    m_teamSelector->blockSignals(false);
    m_createButton->setEnabled(m_teamSelector->count() > 0);
    updateCurrentTeamLabel();
}

int ArsTrackerPlayersTab::currentTeamId() const
{
    return m_teamSelector->currentData().toInt();
}

const ArsTeam *ArsTrackerPlayersTab::findTeamById(int teamId) const
{
    for (const ArsTeam &team : m_teams)
    {
        if (team.teamId == teamId)
        {
            return &team;
        }
    }
    return nullptr;
}

void ArsTrackerPlayersTab::updateCurrentTeamLabel()
{
    const ArsTeam *team = findTeamById(currentTeamId());
    if (team == nullptr)
    {
        m_currentTeamLabel->setText("Current team: No teams");
        return;
    }
    m_currentTeamLabel->setText(QString("Current team: %1 (%2)").arg(team->name, team->ageCategory));
}

int ArsTrackerPlayersTab::positionGroup(const QString &position) const
{
    static const QSet<QString> midfielders = {
        QString::fromUtf8("Атакующий полузащитник"),
        QString::fromUtf8("Фланговый полузащитник"),
        QString::fromUtf8("Центральный полузащитник"),
        QString::fromUtf8("Опорный полузащитник")};
    static const QSet<QString> defenders = {
        QString::fromUtf8("Фланговый защитник"),
        QString::fromUtf8("Центральный защитник")};
    if (position == QString::fromUtf8("Нападающий"))
    {
        return 0;
    }
    if (midfielders.contains(position))
    {
        return 1;
    }
    if (defenders.contains(position))
    {
        return 2;
    }
    if (position == QString::fromUtf8("Вратарь"))
    {
        return 3;
    }
    return 99;
}

QString ArsTrackerPlayersTab::ageDisplay(const ArsPlayer &player) const
{
    if (!player.birthDate.isValid())
    {
        return "-";
    }
    int age = QDate::currentDate().year() - player.birthDate.year();
    if (QDate::currentDate() < QDate(QDate::currentDate().year(), player.birthDate.month(), player.birthDate.day()))
    {
        age -= 1;
    }
    return age >= 0 ? QString::number(age) : "-";
}

void ArsTrackerPlayersTab::reloadCurrentTeamPlayers()
{
    const QString workspace = workspacePath();
    m_players.clear();
    if (workspace.trimmed().isEmpty() || currentTeamId() <= 0)
    {
        rebuildPlayersTable();
        return;
    }

    ArsPlayerRepository repository(workspace);
    QStringList warnings;
    m_players = repository.loadPlayersForTeam(currentTeamId(), &warnings);
    for (const QString &warning : warnings)
    {
        qWarning().noquote() << warning;
    }

    std::sort(m_players.begin(), m_players.end(), [this](const ArsPlayer &a, const ArsPlayer &b) {
        const int ga = positionGroup(a.position);
        const int gb = positionGroup(b.position);
        if (ga != gb) return ga < gb;
        if (a.number != b.number) return a.number < b.number;
        const int surnameCmp = QString::compare(a.surname, b.surname, Qt::CaseInsensitive);
        if (surnameCmp != 0) return surnameCmp < 0;
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });

    const ArsTeam *team = findTeamById(currentTeamId());
    qDebug() << "Ars Players current team id=" << currentTeamId() << "name=" << (team != nullptr ? team->name : QString());
    qDebug() << "Ars Players loaded count=" << m_players.size() << "teamId=" << currentTeamId();
    rebuildPlayersTable();
}

void ArsTrackerPlayersTab::rebuildPlayersTable()
{
    m_table->clearContents();
    m_table->setRowCount(m_players.size());
    const QString workspace = workspacePath();
    ArsPlayerRepository repository(workspace);

    for (int row = 0; row < m_players.size(); ++row)
    {
        const ArsPlayer &player = m_players.at(row);
        QLabel *photoLabel = new QLabel(m_table);
        photoLabel->setAlignment(Qt::AlignCenter);
        const QString absolutePhotoPath = repository.resolvePhotoAbsolutePath(player.photoPath);
        if (player.photoPath.trimmed().isEmpty())
        {
            photoLabel->setText("No photo");
        }
        else if (!QFileInfo::exists(absolutePhotoPath))
        {
            qWarning() << "Ars Player photo missing playerId=" << player.playerId << "path=" << absolutePhotoPath;
            photoLabel->setText("Missing");
        }
        else
        {
            QPixmap pixmap(absolutePhotoPath);
            if (pixmap.isNull())
            {
                qWarning() << "Ars Player photo invalid playerId=" << player.playerId << "path=" << absolutePhotoPath;
                photoLabel->setText("Invalid image");
            }
            else
            {
                photoLabel->setPixmap(pixmap.scaled(60, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        }
        m_table->setCellWidget(row, 0, photoLabel);
        m_table->setItem(row, 1, new QTableWidgetItem(QString("%1 %2").arg(player.surname, player.name)));
        m_table->setItem(row, 2, new QTableWidgetItem(player.position));
        m_table->setItem(row, 3, new QTableWidgetItem(QString::number(player.number)));
        m_table->setItem(row, 4, new QTableWidgetItem(ageDisplay(player)));
        m_table->setRowHeight(row, 110);

        QWidget *actionsWidget = new QWidget(m_table);
        QHBoxLayout *actionsLayout = new QHBoxLayout(actionsWidget);
        actionsLayout->setContentsMargins(2, 2, 2, 2);
        actionsLayout->setSpacing(4);
        QPushButton *edit = new QPushButton("Edit", actionsWidget);
        edit->setProperty("playerId", player.playerId);
        connect(edit, &QPushButton::clicked, this, &ArsTrackerPlayersTab::onEditPlayer);
        QPushButton *del = new QPushButton("Delete", actionsWidget);
        del->setProperty("playerId", player.playerId);
        connect(del, &QPushButton::clicked, this, &ArsTrackerPlayersTab::onDeletePlayer);
        actionsLayout->addWidget(edit);
        actionsLayout->addWidget(del);
        m_table->setCellWidget(row, 5, actionsWidget);
    }
}

ArsPlayer *ArsTrackerPlayersTab::findPlayerById(const QString &playerId)
{
    for (ArsPlayer &player : m_players)
    {
        if (player.playerId == playerId)
        {
            return &player;
        }
    }
    return nullptr;
}

bool ArsTrackerPlayersTab::validatePhotoExtension(const QString &sourcePath, QString *errorMessage) const
{
    const QString ext = QFileInfo(sourcePath).suffix().toLower();
    const QSet<QString> allowed = {"png", "jpg", "jpeg", "bmp", "webp"};
    if (!allowed.contains(ext))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Unsupported photo extension: .%1").arg(ext);
        }
        return false;
    }
    return true;
}

bool ArsTrackerPlayersTab::savePlayerAndLog(const ArsPlayer &player)
{
    const QString workspace = workspacePath();
    ArsPlayerRepository repository(workspace);
    QString error;
    if (!repository.savePlayer(player, &error))
    {
        QMessageBox::warning(this, "Player", QString("Failed to save player: %1").arg(error));
        return false;
    }
    qDebug() << "Ars Player saved playerId=" << player.playerId << "path=" << repository.playerFilePath(player.playerId);
    return true;
}

void ArsTrackerPlayersTab::onCreatePlayer()
{
    qDebug() << "Ars Player create begin";
    if (m_teams.isEmpty())
    {
        QMessageBox::warning(this, "Players", "No teams available.");
        return;
    }
    ArsTrackerPlayerEditDialog dialog(this);
    dialog.setWorkspaceRootPath(workspacePath());
    dialog.setTeams(m_teams);
    dialog.setCreateMode(currentTeamId());
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const QString workspace = workspacePath();
    ArsPlayerRepository repository(workspace);
    QStringList warnings;
    ArsPlayer player = dialog.playerFromUi();
    player.playerId = repository.nextPlayerId(&warnings);
    for (const QString &warning : warnings)
    {
        qWarning().noquote() << warning;
    }

    if (dialog.photoSelectionChanged())
    {
        QString extensionError;
        if (!validatePhotoExtension(dialog.selectedPhotoSourcePath(), &extensionError))
        {
            QMessageBox::warning(this, "Player", extensionError);
            return;
        }
        qDebug() << "Ars Player photo copy begin playerId=" << player.playerId << "source=" << dialog.selectedPhotoSourcePath();
        QString relativePhotoPath;
        QString photoError;
        if (!repository.copyPlayerPhotoToWorkspace(player.playerId, dialog.selectedPhotoSourcePath(), &relativePhotoPath, &photoError))
        {
            QMessageBox::warning(this, "Player", QString("Failed to copy player photo: %1").arg(photoError));
            return;
        }
        player.photoPath = relativePhotoPath;
        qDebug() << "Ars Player photo copied playerId=" << player.playerId << "relativePath=" << relativePhotoPath;
    }

    if (!savePlayerAndLog(player))
    {
        return;
    }
    QString settingsError;
    ArsAppSettings(workspace).saveLastPlayerId(player.playerId.toInt(), &settingsError);
    reloadPlayers("create");
}

void ArsTrackerPlayersTab::onEditPlayer()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (button == nullptr)
    {
        return;
    }
    const QString playerId = button->property("playerId").toString().trimmed();
    ArsPlayer *existing = findPlayerById(playerId);
    if (existing == nullptr)
    {
        return;
    }
    qDebug() << "Ars Player edit begin playerId=" << playerId;
    ArsTrackerPlayerEditDialog dialog(this);
    dialog.setWorkspaceRootPath(workspacePath());
    dialog.setTeams(m_teams);
    ArsPlayer editingPlayer = *existing;
    dialog.setEditMode(editingPlayer);
    const QString workspace = workspacePath();
    ArsPlayerRepository repository(workspace);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    ArsPlayer updated = dialog.playerFromUi();
    updated.playerId = playerId;
    updated.photoPath = editingPlayer.photoPath;

    if (dialog.photoSelectionChanged())
    {
        QString extensionError;
        if (!validatePhotoExtension(dialog.selectedPhotoSourcePath(), &extensionError))
        {
            QMessageBox::warning(this, "Player", extensionError);
            return;
        }
        qDebug() << "Ars Player photo copy begin playerId=" << updated.playerId << "source=" << dialog.selectedPhotoSourcePath();
        QString relativePhotoPath;
        QString photoError;
        if (!repository.copyPlayerPhotoToWorkspace(updated.playerId, dialog.selectedPhotoSourcePath(), &relativePhotoPath, &photoError))
        {
            QMessageBox::warning(this, "Player", QString("Failed to copy player photo: %1").arg(photoError));
            return;
        }
        updated.photoPath = relativePhotoPath;
        qDebug() << "Ars Player photo copied playerId=" << updated.playerId << "relativePath=" << relativePhotoPath;
    }

    if (!savePlayerAndLog(updated))
    {
        return;
    }
    reloadPlayers("edit");
}

void ArsTrackerPlayersTab::onDeletePlayer()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (button == nullptr)
    {
        return;
    }
    const QString playerId = button->property("playerId").toString().trimmed();
    ArsPlayer *player = findPlayerById(playerId);
    if (player == nullptr || playerId.isEmpty())
    {
        return;
    }
    qDebug() << "Ars Player delete requested playerId=" << playerId << "name=" << player->surname << player->name;
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        "Delete player",
        QString("Delete player?\nPlayer: %1 %2\nNumber: %3\n\nThis will delete player JSON and player photo/assets.\nThis action cannot be undone.")
            .arg(player->surname, player->name)
            .arg(player->number),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
    {
        return;
    }
    qDebug() << "Ars Player delete confirmed playerId=" << playerId;

    ArsPlayerRepository repository(workspacePath());
    QString error;
    if (!repository.deletePlayer(playerId, &error))
    {
        qWarning() << "Ars Player delete failed playerId=" << playerId << "error=" << error;
        QMessageBox::warning(this, "Player", QString("Failed to delete player: %1").arg(error));
        return;
    }
    qDebug() << "Ars Player delete done playerId=" << playerId;
    reloadPlayers("delete");
}

void ArsTrackerPlayersTab::onTeamChanged()
{
    updateCurrentTeamLabel();
    reloadCurrentTeamPlayers();
}
