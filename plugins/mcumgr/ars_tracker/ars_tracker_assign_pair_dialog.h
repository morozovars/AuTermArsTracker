#ifndef ARS_TRACKER_ASSIGN_PAIR_DIALOG_H
#define ARS_TRACKER_ASSIGN_PAIR_DIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;

class ArsTrackerAssignPairDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ArsTrackerAssignPairDialog(QWidget *parent = nullptr);

    void setPlayerContext(const QString &playerName, const QString &teamName);
    void setInitialPair(const QString &pairId, const QString &leftSerial, const QString &rightSerial);

    QString pairId() const;
    QString leftTrackerSerial() const;
    QString rightTrackerSerial() const;

private slots:
    void onPairChanged(const QString &value);
    void onAcceptClicked();

private:
    QLabel *m_playerLabel = nullptr;
    QLabel *m_teamLabel = nullptr;
    QLineEdit *m_pairId = nullptr;
    QLineEdit *m_leftSerial = nullptr;
    QLineEdit *m_rightSerial = nullptr;
    bool m_autoFillSerials = true;
};

#endif // ARS_TRACKER_ASSIGN_PAIR_DIALOG_H
