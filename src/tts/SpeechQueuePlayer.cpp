#include "SpeechQueuePlayer.h"

#include <QAudioOutput>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QStandardPaths>
#include <QUrl>
#include <QtEndian>
#include <QtMath>

#include <cmath>

Q_LOGGING_CATEGORY(lcSpeech, "eagle.speech")

namespace {

constexpr int kEnvelopeHopMs = 50;

void appendLe16(QByteArray &out, quint16 v)
{
    char b[2];
    qToLittleEndian(v, b);
    out.append(b, 2);
}

void appendLe32(QByteArray &out, quint32 v)
{
    char b[4];
    qToLittleEndian(v, b);
    out.append(b, 4);
}

struct WavPcm {
    QByteArray samples;
    int sampleRate = 0;
    int channels = 1;
    int bits = 16;
    bool isFloat = false;
    qint64 durationMs = 0;
};

WavPcm readWav(const QString &path)
{
    WavPcm wav;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return wav;
    }
    const QByteArray data = file.readAll();
    if (data.size() < 44 || !data.startsWith("RIFF") || data.mid(8, 4) != "WAVE") {
        return wav;
    }
    int offset = 12;
    int dataOffset = -1;
    int dataSize = 0;
    quint16 audioFormat = 1;
    while (offset + 8 <= data.size()) {
        const QByteArray id = data.mid(offset, 4);
        const quint32 chunkSize = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(data.constData() + offset + 4));
        const int payload = offset + 8;
        if (id == "fmt " && payload + 16 <= data.size()) {
            audioFormat = qFromLittleEndian<quint16>(
                reinterpret_cast<const uchar *>(data.constData() + payload));
            wav.channels = qFromLittleEndian<quint16>(
                reinterpret_cast<const uchar *>(data.constData() + payload + 2));
            wav.sampleRate = int(qFromLittleEndian<quint32>(
                reinterpret_cast<const uchar *>(data.constData() + payload + 4)));
            wav.bits = qFromLittleEndian<quint16>(
                reinterpret_cast<const uchar *>(data.constData() + payload + 14));
            wav.isFloat = audioFormat == 3;
        } else if (id == "data") {
            dataOffset = payload;
            dataSize = int(qMin<quint32>(chunkSize, quint32(data.size() - payload)));
            break;
        }
        offset = payload + int(chunkSize) + int(chunkSize & 1);
    }
    if (dataOffset < 0 || wav.sampleRate <= 0 || wav.channels <= 0) {
        return wav;
    }
    wav.samples = data.mid(dataOffset, dataSize);
    const int bytesPerFrame = qMax(1, wav.channels * (wav.bits / 8));
    const qint64 frames = wav.samples.size() / bytesPerFrame;
    wav.durationMs = (frames * 1000) / wav.sampleRate;
    return wav;
}

QVector<float> rmsEnvelope(const WavPcm &wav)
{
    QVector<float> bins;
    if (wav.sampleRate <= 0 || wav.samples.isEmpty()) {
        return bins;
    }
    const int hop = qMax(1, wav.sampleRate * kEnvelopeHopMs / 1000);
    const int ch = qMax(1, wav.channels);
    if (wav.isFloat && wav.bits == 32) {
        const auto *s = reinterpret_cast<const float *>(wav.samples.constData());
        const int frames = wav.samples.size() / int(sizeof(float)) / ch;
        for (int i = 0; i < frames; i += hop) {
            const int end = qMin(frames, i + hop);
            double acc = 0;
            int n = 0;
            for (int f = i; f < end; ++f) {
                for (int c = 0; c < ch; ++c) {
                    const float v = s[f * ch + c];
                    acc += double(v) * double(v);
                    ++n;
                }
            }
            bins.append(n > 0 ? float(std::sqrt(acc / n)) : 0.f);
        }
        return bins;
    }
    if (wav.bits == 16) {
        const auto *s = reinterpret_cast<const qint16 *>(wav.samples.constData());
        const int frames = wav.samples.size() / 2 / ch;
        for (int i = 0; i < frames; i += hop) {
            const int end = qMin(frames, i + hop);
            double acc = 0;
            int n = 0;
            for (int f = i; f < end; ++f) {
                for (int c = 0; c < ch; ++c) {
                    const double v = double(s[f * ch + c]) / 32768.0;
                    acc += v * v;
                    ++n;
                }
            }
            bins.append(n > 0 ? float(std::sqrt(acc / n)) : 0.f);
        }
    }
    return bins;
}

} // namespace

SpeechQueuePlayer::SpeechQueuePlayer(QObject *parent)
    : QObject(parent)
    , m_output(new QAudioOutput(this))
{
    m_output->setVolume(0.7f);
    const QAudioDevice def = QMediaDevices::defaultAudioOutput();
    if (!def.isNull()) {
        m_output->setDevice(def);
    }
    for (int i = 0; i < 2; ++i) {
        m_players[i] = new QMediaPlayer(this);
        wirePlayer(i);
    }
    m_silenceTimer.setSingleShot(true);
    connect(&m_silenceTimer, &QTimer::timeout, this, &SpeechQueuePlayer::startFront);
}

SpeechQueuePlayer::~SpeechQueuePlayer()
{
    m_destroying = true;
    stop();
}

void SpeechQueuePlayer::wirePlayer(int index)
{
    QMediaPlayer *p = m_players[index];
    connect(p, &QMediaPlayer::playbackStateChanged, this,
            [this, index](QMediaPlayer::PlaybackState state) { onPlaybackState(index, int(state)); });
    connect(p, &QMediaPlayer::mediaStatusChanged, this,
            [this, index](QMediaPlayer::MediaStatus status) { onMediaStatus(index, int(status)); });
    connect(p, &QMediaPlayer::positionChanged, this,
            [this, index](qint64 pos) { onPosition(index, pos); });
    connect(p, &QMediaPlayer::errorOccurred, this,
            [this, index](QMediaPlayer::Error, const QString &e) { onPlayerError(index, e); });
}

void SpeechQueuePlayer::setOutputDevice(const QAudioDevice &device)
{
    if (!device.isNull()) {
        m_output->setDevice(device);
        qCInfo(lcSpeech) << "output device" << device.description();
    }
}

void SpeechQueuePlayer::setVolume(float volume)
{
    m_output->setVolume(qBound(0.f, volume, 1.f));
    qCInfo(lcSpeech) << "volume" << m_output->volume();
}

void SpeechQueuePlayer::setSentenceSilenceMs(int milliseconds)
{
    m_silenceMs = qMax(0, milliseconds);
}

void SpeechQueuePlayer::enqueueWav(int turnId, int sentenceIndex, const QString &wavPath,
                                   bool countsTowardT1, bool deleteAfterPlayback)
{
    if (wavPath.isEmpty() || !QFile::exists(wavPath)) {
        qCWarning(lcSpeech) << "enqueueWav missing" << wavPath << "turn" << turnId << "idx"
                            << sentenceIndex;
        emit playbackError(QStringLiteral("Missing WAV for playback"));
        return;
    }
    if (countsTowardT1 && sentenceIndex >= 0) {
        dropFillers();
    }
    if (!m_haveCurrent && m_pending.isEmpty()) {
        m_t1Emitted = false;
        m_inputComplete = false;
        m_finishing = false;
        m_endOfMediaMs = -1;
        m_activeTurnId = turnId;
    }
    Item item = inspectWav(wavPath, turnId, sentenceIndex, countsTowardT1, deleteAfterPlayback);
    qCInfo(lcSpeech) << "enqueueWav turn" << turnId << "idx" << sentenceIndex << "durMs"
                     << item.wavDurationMs << wavPath;
    m_pending.enqueue(item);
    preloadBack();
    tryPlayNext();
}

void SpeechQueuePlayer::markInputComplete()
{
    m_inputComplete = true;
    if (!isPlaybackBusy()) {
        finishTurn(false);
    }
}

void SpeechQueuePlayer::dropFillers()
{
    QQueue<Item> kept;
    while (!m_pending.isEmpty()) {
        const Item item = m_pending.dequeue();
        if (item.sentenceIndex < 0) {
            releaseItem(item);
        } else {
            kept.enqueue(item);
        }
    }
    m_pending = kept;
    if (m_haveCurrent && m_current.sentenceIndex < 0) {
        m_silenceTimer.stop();
        m_awaitingEnd = false;
        m_haveCurrent = false;
        for (auto *p : m_players) {
            p->stop();
        }
        releaseItem(m_current);
        m_current = {};
        m_front = 1 - m_front;
    }
}

void SpeechQueuePlayer::playTestTone(int milliseconds)
{
    stop();
    const QString path = writeToneWav(milliseconds);
    if (path.isEmpty()) {
        emit playbackError(QStringLiteral("Could not write test tone WAV"));
        emit speechFinished(kTestToneTurnId, true);
        return;
    }
    enqueueWav(kTestToneTurnId, 0, path, false, true);
    markInputComplete();
}

void SpeechQueuePlayer::stop()
{
    m_silenceTimer.stop();
    m_awaitingEnd = false;
    const int turnId = m_haveCurrent ? m_current.turnId
                                     : (!m_pending.isEmpty() ? m_pending.head().turnId : m_activeTurnId);
    const bool hadWork = m_haveCurrent || !m_pending.isEmpty() || m_silenceTimer.isActive();
    for (auto *p : m_players) {
        p->stop();
        p->setAudioOutput(nullptr);
        p->setSource(QUrl());
    }
    m_loadedPath[0].clear();
    m_loadedPath[1].clear();
    m_haveCurrent = false;
    m_current = {};
    clearQueue(true);
    m_inputComplete = true;
    if (hadWork && !m_finishing) {
        m_finishing = true;
        if (!m_destroying) {
            emit speechFinished(turnId, true);
        }
    }
}

bool SpeechQueuePlayer::isPlaying() const
{
    return m_haveCurrent
           && m_players[m_front]->playbackState() == QMediaPlayer::PlayingState;
}

bool SpeechQueuePlayer::isPlaybackBusy() const
{
    return m_haveCurrent || !m_pending.isEmpty() || m_silenceTimer.isActive();
}

void SpeechQueuePlayer::bindOutput(int index)
{
    for (int i = 0; i < 2; ++i) {
        if (i != index) {
            m_players[i]->setAudioOutput(nullptr);
        }
    }
    m_players[index]->setAudioOutput(m_output);
}

void SpeechQueuePlayer::loadPlayer(int index, const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (m_loadedPath[index] == abs) {
        return;
    }
    m_players[index]->setSource(QUrl::fromLocalFile(abs));
    m_loadedPath[index] = abs;
}

void SpeechQueuePlayer::preloadBack()
{
    if (m_pending.isEmpty()) {
        return;
    }
    const int back = m_haveCurrent ? (1 - m_front) : m_front;
    loadPlayer(back, m_pending.head().wavPath);
}

void SpeechQueuePlayer::tryPlayNext()
{
    if (m_haveCurrent || m_silenceTimer.isActive() || m_pending.isEmpty()) {
        if (m_pending.isEmpty() && m_inputComplete && !m_haveCurrent && !m_silenceTimer.isActive()) {
            finishTurn(false);
        }
        return;
    }
    qint64 delay = 0;
    if (m_endOfMediaMs >= 0) {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_endOfMediaMs;
        delay = qMax<qint64>(0, m_silenceMs - elapsed);
    }
    if (delay > 0) {
        m_silenceTimer.start(int(delay));
        return;
    }
    startFront();
}

void SpeechQueuePlayer::startFront()
{
    if (m_haveCurrent || m_pending.isEmpty()) {
        return;
    }
    m_current = m_pending.dequeue();
    m_haveCurrent = true;
    m_awaitingEnd = true;
    bindOutput(m_front);
    loadPlayer(m_front, m_current.wavPath);
    m_players[m_front]->setPosition(0);
    m_players[m_front]->play();
    preloadBack();
}

void SpeechQueuePlayer::onPlaybackState(int index, int state)
{
    if (index != m_front || !m_haveCurrent) {
        return;
    }
    if (state != int(QMediaPlayer::PlayingState)) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_endOfMediaMs >= 0) {
        m_lastGapMs = now - m_endOfMediaMs;
        qCInfo(lcSpeech) << "inter-sentence gapMs" << m_lastGapMs << "target<=300";
    }
    qint64 durationMs = m_players[index]->duration();
    if (durationMs <= 0) {
        durationMs = m_current.wavDurationMs;
    }
    qCInfo(lcSpeech) << "sentence playing idx" << m_current.sentenceIndex << "durationMs"
                     << durationMs << "wavMs" << m_current.wavDurationMs;
    emit sentenceStarted(m_current.sentenceIndex, durationMs);
    if (m_current.countsTowardT1 && !m_t1Emitted) {
        m_t1Emitted = true;
        emit speechStarted(m_current.turnId);
        emit firstAudioDelivered(m_current.turnId);
    }
}

void SpeechQueuePlayer::onMediaStatus(int index, int status)
{
    if (index != m_front || !m_haveCurrent || !m_awaitingEnd) {
        return;
    }
    if (status != int(QMediaPlayer::EndOfMedia)) {
        return;
    }
    m_awaitingEnd = false;
    m_endOfMediaMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 playedMs = m_players[index]->duration() > 0 ? m_players[index]->duration()
                                                             : m_current.wavDurationMs;
    qCInfo(lcSpeech) << "EndOfMedia idx" << m_current.sentenceIndex << "durationMs" << playedMs;
    m_players[index]->stop();
    m_players[index]->setAudioOutput(nullptr);
    releaseItem(m_current);
    m_haveCurrent = false;
    m_current = {};
    m_front = 1 - m_front;
    tryPlayNext();
}

void SpeechQueuePlayer::onPosition(int index, qint64 positionMs)
{
    if (index != m_front || !m_haveCurrent) {
        return;
    }
    emitEnvelope(positionMs);
}

void SpeechQueuePlayer::onPlayerError(int index, const QString &error)
{
    qCWarning(lcSpeech) << "QMediaPlayer error" << index << error;
    emit playbackError(error);
    if (index == m_front && m_haveCurrent) {
        m_awaitingEnd = false;
        releaseItem(m_current);
        m_haveCurrent = false;
        m_current = {};
        m_front = 1 - m_front;
        tryPlayNext();
    }
}

void SpeechQueuePlayer::finishTurn(bool cancelled)
{
    if (m_finishing) {
        return;
    }
    if (!cancelled && (m_haveCurrent || !m_pending.isEmpty() || m_silenceTimer.isActive())) {
        return;
    }
    if (!cancelled && !m_inputComplete) {
        return;
    }
    m_finishing = true;
    const int turnId = m_activeTurnId;
    emit speechFinished(turnId, cancelled);
}

void SpeechQueuePlayer::clearQueue(bool deleteFiles)
{
    while (!m_pending.isEmpty()) {
        Item item = m_pending.dequeue();
        if (deleteFiles) {
            releaseItem(item);
        }
    }
}

void SpeechQueuePlayer::releaseItem(const Item &item)
{
    if (item.deleteAfter && !item.wavPath.isEmpty()) {
        QFile::remove(item.wavPath);
    }
}

void SpeechQueuePlayer::emitEnvelope(qint64 positionMs)
{
    if (m_current.envelope.isEmpty()) {
        return;
    }
    const int bin = int(positionMs / kEnvelopeHopMs);
    if (bin < 0 || bin >= m_current.envelope.size()) {
        return;
    }
    emit level(m_current.envelope.at(bin));
}

QString SpeechQueuePlayer::writeToneWav(int milliseconds)
{
    const int ms = qMax(50, milliseconds);
    const int rate = 22050;
    const int n = rate * ms / 1000;
    QByteArray pcm(n * 2, Qt::Uninitialized);
    auto *dst = reinterpret_cast<qint16 *>(pcm.data());
    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < n; ++i) {
        dst[i] = qint16(qRound(std::sin(2.0 * kPi * 440.0 * i / rate) * 0.4 * 32767.0));
    }
    QByteArray header;
    header.reserve(44);
    header.append("RIFF", 4);
    appendLe32(header, quint32(36 + pcm.size()));
    header.append("WAVE", 4);
    header.append("fmt ", 4);
    appendLe32(header, 16);
    appendLe16(header, 1);
    appendLe16(header, 1);
    appendLe32(header, quint32(rate));
    appendLe32(header, quint32(rate * 2));
    appendLe16(header, 2);
    appendLe16(header, 16);
    header.append("data", 4);
    appendLe32(header, quint32(pcm.size()));

    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                             .filePath(QStringLiteral("eaglegd_tone.wav"));
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    out.write(header);
    out.write(pcm);
    return path;
}

SpeechQueuePlayer::Item SpeechQueuePlayer::inspectWav(const QString &path, int turnId,
                                                      int sentenceIndex, bool countsTowardT1,
                                                      bool deleteAfter)
{
    Item item;
    item.turnId = turnId;
    item.sentenceIndex = sentenceIndex;
    item.wavPath = path;
    item.countsTowardT1 = countsTowardT1;
    item.deleteAfter = deleteAfter;
    const WavPcm wav = readWav(path);
    item.wavDurationMs = wav.durationMs;
    item.envelope = rmsEnvelope(wav);
    return item;
}
