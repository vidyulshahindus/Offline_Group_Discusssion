#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class AppConfig;
class Paths;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

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
    explicit ServiceSupervisor(const AppConfig &config, const Paths &paths, QObject *parent = nullptr);
    ~ServiceSupervisor() override;

    QList<ServiceStatus> statuses() const;
    void refreshAll();

    bool ensureWhisperServer(const QString &languageCode);
    void stopWhisperServer();
    QUrl whisperBaseUrl() const;
    QString runningWhisperLanguage() const { return m_whisperLang; }

signals:
    void statusesChanged();

private slots:
    void onOllamaProbeFinished();
    void onWhisperHealthFinished();

private:
    void setStatus(const QString &id, ServiceHealth health, const QString &detail, const QString &fixHint);
    void probeOllama();
    void probeWhisperHealth();
    void seedPlaceholders();
    bool startWhisperProcess(const QString &languageCode);

    const AppConfig &m_config;
    const Paths &m_paths;
    QNetworkAccessManager *m_network = nullptr;
    QProcess *m_whisper = nullptr;
    QString m_whisperLang;
    QList<ServiceStatus> m_statuses;
};
