#include "speechinput.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMediaDevices>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QVector>
#include <QtMath>

#include <cstring>

namespace {

void appendLe16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void appendLe32(QByteArray &out, quint32 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
    out.append(char((value >> 16) & 0xFF));
    out.append(char((value >> 24) & 0xFF));
}

QString firstExisting(const QStringList &paths)
{
    for (const QString &path : paths) {
        if (QFileInfo::exists(path)) {
            return QFileInfo(path).absoluteFilePath();
        }
    }
    return {};
}

float sampleAt(const char *bytes, QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8: {
        const auto value = static_cast<quint8>(*bytes);
        return (value - 128) / 128.0f;
    }
    case QAudioFormat::Int16: {
        qint16 value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return value / 32768.0f;
    }
    case QAudioFormat::Int32: {
        qint32 value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return value / 2147483648.0f;
    }
    case QAudioFormat::Float: {
        float value = 0.0f;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }
    default:
        return 0.0f;
    }
}

} // namespace

SpeechInput::SpeechInput(QObject *parent)
    : QObject(parent)
    , m_device(QMediaDevices::defaultAudioInput())
    , m_partialTimer(new QTimer(this))
{
    m_partialTimer->setInterval(2000);
    connect(m_partialTimer, &QTimer::timeout, this, &SpeechInput::onPartialTick);
}

SpeechInput::~SpeechInput()
{
    if (m_whisper) {
        m_whisper->kill();
        m_whisper->waitForFinished(500);
    }
    if (m_source) {
        m_source->stop();
    }
}

QList<QAudioDevice> SpeechInput::microphones() const
{
    return QMediaDevices::audioInputs();
}

void SpeechInput::setMicrophone(const QAudioDevice &device)
{
    if (m_recording) {
        return;
    }
    m_device = device;
}

bool SpeechInput::isRecording() const
{
    return m_recording;
}

bool SpeechInput::isBusy() const
{
    return m_recording || m_whisper != nullptr;
}

void SpeechInput::setWhisperPaths(const QString &cliPath, const QString &modelPath)
{
    m_whisperCliOverride = cliPath;
    m_whisperModelOverride = modelPath;
}

void SpeechInput::startRecording()
{
    if (isBusy()) {
        return;
    }
    if (m_device.isNull()) {
        emit errorOccurred(QStringLiteral("No microphone selected."));
        return;
    }
    if (whisperCliPath().isEmpty() || whisperModelPath().isEmpty()) {
        emit errorOccurred(QStringLiteral(
            "whisper.cpp was not found. Place whisper-cli and ggml-base.bin under tools/whisper/ "
            "(see Readiness panel)."));
        return;
    }

    QAudioFormat preferred;
    preferred.setSampleRate(16000);
    preferred.setChannelCount(1);
    preferred.setSampleFormat(QAudioFormat::Int16);
    m_format = m_device.isFormatSupported(preferred) ? preferred : m_device.preferredFormat();

    m_captured.clear();
    m_source = new QAudioSource(m_device, m_format, this);
    m_capture = m_source->start();
    if (!m_capture) {
        m_source->deleteLater();
        m_source = nullptr;
        emit errorOccurred(QStringLiteral("Could not open the selected microphone."));
        return;
    }

    connect(m_capture, &QIODevice::readyRead, this, &SpeechInput::onAudioReady);
    m_recording = true;
    m_finalPass = false;
    m_finalQueued = false;
    m_partialTimer->start();
    emit recordingChanged(true);
    emit statusChanged(QStringLiteral("Listening…"));
}

void SpeechInput::stopRecording()
{
    if (!m_recording) {
        return;
    }

    if (m_capture) {
        m_captured.append(m_capture->readAll());
    }
    if (m_source) {
        m_source->stop();
        m_source->deleteLater();
        m_source = nullptr;
    }
    m_capture = nullptr;
    m_recording = false;
    m_partialTimer->stop();
    emit recordingChanged(false);

    if (m_whisper) {
        m_finalQueued = true;
        emit statusChanged(QStringLiteral("Transcribing…"));
        return;
    }
    startWhisper(true);
}

void SpeechInput::onPartialTick()
{
    if (!m_recording || m_whisper) {
        return;
    }
    startWhisper(false);
}

void SpeechInput::startWhisper(bool finalPass)
{
    const QByteArray pcm = toPcm16kMono(m_captured);
    if (pcm.size() < 8000) {
        if (finalPass) {
            emit statusChanged(QStringLiteral("Ready"));
            emit errorOccurred(QStringLiteral("Recording was too short. Speak a little longer, then stop."));
        }
        return;
    }

    const QString wavPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                .filePath(finalPass ? QStringLiteral("eaglegd-stt-final.wav")
                                                    : QStringLiteral("eaglegd-stt-live.wav"));
    if (!writeWavFile(wavPath, pcm)) {
        if (finalPass) {
            emit statusChanged(QStringLiteral("Ready"));
            emit errorOccurred(QStringLiteral("Could not save the recording."));
        }
        return;
    }

    m_finalPass = finalPass;
    m_wavPath = wavPath;
    if (finalPass) {
        emit statusChanged(QStringLiteral("Transcribing…"));
    }

    m_whisper = new QProcess(this);
    m_whisper->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_whisper, &QProcess::finished, this, &SpeechInput::onWhisperFinished);

    QStringList args = {
        QStringLiteral("-m"), whisperModelPath(),
        QStringLiteral("-f"), wavPath,
        QStringLiteral("-l"), QStringLiteral("auto"),
        QStringLiteral("-nt"),
        QStringLiteral("-np"),
        QStringLiteral("-t"), QStringLiteral("4"),
        QStringLiteral("-bs"), QStringLiteral("1"),
        QStringLiteral("-bo"), QStringLiteral("1"),
    };
    m_whisper->start(whisperCliPath(), args);
    if (!m_whisper->waitForStarted(3000)) {
        const QString message = m_whisper->errorString();
        m_whisper->deleteLater();
        m_whisper = nullptr;
        if (finalPass) {
            emit statusChanged(QStringLiteral("Ready"));
            emit errorOccurred(QStringLiteral("Could not start whisper.cpp: %1").arg(message));
        }
    }
}

void SpeechInput::onAudioReady()
{
    if (m_capture) {
        m_captured.append(m_capture->readAll());
    }
}

QString SpeechInput::textFromWhisperOutput(const QString &stdoutText)
{
    QStringList lines;
    for (const QString &line : stdoutText.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            lines.append(trimmed);
        }
    }
    return lines.join(QLatin1Char(' ')).trimmed();
}

void SpeechInput::onWhisperFinished(int exitCode, QProcess::ExitStatus)
{
    const bool finalPass = m_finalPass;
    const QString stdoutText = QString::fromUtf8(m_whisper->readAllStandardOutput()).trimmed();
    const QString stderrText = QString::fromUtf8(m_whisper->readAllStandardError()).trimmed();
    m_whisper->deleteLater();
    m_whisper = nullptr;
    QFile::remove(m_wavPath);

    if (m_finalQueued) {
        m_finalQueued = false;
        startWhisper(true);
        return;
    }

    if (exitCode != 0) {
        if (finalPass || !m_recording) {
            emit statusChanged(QStringLiteral("Ready"));
            emit errorOccurred(stderrText.isEmpty()
                                   ? QStringLiteral("whisper.cpp failed (exit %1).").arg(exitCode)
                                   : stderrText);
        }
        return;
    }

    const QString text = textFromWhisperOutput(stdoutText);
    if (finalPass) {
        m_captured.clear();
        emit statusChanged(QStringLiteral("Ready"));
        if (text.isEmpty()) {
            emit errorOccurred(QStringLiteral("No speech detected."));
            return;
        }
        emit transcriptionReady(text);
        return;
    }

    if (m_recording && !text.isEmpty()) {
        emit partialTranscription(text);
        emit statusChanged(QStringLiteral("Listening…"));
    }
}

QString SpeechInput::whisperCliPath() const
{
    if (!m_whisperCliOverride.isEmpty() && QFileInfo::exists(m_whisperCliOverride)) {
        return QFileInfo(m_whisperCliOverride).absoluteFilePath();
    }
    return m_whisperCliOverride;
}

QString SpeechInput::whisperModelPath() const
{
    if (!m_whisperModelOverride.isEmpty() && QFileInfo::exists(m_whisperModelOverride)) {
        return QFileInfo(m_whisperModelOverride).absoluteFilePath();
    }
    return m_whisperModelOverride;
}

bool SpeechInput::writeWavFile(const QString &path, const QByteArray &pcm16k) const
{
    QByteArray header;
    header.append("RIFF", 4);
    appendLe32(header, 36 + static_cast<quint32>(pcm16k.size()));
    header.append("WAVE", 4);
    header.append("fmt ", 4);
    appendLe32(header, 16);
    appendLe16(header, 1);
    appendLe16(header, 1);
    appendLe32(header, 16000);
    appendLe32(header, 16000 * 2);
    appendLe16(header, 2);
    appendLe16(header, 16);
    header.append("data", 4);
    appendLe32(header, static_cast<quint32>(pcm16k.size()));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(header) == header.size() && file.write(pcm16k) == pcm16k.size();
}

QByteArray SpeechInput::toPcm16kMono(const QByteArray &raw) const
{
    const int channels = qMax(1, m_format.channelCount());
    const int bytesPerSample = qMax(1, m_format.bytesPerSample());
    const int frameBytes = bytesPerSample * channels;
    if (frameBytes <= 0 || raw.size() < frameBytes) {
        return {};
    }

    const int frameCount = raw.size() / frameBytes;
    QVector<float> mono;
    mono.reserve(frameCount);
    const char *data = raw.constData();
    for (int frame = 0; frame < frameCount; ++frame) {
        float sum = 0.0f;
        for (int channel = 0; channel < channels; ++channel) {
            sum += sampleAt(data + frame * frameBytes + channel * bytesPerSample, m_format.sampleFormat());
        }
        mono.append(sum / channels);
    }

    const int srcRate = qMax(1, m_format.sampleRate());
    const int dstCount = qMax(1, qRound(mono.size() * (16000.0 / srcRate)));
    QByteArray pcm(dstCount * 2, '\0');
    auto *dst = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < dstCount; ++i) {
        const double position = i * (static_cast<double>(srcRate) / 16000.0);
        const int index = qMin(static_cast<int>(position), mono.size() - 1);
        const int next = qMin(index + 1, mono.size() - 1);
        const float fraction = static_cast<float>(position - index);
        float sample = mono[index] * (1.0f - fraction) + mono[next] * fraction;
        sample = qBound(-1.0f, sample, 1.0f);
        dst[i] = static_cast<qint16>(sample * 32767.0f);
    }
    return pcm;
}
