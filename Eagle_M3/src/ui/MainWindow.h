#pragma once

#include "LanguageService.h"
#include "PromptTypes.h"
#include "llm/ILlmBackend.h"

#include <QElapsedTimer>
#include <QMainWindow>

class AppConfig;
class ChatPanel;
class ListenController;
class LlmServices;
class MetricsOverlay;
class Paths;
class PiperSpeaker;
class ServiceSupervisor;
class TurnSpeechController;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(const AppConfig &config, const Paths &paths, ServiceSupervisor &supervisor,
               LlmServices &llm, QWidget *parent = nullptr);
    ~MainWindow() override;

    TurnSpeechController *turnSpeech() const { return m_turnSpeech; }

private slots:
    void onSendClicked();
    void onNewChatClicked();
    void onStopClicked();
    void onLlmToken(int turnId, const QString &text);
    void onLlmFinished(int turnId, const QString &fullText, const LlmMetrics &metrics);
    void onLlmFailed(int turnId, const QString &error, bool recoverable);
    void onModelsProbed(bool ok, const QString &version, const QStringList &models);
    void onSpeakerSelectionChanged(int index);
    void onRefreshMicsClicked();
    void onRecordClicked();
    void onLanguageChanged(int index);
    void onListenError(const QString &message);
    void onPiperSpeakingChanged(bool speaking);
    void onPiperStatus(const QString &status);
    void onPiperError(const QString &message);
    void onTurnFirstAudio(int turnId, qint64 t1Ms);
    void onWordBudgetReached(int turnId);
    void onTurnSpeechError(const QString &message);
    void showReadinessPanel();
    void showMetricsOverlay();

private:
    void onClientError(const QString &message);
    void onTurnIdle(int turnId);
    void refreshTurnChrome();
    void setGenerating(bool generating);
    void refreshSpeakers();
    void populateModels(const QStringList &models);
    void wirePaths(const Paths &paths);
    void selectInitialLanguage();
    QString languageCode() const;
    // Reply language for a new turn. In Auto mode it follows the question's
    // script and the Hinglish heuristic (spec §10.5).
    SessionLanguage languageForTurn(const QString &question, QString *note) const;
    EagleMode currentMode() const;
    bool isEagleBusy() const;

    void finalizeTurnMetrics(int turnId, const LlmMetrics &metrics,
                             const QVector<LlmMessage> &historyBeforeAppend);

    const AppConfig &m_config;
    const Paths &m_paths;
    ServiceSupervisor &m_supervisor;
    LlmServices &m_llm;

    ChatPanel *m_chat = nullptr;
    MetricsOverlay *m_metricsOverlay = nullptr;
    ListenController *m_listen = nullptr;
    PiperSpeaker *m_piper = nullptr;
    TurnSpeechController *m_turnSpeech = nullptr;
    LanguageService m_languages;

    QString m_lastUserTurnMessage;
    SessionLanguage m_turnLanguage = SessionLanguage::English;
    int m_turnCounter = 0;
    QElapsedTimer m_turnTimer;
    UnitContext m_unitContext;
    LlmMetrics m_pendingLlmMetrics;
    bool m_pendingTurnOpen = false;
};
