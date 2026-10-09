#include "ServiceSupervisor.h"

#include "AppConfig.h"
#include "Paths.h"
#include "SttConfig.h"

#include <QDir>
#include <QFileInfo>
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

ServiceSupervisor::ServiceSupervisor(const AppConfig &config, const Paths &paths, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_paths(paths)
    , m_network(new QNetworkAccessManager(this))
    , m_whisper(new WhisperServerProcess(this))
{
    connect(m_whisper, &WhisperServerProcess::stateChanged, this,
            &ServiceSupervisor::onWhisperStateChanged);
    seedPlaceholders();
}

ServiceSupervisor::~ServiceSupervisor()
{
    disconnect(m_whisper, nullptr, this, nullptr);
    stopAll();
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
        {QStringLiteral("whisper"), QStringLiteral("whisper-server (speech to text)"),
         ServiceHealth::Unknown, QStringLiteral("Not started yet"), QString()},
        {QStringLiteral("piper"), QStringLiteral("Piper (TTS)"), ServiceHealth::Unknown,
         QStringLiteral("Not checked yet"),
         QStringLiteral("Place piper under tools/piper or configure tts.piper_exe.")},
        {QStringLiteral("robot"), QStringLiteral("Robot bridge"), ServiceHealth::Unknown,
         QStringLiteral("Not supervised yet (M6)"),
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
    m_statuses[idx].fixHint = fixHint;
    emit statusesChanged();
}

void ServiceSupervisor::refreshAll()
{
    const QString piper = m_paths.piperExecutable();
    if (piper.isEmpty()) {
        setStatus(QStringLiteral("piper"), ServiceHealth::Warning,
                  QStringLiteral("Piper executable not found"),
                  QStringLiteral("Place piper under %1/piper or configure tts.piper_exe.")
                      .arg(QDir::toNativeSeparators(m_paths.toolsDir())));
    } else {
        setStatus(QStringLiteral("piper"), ServiceHealth::Ok, QDir::toNativeSeparators(piper), {});
    }

    // "Refresh checks" also retries a whisper-server that failed (e.g. after the
    // model file was copied in while Eagle was running).
    if (m_whisperStarted
        && (m_whisper->state() == WhisperServerProcess::State::Failed
            || m_whisper->state() == WhisperServerProcess::State::Stopped)) {
        m_whisper->start(SttConfig::serverSettings(m_config, m_paths, m_sttLanguage));
    }
    updateWhisperRow();
    probeOllama();
}

void ServiceSupervisor::startManagedServices(const QString &sttLanguage)
{
    m_sttLanguage = SttConfig::whisperLanguageFor(sttLanguage);
    m_whisperStarted = true;
    m_whisper->start(SttConfig::serverSettings(m_config, m_paths, m_sttLanguage));
    updateWhisperRow();
}

void ServiceSupervisor::stopAll()
{
    if (m_whisper) {
        m_whisper->stop();
    }
}

void ServiceSupervisor::setSttLanguage(const QString &language)
{
    const QString lang = SttConfig::whisperLanguageFor(language);
    if (lang == m_sttLanguage) {
        return;
    }
    m_sttLanguage = lang;
    const bool restart =
        m_config.valueAt({QStringLiteral("stt"), QStringLiteral("restart_on_language_change")}).toBool(false);
    if (restart && m_whisperStarted) {
        m_whisper->restart(SttConfig::serverSettings(m_config, m_paths, m_sttLanguage));
    }
}

QUrl ServiceSupervisor::whisperUrl() const
{
    return QUrl(QStringLiteral("http://%1:%2").arg(m_config.sttHost()).arg(m_config.sttPort()));
}

bool ServiceSupervisor::isWhisperReady() const
{
    return m_whisper->state() == WhisperServerProcess::State::Ready;
}

WhisperServerProcess::State ServiceSupervisor::whisperState() const
{
    return m_whisper->state();
}

QString ServiceSupervisor::whisperDetail() const
{
    return m_whisper->detail();
}

void ServiceSupervisor::onWhisperStateChanged(WhisperServerProcess::State state, const QString &detail)
{
    updateWhisperRow();
    emit whisperStateChanged(state == WhisperServerProcess::State::Ready, detail);
}

void ServiceSupervisor::updateWhisperRow()
{
    const QString exe = m_paths.whisperServer();
    const QString model = m_paths.whisperModel();
    const QString tools = QDir::toNativeSeparators(m_paths.toolsDir());
    if (exe.isEmpty()) {
        setStatus(QStringLiteral("whisper"), ServiceHealth::Error,
                  QStringLiteral("whisper-server not found under %1").arg(tools),
                  QStringLiteral("Copy whisper-server (with whisper.dll and the ggml*.dll files) into "
                                 "tools/whisper/Release/, or set stt.server_exe."));
        return;
    }
    if (model.isEmpty()) {
        setStatus(QStringLiteral("whisper"), ServiceHealth::Error,
                  QStringLiteral("No whisper model found (looked for %1 and %2 in tools/whisper/models).")
                      .arg(m_config.sttModel(), m_config.sttFallbackModel()),
                  QStringLiteral("Download ggml-base.bin (faster) or ggml-small-q5_1.bin (better "
                                 "Hindi) into tools/whisper/models/, then press Refresh checks."));
        return;
    }

    QString files = QStringLiteral("%1\n%2").arg(QDir::toNativeSeparators(exe),
                                                 QDir::toNativeSeparators(model));
    if (QFileInfo(model).fileName() != m_config.sttModel()) {
        files += QStringLiteral("\nUsing the fallback model; %1 was not found.").arg(m_config.sttModel());
    }
    const QString detail = m_whisper->detail().isEmpty() ? QStringLiteral("Not started yet")
                                                        : m_whisper->detail();
    switch (m_whisper->state()) {
    case WhisperServerProcess::State::Ready:
        setStatus(QStringLiteral("whisper"), ServiceHealth::Ok, detail + QLatin1Char('\n') + files, {});
        break;
    case WhisperServerProcess::State::Starting:
        setStatus(QStringLiteral("whisper"), ServiceHealth::Checking,
                  detail + QLatin1Char('\n') + files, {});
        break;
    case WhisperServerProcess::State::Failed:
        setStatus(QStringLiteral("whisper"), ServiceHealth::Error, detail + QLatin1Char('\n') + files,
                  QStringLiteral("See whisper-server.log in the log folder, fix the cause, then "
                                 "press Refresh checks to start it again."));
        break;
    case WhisperServerProcess::State::Stopped:
        setStatus(QStringLiteral("whisper"), ServiceHealth::Warning, detail + QLatin1Char('\n') + files,
                  QStringLiteral("Press Refresh checks to start it."));
        break;
    }
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
