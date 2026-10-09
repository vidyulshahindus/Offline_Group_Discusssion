#pragma once

#include <QDialog>

class QLabel;
class QPushButton;
class QVBoxLayout;
class ServiceSupervisor;
class TurnSpeechController;

class ReadinessPanel : public QDialog
{
    Q_OBJECT

public:
    explicit ReadinessPanel(ServiceSupervisor &supervisor, TurnSpeechController *turnSpeech,
                            QWidget *parent = nullptr);

private slots:
    void onRefreshClicked();
    void onStatusesChanged();
    void onTestVoiceClicked();
    void onVoiceTestFinished(bool ok, const QString &detail);
    void onTestToneClicked();
    void onTestToneFinished(bool ok, const QString &detail);
    void onOpenLogFolderClicked();

private:
    void rebuildRows();
    void refreshLogLabel();

    ServiceSupervisor &m_supervisor;
    TurnSpeechController *m_turnSpeech = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_logLabel = nullptr;
    QPushButton *m_testVoiceButton = nullptr;
    QPushButton *m_testToneButton = nullptr;
    QPushButton *m_openLogButton = nullptr;
};
