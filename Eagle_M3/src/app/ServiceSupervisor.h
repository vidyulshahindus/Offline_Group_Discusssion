#pragma once

#include "WhisperServerProcess.h"

#include <QObject>
#include <QString>
#include <QUrl>

class AppConfig;
class Paths;
class QNetworkAccessManager;
class QNetworkReply;

enum class ServiceHealth {
    Unknown,
    Checking,
    Ok,
    Warning,
    Error,
};

struct ServiceStatus {
    QString id;
    QString label;
    ServiceHealth health = ServiceHealth::Unknown;
    QString detail;
    QString fixHint;
};

class ServiceSupervisor : public QObject
{
    Q_OBJECT

public:
    ServiceSupervisor(const AppConfig &config, const Paths &paths, QObject *parent = nullptr);
    ~ServiceSupervisor() override;

    QList<ServiceStatus> statuses() const;
    void refreshAll();

    // Starts helper processes owned by Eagle (M3: whisper-server).
    void startManagedServices(const QString &sttLanguage);
    void stopAll();

    // Language lock for listening ("en", "hi", "auto"). whisper-server takes the
    // language per request, so a restart is only done when
    // stt.restart_on_language_change is true.
    void setSttLanguage(const QString &language);
    QUrl whisperUrl() const;
    bool isWhisperReady() const;
    WhisperServerProcess::State whisperState() const;
    QString whisperDetail() const;

signals:
    void statusesChanged();
    void whisperStateChanged(bool ready, const QString &detail);

private slots:
    void onOllamaProbeFinished();
    void onWhisperStateChanged(WhisperServerProcess::State state, const QString &detail);

private:
    void setStatus(const QString &id, ServiceHealth health, const QString &detail, const QString &fixHint);
    void probeOllama();
    void seedPlaceholders();
    void updateWhisperRow();

    const AppConfig &m_config;
    const Paths &m_paths;
    QNetworkAccessManager *m_network = nullptr;
    WhisperServerProcess *m_whisper = nullptr;
    QList<ServiceStatus> m_statuses;
    QString m_sttLanguage = QStringLiteral("en");
    bool m_whisperStarted = false;
};
