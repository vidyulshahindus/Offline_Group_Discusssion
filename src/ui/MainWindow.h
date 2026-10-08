#pragma once

#include <QMainWindow>

class AppConfig;
class ChatPanel;
class OllamaClient;
class Paths;
class PiperSpeaker;
class ServiceSupervisor;
class SpeechInput;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(const AppConfig &config, const Paths &paths, ServiceSupervisor &supervisor,
               QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onSendClicked();
    void onNewChatClicked();
    void onStopClicked();
    void onResponseChunk(const QString &text);
    void onResponseFinished();
    void onClientError(const QString &message);
    void onMicSelectionChanged(int index);
    void onSpeakerSelectionChanged(int index);
    void onRefreshMicsClicked();
    void onRecordClicked();
    void onRecordingChanged(bool recording);
    void onPartialTranscription(const QString &text);
    void onTranscriptionReady(const QString &text);
    void onSpeechError(const QString &message);
    void onSpeechStatus(const QString &status);
    void onPiperSpeakingChanged(bool speaking);
    void onPiperStatus(const QString &status);
    void onPiperError(const QString &message);
    void showReadinessPanel();

private:
    void setGenerating(bool generating);
    void refreshMicrophones();
    void refreshSpeakers();
    void showSpeechText(const QString &text);
    void populateModels();
    void wirePaths(const Paths &paths);

    const AppConfig &m_config;
    ServiceSupervisor &m_supervisor;

    ChatPanel *m_chat = nullptr;
    OllamaClient *m_client = nullptr;
    SpeechInput *m_speech = nullptr;
    PiperSpeaker *m_piper = nullptr;

    QString m_speechPrefix;
};
