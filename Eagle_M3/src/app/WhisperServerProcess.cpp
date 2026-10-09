#include "WhisperServerProcess.h"

#include "ChildProcessGuard.h"
#include "stt/WhisperServerClient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcWhisperServer, "eagle.stt.server")

namespace {

constexpr int kMaxCrashesPerWindow = 5;
constexpr qint64 kCrashWindowMs = 5 * 60 * 1000;

} // namespace

WhisperServerProcess::WhisperServerProcess(QObject *parent)
    : QObject(parent)
    , m_health(new WhisperServerClient(this))
{
    connect(m_health, &WhisperServerClient::healthChecked, this, &WhisperServerProcess::onHealthChecked);
    m_healthTimer.setInterval(300);
    connect(&m_healthTimer, &QTimer::timeout, this, &WhisperServerProcess::pollHealth);
    m_restartTimer.setSingleShot(true);
    connect(&m_restartTimer, &QTimer::timeout, this, &WhisperServerProcess::launch);
    m_uptime.start();
}

WhisperServerProcess::~WhisperServerProcess()
{
    m_stopping = true;
    killProcess(true);
}

QUrl WhisperServerProcess::baseUrl() const
{
    return QUrl(QStringLiteral("http://%1:%2").arg(m_settings.host).arg(m_settings.port));
}

QStringList WhisperServerProcess::buildArguments(const Settings &s)
{
    QStringList args = {
        QStringLiteral("-m"), QDir::toNativeSeparators(s.model),
        QStringLiteral("--host"), s.host,
        QStringLiteral("--port"), QString::number(s.port),
        QStringLiteral("-t"), QString::number(qMax(1, s.threads)),
        QStringLiteral("-l"), s.language.isEmpty() ? QStringLiteral("en") : s.language,
    };
    args += s.extraArgs;
    return args;
}

void WhisperServerProcess::setState(State state, const QString &detail)
{
    if (m_state == state && m_detail == detail) {
        return;
    }
    m_state = state;
    m_detail = detail;
    qCInfo(lcWhisperServer) << state << detail;
    emit stateChanged(state, detail);
}

void WhisperServerProcess::start(const Settings &settings)
{
    m_settings = settings;
    m_stopping = false;
    if (m_process || m_probingExisting || m_restartTimer.isActive()) {
        return;
    }
    m_adopted = false;
    m_crashTimes.clear();
    m_backoffMs = 1000;
    if (settings.exe.isEmpty() || !QFileInfo::exists(settings.exe)) {
#ifdef Q_OS_WIN
        const QString exeName = QStringLiteral("whisper-server.exe and its DLLs");
#else
        const QString exeName = QStringLiteral("whisper-server");
#endif
        const QString where = settings.exe.isEmpty() ? QString() : QStringLiteral(" at ") + settings.exe;
        setState(State::Failed,
                 QStringLiteral("whisper-server not found%1. Put %2 in tools/whisper/Release/, or set "
                                "stt.server_exe in the config.")
                     .arg(where, exeName));
        return;
    }
    if (settings.model.isEmpty() || !QFileInfo::exists(settings.model)) {
        setState(State::Failed,
                 QStringLiteral("Whisper model not found. Put ggml-base.bin or ggml-small-q5_1.bin "
                                "in tools/whisper/models/."));
        return;
    }
    setState(State::Starting, QStringLiteral("Checking port %1…").arg(settings.port));
    // An orphan server from an earlier crash (or one started by hand) may already
    // hold the port: reuse it instead of failing to bind.
    m_probingExisting = true;
    m_healthInFlight = true;
    m_health->setBaseUrl(baseUrl());
    m_health->checkHealth(700);
}

void WhisperServerProcess::launch()
{
    if (m_process || m_stopping) {
        return;
    }
    m_process = new QProcess(this);
    m_process->setProgram(m_settings.exe);
    m_process->setArguments(buildArguments(m_settings));
    m_process->setWorkingDirectory(QFileInfo(m_settings.exe).absolutePath());
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    if (!m_settings.logFile.isEmpty()) {
        QDir().mkpath(QFileInfo(m_settings.logFile).absolutePath());
        m_process->setStandardOutputFile(m_settings.logFile, QIODevice::Truncate);
    } else {
        m_process->setStandardOutputFile(QProcess::nullDevice());
    }
    ChildProcessGuard::prepare(m_process, true);
    connect(m_process, &QProcess::started, this, [this]() {
        ChildProcessGuard::adopt(m_process);
        qCInfo(lcWhisperServer) << "started pid" << (m_process ? m_process->processId() : 0);
    });
    connect(m_process, &QProcess::finished, this, &WhisperServerProcess::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, &WhisperServerProcess::onProcessError);

    m_health->setBaseUrl(baseUrl());
    m_startClock.start();
    setState(State::Starting,
             QStringLiteral("Loading %1…").arg(QFileInfo(m_settings.model).fileName()));
    qCInfo(lcWhisperServer) << "launch" << m_settings.exe << m_process->arguments();
    m_process->start();
    m_healthTimer.start();
}

void WhisperServerProcess::pollHealth()
{
    if (!m_process || m_healthInFlight) {
        return;
    }
    m_healthInFlight = true;
    m_health->checkHealth(800);
}

void WhisperServerProcess::onHealthChecked(bool ok, int httpStatus, const QString &detail)
{
    m_healthInFlight = false;
    if (m_probingExisting) {
        m_probingExisting = false;
        if (m_stopping) {
            return;
        }
        if (ok) {
            m_adopted = true;
            setState(State::Ready,
                     QStringLiteral("Using a whisper-server that was already running on port %1 "
                                    "(not started by Eagle).")
                         .arg(m_settings.port));
            return;
        }
        launch();
        return;
    }
    if (m_state != State::Starting || !m_process) {
        return;
    }
    if (ok) {
        m_healthTimer.stop();
        m_backoffMs = 1000;
        setState(State::Ready,
                 QStringLiteral("Ready in %1 s · %2 · %3 threads · language %4")
                     .arg(double(m_startClock.elapsed()) / 1000.0, 0, 'f', 1)
                     .arg(QFileInfo(m_settings.model).fileName())
                     .arg(m_settings.threads)
                     .arg(m_settings.language));
        return;
    }
    if (m_startClock.elapsed() > m_settings.startTimeoutMs) {
        m_healthTimer.stop();
        setState(State::Failed,
                 QStringLiteral("whisper-server did not become ready within %1 s (last: %2). %3")
                     .arg(m_settings.startTimeoutMs / 1000)
                     .arg(httpStatus ? QString::number(httpStatus) : detail)
                     .arg(logTail(300)));
        m_process->kill();  // onFinished sees Failed and does not restart
    }
}

void WhisperServerProcess::onProcessError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart || !m_process) {
        return;  // crashes are handled in onFinished
    }
    m_healthTimer.stop();
    const QString reason = m_process->errorString();
    m_process->deleteLater();
    m_process = nullptr;
    setState(State::Failed, QStringLiteral("Could not start whisper-server: %1").arg(reason));
}

void WhisperServerProcess::onFinished(int exitCode, QProcess::ExitStatus status)
{
    m_healthTimer.stop();
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (m_stopping) {
        setState(State::Stopped, QStringLiteral("Stopped"));
        return;
    }
    if (m_restartPending) {
        m_restartPending = false;
        launch();
        return;
    }
    if (m_state == State::Failed) {
        return;
    }
    const QString tail = logTail();
    if (tail.contains(QStringLiteral("couldn't bind"), Qt::CaseInsensitive)) {
        setState(State::Failed,
                 QStringLiteral("Port %1 is used by another program. Close it, or change stt.port "
                                "in the config.")
                     .arg(m_settings.port));
        return;
    }
    if (tail.contains(QStringLiteral("unknown argument"), Qt::CaseInsensitive)
        || tail.contains(QStringLiteral("failed to initialize whisper context"), Qt::CaseInsensitive)
        || tail.contains(QStringLiteral("failed to load model"), Qt::CaseInsensitive)) {
        setState(State::Failed, QStringLiteral("whisper-server stopped: %1").arg(tail.right(300)));
        return;
    }
    scheduleRestart(status == QProcess::CrashExit ? QStringLiteral("crashed")
                                                  : QStringLiteral("exit code %1").arg(exitCode));
}

void WhisperServerProcess::scheduleRestart(const QString &why)
{
    const qint64 now = m_uptime.elapsed();
    while (!m_crashTimes.isEmpty() && now - m_crashTimes.first() > kCrashWindowMs) {
        m_crashTimes.removeFirst();
    }
    m_crashTimes.append(now);
    if (m_crashTimes.size() > kMaxCrashesPerWindow) {
        setState(State::Failed,
                 QStringLiteral("whisper-server stopped %1 times in 5 minutes (%2). %3")
                     .arg(m_crashTimes.size())
                     .arg(why, logTail(300)));
        return;
    }
    setState(State::Starting, QStringLiteral("whisper-server stopped (%1); restarting in %2 s")
                                  .arg(why)
                                  .arg(m_backoffMs / 1000));
    m_restartTimer.start(m_backoffMs);
    m_backoffMs = qMin(m_backoffMs * 2, 30000);
}

void WhisperServerProcess::restart(const Settings &settings)
{
    if (m_adopted) {
        m_settings.language = settings.language;
        qCInfo(lcWhisperServer) << "restart skipped: using a server Eagle did not start";
        return;
    }
    m_settings = settings;
    m_restartTimer.stop();
    if (m_process) {
        m_restartPending = true;
        m_healthTimer.stop();
        setState(State::Starting, QStringLiteral("Switching listening language…"));
        m_process->kill();
        return;
    }
    start(settings);
}

void WhisperServerProcess::stop()
{
    m_stopping = true;
    m_restartTimer.stop();
    m_healthTimer.stop();
    killProcess(true);
    setState(State::Stopped, QStringLiteral("Stopped"));
}

void WhisperServerProcess::killProcess(bool wait)
{
    if (!m_process) {
        return;
    }
    QProcess *process = m_process;
    m_process = nullptr;
    disconnect(process, nullptr, this, nullptr);
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        if (wait) {
            process->waitForFinished(2000);  // exit path only
        }
    }
    process->deleteLater();
}

QString WhisperServerProcess::logTail(int maxChars) const
{
    if (m_settings.logFile.isEmpty()) {
        return {};
    }
    QFile file(m_settings.logFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const qint64 size = file.size();
    file.seek(qMax<qint64>(0, size - 4096));
    QString text = QString::fromLocal8Bit(file.readAll()).trimmed();
    text.replace(QLatin1Char('\r'), QString());
    QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    while (lines.size() > 6) {
        lines.removeFirst();
    }
    return lines.join(QStringLiteral(" | ")).right(maxChars);
}
