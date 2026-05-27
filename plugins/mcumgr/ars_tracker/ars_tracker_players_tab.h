#ifndef ARS_TRACKER_PLAYERS_TAB_H
#define ARS_TRACKER_PLAYERS_TAB_H

#include <QWidget>
#include <QHash>

#include "ars/workspace/ArsPlayer.h"
#include "ars/workspace/ArsTeamRepository.h"

class QLabel;
class QPushButton;
class QComboBox;
class QTableWidget;

class ArsTrackerPlayersTab : public QWidget
{
    Q_OBJECT

public:
    explicit ArsTrackerPlayersTab(QWidget *parent = nullptr);

public slots:
    void reloadPlayers(const QString &reason = QString());

private slots:
    void onTeamChanged();
    void onCreatePlayer();
    void onEditPlayer();
    void onDeletePlayer();
    void onAssignTracker();

private:
    QString workspacePath() const;
    void buildUi();
    void reloadTeamsAndSelection();
    void reloadCurrentTeamPlayers();
    void updateCurrentTeamLabel();
    void rebuildPlayersTable();
    ArsPlayer *findPlayerById(const QString &playerId);
    const ArsTeam *findTeamById(int teamId) const;
    int currentTeamId() const;
    int positionGroup(const QString &position) const;
    QString ageDisplay(const ArsPlayer &player) const;
    bool savePlayerAndLog(const ArsPlayer &player);
    bool validatePhotoExtension(const QString &sourcePath, QString *errorMessage) const;
    QString boundPairForPlayer(const QString &playerId) const;

    QLabel *m_currentTeamLabel = nullptr;
    QComboBox *m_teamSelector = nullptr;
    QPushButton *m_reloadButton = nullptr;
    QPushButton *m_createButton = nullptr;
    QTableWidget *m_table = nullptr;

    QList<ArsTeam> m_teams;
    QList<ArsPlayer> m_players;
    QHash<QString, QString> m_playerPairByPlayerId;
};

#endif // ARS_TRACKER_PLAYERS_TAB_H
