#pragma once

#include <QObject>
#include <QString>

class AppConfig;
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
    explicit ServiceSupervisor(const AppConfig &config, QObject *parent = nullptr);

    QList<ServiceStatus> statuses() const;
    void refreshAll();

signals:
    void statusesChanged();

private slots:
    void onOllamaProbeFinished();

private:
    void setStatus(const QString &id, ServiceHealth health, const QString &detail, const QString &fixHint);
    void probeOllama();
    void seedPlaceholders();

    const AppConfig &m_config;
    QNetworkAccessManager *m_network = nullptr;
    QList<ServiceStatus> m_statuses;
};
