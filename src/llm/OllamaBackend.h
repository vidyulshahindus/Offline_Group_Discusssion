#pragma once

#include "ILlmBackend.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

class AppConfig;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

struct OllamaLoadOptions {
    int numCtx = 4096;
    int numThread = 0;
    int numBatch = 256;
};

class OllamaBackend : public ILlmBackend
{
    Q_OBJECT

public:
    explicit OllamaBackend(const AppConfig &config, QObject *parent = nullptr);
    ~OllamaBackend() override;

    void start(const LlmRequest &req) override;
    void abort() override;
    bool isBusy() const override;
    void probe() override;
    void showModel(const QString &model) override;
    void preload(const QString &model) override;

    void warmUp(const QString &model, const QString &systemPrompt);
    void runStartupSequence(const QString &model, const QString &fallbackModel,
                            const QString &systemPrompt);

    OllamaLoadOptions loadOptions() const { return m_loadOptions; }
    QStringList installedModels() const { return m_installedModels; }

signals:
    void warmUpFinished(bool ok);
    void startupSequenceFinished(bool ok);

private slots:
    void onStreamReadyRead();
    void onStreamFinished();
    void onFirstTokenTimeout();
    void onOverallTimeout();
    void onProbeFinished();
    void onShowFinished();
    void onSimpleChatFinished();

private:
    enum class StartupStep {
        Idle,
        ShowPrimary,
        ShowFallback,
        Preload,
        WarmUp,
    };

    struct ModelInfo {
        QStringList capabilities;
        qint64 sizeBytes = 0;
    };

    void cleanupStream();
    QJsonObject buildOptions(const LlmRequest &req) const;
    QJsonObject buildChatBody(const QString &model, const QVector<LlmMessage> &messages,
                              const LlmRequest &req, bool forWarmUp, bool stream) const;
    QNetworkReply *postChat(const QJsonObject &body, const QString &purpose, bool streamResponse);
    bool modelHasThinking(const QString &model) const;
    bool shouldSendThinkFalse(const QString &model) const;
    void parseStreamLine(const QByteArray &line);
    void handleModelFallback(const QString &errorText);
    void logChatRequest(const QString &purpose, const QJsonObject &body) const;
    void logStreamLine(const QByteArray &line, qint64 msSincePost) const;
    void advanceStartupSequence();
    void beginTurnStream();
    void finishStreamTurn(bool success, const QString &error = {});

    const AppConfig &m_config;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_streamReply = nullptr;
    QNetworkReply *m_auxReply = nullptr;
    QTimer *m_firstTokenTimer = nullptr;
    QTimer *m_overallTimer = nullptr;

    OllamaLoadOptions m_loadOptions;
    LlmRequest m_activeRequest;
    QByteArray m_lineBuffer;
    QString m_streamText;
    QString m_streamPurpose;
    bool m_gotFirstToken = false;
    bool m_loggedFirstStreamByte = false;
    QElapsedTimer m_requestTimer;
    QElapsedTimer m_streamPostTimer;
    qint64 m_turnId = -1;

    QStringList m_installedModels;
    QHash<QString, ModelInfo> m_modelInfo;
    QString m_pendingWarmModel;
    QString m_pendingWarmSystem;
    QString m_pendingPreloadModel;
    QString m_auxPurpose;
    bool m_streamCompleted = false;
    qint64 m_firstTokenMs = -1;

    StartupStep m_startupStep = StartupStep::Idle;
    QString m_startupPrimaryModel;
    QString m_startupFallbackModel;
    QString m_startupSystemPrompt;

    QSet<QString> m_modelsRequireThinkFalse;
    bool m_currentRequestSentThinkFalse = false;
    bool m_turnThinkingRetryUsed = false;
    bool m_pendingThinkFalseRetry = false;
};
