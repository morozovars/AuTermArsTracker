#ifndef ARS_SESSION_ASSIGNMENT_DIALOG_H
#define ARS_SESSION_ASSIGNMENT_DIALOG_H

#include <QDialog>
#include <QList>

#include "ars/workspace/ArsPlayer.h"

class QLabel;
class QComboBox;

class ArsSessionAssignmentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ArsSessionAssignmentDialog(QWidget *parent = nullptr);

    void setPairId(const QString &pairId);
    void setPlayers(const QList<ArsPlayer> &players);
    void setSelectedPlayerId(const QString &playerId);

    QString selectedPlayerId() const;
    QString selectedPlayerName() const;

private slots:
    void onAcceptClicked();

private:
    QLabel *m_pairLabel = nullptr;
    QComboBox *m_players = nullptr;
};

#endif // ARS_SESSION_ASSIGNMENT_DIALOG_H
