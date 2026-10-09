#include "PiperCliBackend.h"

#include "ChildProcessGuard.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRandomGenerator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QtEndian>

Q_LOGGING_CATEGORY(lcPiper, "eagle.piper")

namespace {

struct WavInfo {
    QByteArray pcm;
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    qint64 fileBytes = 0;
};

WavInfo readWavPcm(const QString &path)
{
    WavInfo info;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcPiper) << "cannot open WAV" << path << file.errorString();
        return info;
    }
    const QByteArray data = file.readAll();
    info.fileBytes = data.size();
    if (data.size() < 44 || !data.startsWith("RIFF") || data.mid(8, 4) != "WAVE") {
        qCWarning(lcPiper) << "not a RIFF/WAVE file" << path << "bytes" << data.size();
        return info;
    }

    int offset = 12;
    int dataOffset = -1;
    int dataSize = 0;
    while (offset + 8 <= data.size()) {
        const QByteArray id = data.mid(offset, 4);
        const quint32 chunkSize = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(data.constData() + offset + 4));
        const int payload = offset + 8;
        if (id == "fmt " && payload + 16 <= data.size()) {
            info.channels = qFromLittleEndian<quint16>(
                reinterpret_cast<const uchar *>(data.constData() + payload + 2));
            info.sampleRate = static_cast<int>(qFromLittleEndian<quint32>(
                reinterpret_cast<const uchar *>(data.constData() + payload + 4)));
            info.bitsPerSample = qFromLittleEndian<quint16>(
                reinterpret_cast<const uchar *>(data.constData() + payload + 14));
        } else if (id == "data") {
            dataOffset = payload;
            dataSize = static_cast<int>(qMin<quint32>(chunkSize, quint32(data.size() - payload)));
            break;
        }
        offset = payload + static_cast<int>(chunkSize) + (chunkSize & 1);
    }
    if (dataOffset < 0) {
        qCWarning(lcPiper) << "WAV has no data chunk" << path;
        return info;
    }
    info.pcm = data.mid(dataOffset, dataSize);
    qCInfo(lcPiper) << "WAV" << path << "fileBytes" << info.fileBytes << "pcmBytes" << info.pcm.size()
                    << "rate" << info.sampleRate << "ch" << info.channels
                    << "bits" << info.bitsPerSample;
    return info;
}

QString normalizeStdoutPath(const QByteArray &line)
{
    QString path = QString::fromUtf8(line).trimmed();
    if (path.startsWith(QLatin1Char('"'))) {
        path = path.mid(1, path.size() - 2);
    }
    return path;
}

} // namespace

PiperCliBackend::PiperCliBackend(QObject *parent)
    : QObject(parent)
{
    m_outputDir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                        .filePath(QStringLiteral("eaglegd_piper"));
    QDir().mkpath(m_outputDir);
    m_jobTimer.setSingleShot(true);
    m_jobTimer.setInterval(45000);
    connect(&m_jobTimer, &QTimer::timeout, this, &PiperCliBackend::onJobTimeout);
}

PiperCliBackend::~PiperCliBackend()
{
    stopWorker();
}

void PiperCliBackend::configure(const QString &executable, const QString &modelOnnx, double lengthScale)
{
    const bool scaleChanged = qAbs(m_lengthScale - lengthScale) > 0.001;
    m_executable = executable;
    m_modelOnnx = modelOnnx;
    m_configJson = modelOnnx + QStringLiteral(".json");
    m_lengthScale = lengthScale;
    if (scaleChanged && m_process) {
        stopWorker();
    }
}

bool PiperCliBackend::isConfigured() const
{
    return !m_executable.isEmpty() && QFile::exists(m_executable) && QFile::exists(m_modelOnnx);
}

bool PiperCliBackend::isWorkerRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool PiperCliBackend::hasPendingJobs() const
{
    return m_waitingForPath || !m_queue.isEmpty();
}

void PiperCliBackend::startWorker()
{
    if (!isConfigured() || isWorkerRunning()) {
        return;
    }
    m_jobsCompletedOnWorker = 0;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setWorkingDirectory(QFileInfo(m_executable).absolutePath());
    connect(m_process, &QProcess::readyReadStandardOutput, this, &PiperCliBackend::onStdoutReady);
    connect(m_process, &QProcess::readyReadStandardError, this, &PiperCliBackend::onStderrReady);
    connect(m_process, &QProcess::finished, this, &PiperCliBackend::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &PiperCliBackend::onProcessError);

    const QStringList args = {
        QStringLiteral("--model"), m_modelOnnx,
        QStringLiteral("--config"), m_configJson,
        QStringLiteral("--json-input"),
        QStringLiteral("--output_dir"), m_outputDir,
        QStringLiteral("--length_scale"), QString::number(m_lengthScale, 'f', 2),
        QStringLiteral("--sentence_silence"), QStringLiteral("0"),
    };
    ChildProcessGuard::prepare(m_process);
    m_process->start(m_executable, args);
    if (!m_process->waitForStarted(8000)) {
        qCWarning(lcPiper) << "Piper worker failed to start:" << m_process->errorString();
        m_process->deleteLater();
        m_process = nullptr;
    } else {
        ChildProcessGuard::adopt(m_process);  // dies with Eagle, even after a crash
        qCInfo(lcPiper) << "Piper worker started" << m_modelOnnx;
    }
}

void PiperCliBackend::stopWorker()
{
    m_jobTimer.stop();
    m_queue.clear();
    m_waitingForPath = false;
    m_stdoutBuffer.clear();
    if (m_process) {
        m_process->disconnect(this);
        m_process->terminate();
        m_process->waitForFinished(2000);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
}

QString PiperCliBackend::nextOutputPath() const
{
    return QDir(m_outputDir).filePath(
        QStringLiteral("job_%1_%2.wav")
            .arg(QDateTime::currentMSecsSinceEpoch())
            .arg(QRandomGenerator::global()->generate()));
}

void PiperCliBackend::synthesize(int turnId, int sentenceIndex, const QString &text)
{
    if (!isConfigured() || text.trimmed().isEmpty()) {
        emit synthesisFailed(turnId, sentenceIndex, QStringLiteral("Piper not configured"));
        return;
    }
    Job job;
    job.turnId = turnId;
    job.sentenceIndex = sentenceIndex;
    job.text = text;
    job.outputPath = nextOutputPath();
    job.queuedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_queue.enqueue(job);
    startWorker();
    pumpQueue();
}

void PiperCliBackend::pumpQueue()
{
    if (m_waitingForPath || m_queue.isEmpty() || !m_process) {
        return;
    }
    m_activeJob = m_queue.head();
    m_waitingForPath = true;
    m_jobStartedMs = QDateTime::currentMSecsSinceEpoch();

    QJsonObject line;
    line.insert(QStringLiteral("text"), m_activeJob.text);
    QString outPath = m_activeJob.outputPath;
    outPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    line.insert(QStringLiteral("output_file"), outPath);
    const QByteArray payload = QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n';
    const qint64 written = m_process->write(payload);
    if (written != payload.size()) {
        failFrontJob(QStringLiteral("Failed to write to Piper stdin"));
        return;
    }
    m_jobTimer.start();
}

void PiperCliBackend::onStderrReady()
{
    if (!m_process) {
        return;
    }
    const QByteArray err = m_process->readAllStandardError();
    if (!err.trimmed().isEmpty()) {
        qCWarning(lcPiper) << "stderr:" << err.trimmed();
    }
}

void PiperCliBackend::onJobTimeout()
{
    if (!m_waitingForPath) {
        return;
    }
    qCWarning(lcPiper) << "Piper job timed out waiting for WAV path";
    failFrontJob(QStringLiteral("Piper synthesis timed out"));
}

void PiperCliBackend::onStdoutReady()
{
    m_stdoutBuffer.append(m_process->readAllStandardOutput());
    int newline = -1;
    while ((newline = m_stdoutBuffer.indexOf('\n')) != -1) {
        const QByteArray line = m_stdoutBuffer.left(newline);
        m_stdoutBuffer.remove(0, newline + 1);
        const QString path = normalizeStdoutPath(line);
        if (path.isEmpty()) {
            continue;
        }
        if (m_waitingForPath) {
            completeJob(path);
        }
    }
}

void PiperCliBackend::completeJob(const QString &wavPath)
{
    m_jobTimer.stop();
    m_waitingForPath = false;
    if (!m_queue.isEmpty()) {
        m_queue.dequeue();
    }
    const qint64 synthMs = QDateTime::currentMSecsSinceEpoch() - m_jobStartedMs;
    ++m_jobsCompletedOnWorker;
    if (m_jobsCompletedOnWorker == 1) {
        m_lastColdSynthMs = synthMs;
    } else {
        m_lastWarmSynthMs = synthMs;
    }

    QString path = wavPath;
    if (!QFile::exists(path) && QFile::exists(m_activeJob.outputPath)) {
        path = m_activeJob.outputPath;
    }
    const WavInfo wav = readWavPcm(path);
    const QString lastCopy = QDir(m_outputDir).filePath(QStringLiteral("last.wav"));
    if (QFile::exists(path)) {
        QFile::remove(lastCopy);
        QFile::copy(path, lastCopy);
    }
    if (wav.pcm.isEmpty() || !QFile::exists(path)) {
        qCWarning(lcPiper) << "Empty or unreadable WAV" << path;
        QFile::remove(path);
        emit synthesisFailed(m_activeJob.turnId, m_activeJob.sentenceIndex,
                             QStringLiteral("Empty WAV from Piper"));
    } else {
        qCInfo(lcPiper) << "synth ms=" << synthMs << "cold=" << m_lastColdSynthMs
                        << "warm=" << m_lastWarmSynthMs << "wav" << path << "copied" << lastCopy;
        emit wavReady(m_activeJob.turnId, m_activeJob.sentenceIndex, path, synthMs);
    }
    pumpQueue();
}

void PiperCliBackend::failFrontJob(const QString &error)
{
    m_jobTimer.stop();
    m_waitingForPath = false;
    if (m_queue.isEmpty()) {
        return;
    }
    const Job job = m_queue.dequeue();
    emit synthesisFailed(job.turnId, job.sentenceIndex, error);
    pumpQueue();
}

void PiperCliBackend::onProcessFinished(int exitCode, QProcess::ExitStatus)
{
    qCWarning(lcPiper) << "Piper worker exited" << exitCode;
    const QString err = m_process ? QString::fromUtf8(m_process->readAllStandardError()) : QString();
    m_process->deleteLater();
    m_process = nullptr;
    m_stdoutBuffer.clear();
    if (m_waitingForPath && !m_queue.isEmpty()) {
        failFrontJob(err.isEmpty() ? QStringLiteral("Piper worker exited") : err);
    }
    m_waitingForPath = false;
    if (!m_queue.isEmpty()) {
        startWorker();
        pumpQueue();
    }
}

void PiperCliBackend::onProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        failFrontJob(QStringLiteral("Piper failed to start"));
    }
}
