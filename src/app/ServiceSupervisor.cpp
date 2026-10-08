#include "ServiceSupervisor.h"

#include "AppConfig.h"
#include "Paths.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

namespace {

int indexOfStatus(QList<ServiceStatus> &list, const QString &id)
{
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].id == id) {
            return i;
        }
    }
    return -1;
}

} // namespace

ServiceSupervisor::ServiceSupervisor(const AppConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_network(new QNetworkAccessManager(this))
{
    seedPlaceholders();
}

QList<ServiceStatus> ServiceSupervisor::statuses() const
{
    return m_statuses;
}

void ServiceSupervisor::seedPlaceholders()
{
    m_statuses = {
        {QStringLiteral("ollama"), QStringLiteral("Ollama (LLM)"), ServiceHealth::Unknown,
         QStringLiteral("Not checked yet"),
         QStringLiteral("Install Ollama and ensure it is running on localhost:11434.")},
        {QStringLiteral("whisper"), QStringLiteral("whisper-server (STT)"), ServiceHealth::Unknown,
         QStringLiteral("Not supervised in M0"),
         QStringLiteral("whisper-server will be started in a later milestone.")},
        {QStringLiteral("piper"), QStringLiteral("Piper (TTS)"), ServiceHealth::Unknown,
         QStringLiteral("Not supervised in M0"),
         QStringLiteral("Place piper under tools/piper or configure tts.piper_exe.")},
        {QStringLiteral("robot"), QStringLiteral("Robot bridge"), ServiceHealth::Unknown,
         QStringLiteral("Not supervised in M0"),
         QStringLiteral("Optional: run robot_bridge on port 9100.")},
    };
}

void ServiceSupervisor::setStatus(const QString &id,
                                  ServiceHealth health,
                                  const QString &detail,
                                  const QString &fixHint)
{
    const int idx = indexOfStatus(m_statuses, id);
    if (idx < 0) {
        return;
    }
    m_statuses[idx].health = health;
    m_statuses[idx].detail = detail;
    if (!fixHint.isEmpty()) {
        m_statuses[idx].fixHint = fixHint;
    }
    emit statusesChanged();
}

void ServiceSupervisor::refreshAll()
{
    seedPlaceholders();
    emit statusesChanged();

    Paths paths(m_config);
    const QString whisperCli = paths.whisperCli();
    if (whisperCli.isEmpty()) {
        setStatus(QStringLiteral("whisper"), ServiceHealth::Warning,
                  QStringLiteral("Prototype STT uses whisper-cli; binary not found"),
                  QStringLiteral("Build or copy whisper-cli into tools/whisper/Release/."));
    } else {
        setStatus(QStringLiteral("whisper"), ServiceHealth::Ok, whisperCli, {});
    }

    const QString piper = paths.piperExecutable();
    if (piper.isEmpty()) {
        setStatus(QStringLiteral("piper"), ServiceHealth::Warning,
                  QStringLiteral("Piper executable not found"), paths.toolsDir());
    } else {
        setStatus(QStringLiteral("piper"), ServiceHealth::Ok, piper, {});
    }

    probeOllama();
}

void ServiceSupervisor::probeOllama()
{
    setStatus(QStringLiteral("ollama"), ServiceHealth::Checking,
              QStringLiteral("Checking %1…").arg(m_config.llmBaseUrl()), {});

    const QUrl url(m_config.llmBaseUrl() + QStringLiteral("/api/version"));
    QNetworkReply *reply = m_network->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, &ServiceSupervisor::onOllamaProbeFinished);
}

void ServiceSupervisor::onOllamaProbeFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        setStatus(QStringLiteral("ollama"), ServiceHealth::Error,
                  QStringLiteral("Unreachable: %1").arg(reply->errorString()),
                  QStringLiteral("Start Ollama (ollama serve) and confirm llm.base_url in config."));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QString version =
        doc.isObject() ? doc.object().value(QStringLiteral("version")).toString() : QString();
    setStatus(QStringLiteral("ollama"), ServiceHealth::Ok,
              version.isEmpty() ? QStringLiteral("Reachable") : QStringLiteral("Version %1").arg(version),
              {});
}
