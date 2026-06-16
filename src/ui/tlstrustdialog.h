#ifndef TLSTRUSTDIALOG_H
#define TLSTRUSTDIALOG_H

#include <QDialog>

class QListWidget;
class QLabel;

class TlsTrustDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TlsTrustDialog(QWidget* parent = nullptr);

private Q_SLOTS:
    void reloadPeers();
    void updateFingerprintPreview();
    void copyFingerprint();
    void exportTrustStore();
    void importTrustStore();
    void removeSelectedPeer();
    void clearAllPeers();

private:
    QListWidget* mPeerList;
    QLabel* mFingerprintLabel;
};

#endif // TLSTRUSTDIALOG_H
