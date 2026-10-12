#include "ServiceSupervisor.h"

#include "AppConfig.h"
#include "Paths.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QUrl>

Q_LOGGING_CATEGORY(lcSupervisor, "eagle.supervisor")

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
{
    seedPlaceholders();
}

ServiceSupervisor::~ServiceSupervisor()
{
    stopWhisperServer();
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
         QStringLiteral("Not checked yet"),
         QStringLiteral("Place whisper-server.exe and ggml model under tools/whisper/.")},
        {QStringLiteral("piper"), QStringLiteral("Piper (TTS)"), ServiceHealth::Unknown,
         QStringLiteral("Not supervised in M0"),
         QStringLiteral("Place piper under tools/piper or configure tts.piper_exe.")},
        {QStringLiteral("robot"), QStringLiteral("Robot bridge"), ServiceHealth::Unknown,
         QStringLiteral("Not supervised in M0"),
         QStringLiteral("Optional: run robot_bridge on port 9100.")},
    };
}

void ServiceSupervisor::setStatus(const QString &id, ServiceHealth health, const QString &detail,
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

QUrl ServiceSupervisor::whisperBaseUrl() const
{
    return QUrl(QStringLiteral("http://%1:%2")
                  .arg(m_config.sttHost())
                  .arg(m_config.sttPort()));
}

void ServiceSupervisor::stopWhisperServer()
{
    if (!m_whisper) {
        return;
    }
    m_whisper->disconnect(this);
    m_whisper->terminate();
    if (!m_whisper->waitForFinished(2000)) {
        m_whisper->kill();
        m_whisper->waitForFinished(1000);
    }
    m_whisper->deleteLater();
    m_whisper = nullptr;
    m_whisperLang.clear();
    qCInfo(lcSupervisor) << "whisper-server stopped";
}

bool ServiceSupervisor::startWhisperProcess(const QString &languageCode)
{
    const QString exe = m_paths.whisperServerExe();
    const QString model = m_paths.resolvedWhisperModel();
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        qCWarning(lcSupervisor) << "whisper-server exe missing";
        return false;
    }
    if (model.isEmpty() || !QFileInfo::exists(model)) {
        qCWarning(lcSupervisor) << "whisper model missing" << model;
        return false;
    }

    stopWhisperServer();
    m_whisper = new QProcess(this);
    m_whisper->setWorkingDirectory(QFileInfo(exe).absolutePath());
    m_whisper->setProcessChannelMode(QProcess::SeparateChannels);

    const int ideal = QThread::idealThreadCount();
    const int defaultThreads = qMin(6, ideal / 2);
    const int threads =
        m_config.sttThreads() > 0 ? m_config.sttThreads() : qMax(1, defaultThreads);
    const QStringList args = {
        QStringLiteral("-m"), model,
        QStringLiteral("--host"), m_config.sttHost(),
        QStringLiteral("--port"), QString::number(m_config.sttPort()),
        QStringLiteral("-t"), QString::number(qMax(1, threads)),
        QStringLiteral("-l"), languageCode,
        QStringLiteral("--inference-path"), QStringLiteral("/inference"),
    };
    qCInfo(lcSupervisor) << "starting whisper-server threads" << threads << "args" << args;
    m_whisper->start(exe, args);
    if (!m_whisper->waitForStarted(8000)) {
        qCWarning(lcSupervisor) << "whisper-server failed to start" << m_whisper->errorString();
        stopWhisperServer();
        return false;
    }
    m_whisperLang = languageCode;
    return true;
}

bool ServiceSupervisor::ensureWhisperServer(const QString &languageCode)
{
    if (m_whisper && m_whisper->state() == QProcess::Running && m_whisperLang == languageCode) {
        return true;
    }
    if (!startWhisperProcess(languageCode)) {
        return false;
    }
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool ok = false;
    const auto probe = [this, &ok, &loop]() {
        QNetworkReply *reply = m_network->get(QNetworkRequest(whisperBaseUrl().resolved(QUrl("/health"))));
        connect(reply, &QNetworkReply::finished, &loop, [&]() {
            ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            loop.quit();
        });
    };
    timer.setInterval(4000);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    for (int i = 0; i < 8; ++i) {
        probe();
        timer.start();
        loop.exec();
        if (ok) {
            return true;
        }
        QThread::msleep(200);
    }
    return m_whisper && m_whisper->state() == QProcess::Running;
}

void ServiceSupervisor::probeWhisperHealth()
{
    const QString exe = m_paths.whisperServerExe();
    const QString model = m_paths.resolvedWhisperModel();
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        setStatus(QStringLiteral("whisper"), ServiceHealth::Error,
                  QStringLiteral("whisper-server.exe not found"),
                  QStringLiteral("Copy tools/whisper/Release/whisper-server.exe (see Paths)."));
        return;
    }
    if (model.isEmpty() || !QFileInfo::exists(model)) {
        setStatus(QStringLiteral("whisper"), ServiceHealth::Error,
                  QStringLiteral("Model file not found"),
                  QStringLiteral("Place ggml-base.bin or ggml-small-q5_1.bin under tools/whisper/models/."));
        return;
    }
    if (!m_whisper || m_whisper->state() != QProcess::Running) {
        setStatus(QStringLiteral("whisper"), ServiceHealth::Warning,
                  QStringLiteral("Binary and model OK; server starts on Listen (%1)").arg(model),
                  QStringLiteral("Press Listen or refresh after first use."));
        return;
    }
    setStatus(QStringLiteral("whisper"), ServiceHealth::Checking,
              QStringLiteral("Checking %1…").arg(whisperBaseUrl().toString()), {});
    QNetworkReply *reply =
        m_network->get(QNetworkRequest(whisperBaseUrl().resolved(QUrl("/health"))));
    connect(reply, &QNetworkReply::finished, this, &ServiceSupervisor::onWhisperHealthFinished);
}

void ServiceSupervisor::onWhisperHealthFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }
    reply->deleteLater();
    const QString model = m_paths.resolvedWhisperModel();
    if (reply->error() != QNetworkReply::NoError) {
        setStatus(QStringLiteral("whisper"), ServiceHealth::Warning,
                  QStringLiteral("Running but /health failed: %1").arg(reply->errorString()),
                  QStringLiteral("Check firewall; model %1").arg(model));
        return;
    }
    setStatus(QStringLiteral("whisper"), ServiceHealth::Ok,
              QStringLiteral("%1 | lang=%2 | %3")
                  .arg(m_paths.whisperServerExe(), m_whisperLang, model),
              {});
}

void ServiceSupervisor::refreshAll()
{
    seedPlaceholders();
    emit statusesChanged();

    const QString exe = m_paths.whisperServerExe();
    const QString model = m_paths.resolvedWhisperModel();
    if (!exe.isEmpty() && QFileInfo::exists(exe) && !model.isEmpty() && QFileInfo::exists(model)) {
        QString lang = m_config.sttLanguageMode();
        if (lang != QLatin1String("auto") && lang != QLatin1String("en") && lang != QLatin1String("hi")) {
            lang = QStringLiteral("auto");
        }
        ensureWhisperServer(lang);
    }
    probeWhisperHealth();

    const QString piper = m_paths.piperExecutable();
    if (piper.isEmpty()) {
        setStatus(QStringLiteral("piper"), ServiceHealth::Warning,
                  QStringLiteral("Piper executable not found"), m_paths.toolsDir());
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
