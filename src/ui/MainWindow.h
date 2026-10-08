#pragma once

#include "PromptTypes.h"
#include "llm/ILlmBackend.h"

#include <QElapsedTimer>
#include <QMainWindow>

class AppConfig;
class ChatPanel;
class LlmServices;
class MetricsOverlay;
class Paths;
class PiperSpeaker;
class ServiceSupervisor;
class SpeechInput;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(const AppConfig &config, const Paths &paths, ServiceSupervisor &supervisor,
               LlmServices &llm, QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onSendClicked();
    void onNewChatClicked();
    void onStopClicked();
    void onLlmToken(int turnId, const QString &text);
    void onLlmFinished(int turnId, const QString &fullText, const LlmMetrics &metrics);
    void onLlmFailed(int turnId, const QString &error, bool recoverable);
    void onModelsProbed(bool ok, const QString &version, const QStringList &models);
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
    void showMetricsOverlay();

private:
    void onClientError(const QString &message);
    void setGenerating(bool generating);
    void refreshMicrophones();
    void refreshSpeakers();
    void showSpeechText(const QString &text);
    void populateModels(const QStringList &models);
    void wirePaths(const Paths &paths);
    SessionLanguage currentLanguage() const;
    EagleMode currentMode() const;

    const AppConfig &m_config;
    ServiceSupervisor &m_supervisor;
    LlmServices &m_llm;

    ChatPanel *m_chat = nullptr;
    MetricsOverlay *m_metricsOverlay = nullptr;
    SpeechInput *m_speech = nullptr;
    PiperSpeaker *m_piper = nullptr;

    QString m_speechPrefix;
    QString m_lastUserTurnMessage;
    int m_turnCounter = 0;
    QElapsedTimer m_turnTimer;
    UnitContext m_unitContext;
};
