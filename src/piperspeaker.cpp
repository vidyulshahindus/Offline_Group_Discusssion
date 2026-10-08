#include "piperspeaker.h"

#include <QAudioOutput>
#include <QMediaDevices>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace {

QString firstExisting(const QStringList &paths)
{
    for (const QString &path : paths) {
        if (QFileInfo::exists(path)) {
            return QFileInfo(path).absoluteFilePath();
        }
    }
    return {};
}

} // namespace

PiperSpeaker::PiperSpeaker(QObject *parent)
    : QObject(parent)
    , m_player(new QMediaPlayer(this))
    , m_audio(new QAudioOutput(this))
{
    m_audio->setVolume(1.0f);
    m_player->setAudioOutput(m_audio);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &PiperSpeaker::onPlaybackStateChanged);
}

PiperSpeaker::~PiperSpeaker()
{
    stop();
}

bool PiperSpeaker::isBusy() const
{
    return m_piper != nullptr || m_playing;
}

QList<QAudioDevice> PiperSpeaker::speakers() const
{
    return QMediaDevices::audioOutputs();
}

void PiperSpeaker::setSpeaker(const QAudioDevice &device)
{
    if (!device.isNull()) {
        m_audio->setDevice(device);
    }
}

void PiperSpeaker::speak(const QString &text)
{
    const QString spoken = text.trimmed();
    if (spoken.isEmpty()) {
        return;
    }

    stop();

    const QString executable = piperExecutable();
    const QString model = voiceModelPath();
    if (executable.isEmpty() || model.isEmpty()) {
        emit errorOccurred(QStringLiteral(
            "Piper was not found. Expected tools/piper/piper/piper.exe and "
            "tools/piper/voices/en_US-lessac-medium.onnx."));
        return;
    }

    m_wavPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                    .filePath(QStringLiteral("ollama-chat-piper.wav"));
    QFile::remove(m_wavPath);

    m_piper = new QProcess(this);
    m_piper->setWorkingDirectory(QFileInfo(executable).absolutePath());
    m_piper->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_piper, &QProcess::finished, this, &PiperSpeaker::onPiperFinished);

    const QStringList args = {
        QStringLiteral("--model"), model,
        QStringLiteral("--output_file"), m_wavPath,
    };
    m_piper->start(executable, args);
    if (!m_piper->waitForStarted(3000)) {
        const QString message = m_piper->errorString();
        m_piper->deleteLater();
        m_piper = nullptr;
        emit errorOccurred(QStringLiteral("Could not start Piper: %1").arg(message));
        return;
    }

    m_piper->write(spoken.toUtf8());
    m_piper->write("\n");
    m_piper->closeWriteChannel();
    emit speakingChanged(true);
    emit statusChanged(QStringLiteral("Speaking…"));
}

void PiperSpeaker::stop()
{
    if (m_piper) {
        m_piper->disconnect(this);
        m_piper->kill();
        m_piper->deleteLater();
        m_piper = nullptr;
    }
    if (m_player->playbackState() != QMediaPlayer::StoppedState) {
        m_player->stop();
    }
    m_player->setSource(QUrl());
    m_playing = false;
    emit speakingChanged(false);
}

void PiperSpeaker::onPiperFinished(int exitCode, QProcess::ExitStatus)
{
    if (!m_piper) {
        return;
    }
    const QString stderrText = QString::fromUtf8(m_piper->readAllStandardError()).trimmed();
    m_piper->deleteLater();
    m_piper = nullptr;

    if (exitCode != 0 || !QFileInfo::exists(m_wavPath)) {
        emit speakingChanged(false);
        emit statusChanged(QStringLiteral("Ready"));
        emit errorOccurred(stderrText.isEmpty()
                               ? QStringLiteral("Piper could not create speech (exit %1).").arg(exitCode)
                               : stderrText);
        return;
    }

    playWav(m_wavPath);
}

void PiperSpeaker::playWav(const QString &path)
{
    m_playing = true;
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->play();
    emit speakingChanged(true);
    emit statusChanged(QStringLiteral("Speaking…"));
}

void PiperSpeaker::setPiperPaths(const QString &executable, const QString &voiceOnnxPath)
{
    m_piperExeOverride = executable;
    m_voiceOnnxOverride = voiceOnnxPath;
}

void PiperSpeaker::onPlaybackStateChanged()
{
    if (!m_playing) {
        return;
    }
    if (m_player->playbackState() == QMediaPlayer::StoppedState) {
        m_playing = false;
        emit speakingChanged(false);
        emit statusChanged(QStringLiteral("Ready"));
    }
}

QString PiperSpeaker::piperExecutable() const
{
    if (!m_piperExeOverride.isEmpty() && QFileInfo::exists(m_piperExeOverride)) {
        return QFileInfo(m_piperExeOverride).absoluteFilePath();
    }
    return m_piperExeOverride;
}

QString PiperSpeaker::voiceModelPath() const
{
    if (!m_voiceOnnxOverride.isEmpty() && QFileInfo::exists(m_voiceOnnxOverride)) {
        return QFileInfo(m_voiceOnnxOverride).absoluteFilePath();
    }
    return m_voiceOnnxOverride;
}
