#include "ars_session_assignment_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

ArsSessionAssignmentDialog::ArsSessionAssignmentDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Assign player");
    resize(420, 170);

    QVBoxLayout *root = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout();
    m_pairLabel = new QLabel(this);
    form->addRow("Tracker pair", m_pairLabel);
    m_players = new QComboBox(this);
    form->addRow("Player*", m_players);
    root->addLayout(form);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &ArsSessionAssignmentDialog::onAcceptClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ArsSessionAssignmentDialog::setPairId(const QString &pairId)
{
    m_pairLabel->setText(pairId);
}

void ArsSessionAssignmentDialog::setPlayers(const QList<ArsPlayer> &players)
{
    m_players->clear();
    for (const ArsPlayer &player : players)
    {
        m_players->addItem(QString("%1 %2 #%3").arg(player.surname, player.name).arg(player.number), player.playerId);
    }
}

void ArsSessionAssignmentDialog::setSelectedPlayerId(const QString &playerId)
{
    const int idx = m_players->findData(playerId);
    if (idx >= 0) m_players->setCurrentIndex(idx);
}

QString ArsSessionAssignmentDialog::selectedPlayerId() const
{
    return m_players->currentData().toString().trimmed();
}

QString ArsSessionAssignmentDialog::selectedPlayerName() const
{
    QString text = m_players->currentText().trimmed();
    const int hash = text.lastIndexOf('#');
    if (hash > 0)
    {
        text = text.left(hash).trimmed();
    }
    return text;
}

void ArsSessionAssignmentDialog::onAcceptClicked()
{
    if (selectedPlayerId().isEmpty())
    {
        QMessageBox::warning(this, "Assign player", "Select player.");
        return;
    }
    accept();
}
