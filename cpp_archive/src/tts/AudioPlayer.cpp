#include "AudioPlayer.h"

#include "PcmConverter.h"

#include <QDir>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QStandardPaths>
#include <QTimer>
#include <QtMath>

Q_LOGGING_CATEGORY(lcAudio, "eagle.audio")

namespace {

constexpr int kTestToneTurnId = -2;

QAudioFormat pickPlaybackFormat(const QAudioDevice &device, const QAudioFormat &source)
{
    const QAudioFormat preferred = device.preferredFormat();
    qCInfo(lcAudio) << "preferredFormat rate" << preferred.sampleRate()
                    << "ch" << preferred.channelCount()
                    << "sampleFormat" << PcmConverter::sampleFormatName(preferred.sampleFormat())
                    << "bytesPerFrame" << preferred.bytesPerFrame()
                    << "direct22050" << device.isFormatSupported(source);
    // Never open 22050 Hz: WASAPI shared mix is 48 kHz, and letting Windows
    // resample full-scale-ish speech overshoots back to ±1 after our 0.7 gain.
    if (preferred.isValid() && preferred.sampleRate() >= 44100) {
        qCInfo(lcAudio) << "using preferredFormat; app Lanczos-resamples and applies tts.volume";
        return preferred;
    }
    qCInfo(lcAudio) << "preferredFormat unusable; using 48 kHz stereo Float";
    return PcmConverter::mixFormat48000StereoFloat();
}

QByteArray makeSinePcm(int sampleRate, int milliseconds, qreal hz, qreal amplitude)
{
    const int samples = qMax(1, sampleRate * milliseconds / 1000);
    QByteArray pcm(samples * static_cast<int>(sizeof(qint16)), Qt::Uninitialized);
    auto *dst = reinterpret_cast<qint16 *>(pcm.data());
    constexpr qreal kPi = 3.14159265358979323846;
    for (int i = 0; i < samples; ++i) {
        const qreal env = (i < samples / 20 || i > samples - samples / 20) ? 0.3 : 1.0;
        dst[i] = static_cast<qint16>(qRound(qSin(2.0 * kPi * hz * i / sampleRate)
                                            * amplitude * env * 32767.0));
    }
    return pcm;
}

} // namespace

AudioPlayer::AudioPlayer(QObject *parent)
    : QObject(parent)
    , m_sourceFormat(PcmConverter::sourceFormat22050MonoInt16())
{
    connect(&m_device, &QIODevice::readyRead, this, [this]() {
        qCInfo(lcAudio) << "readyRead queued" << m_device.queuedBytes();
        kickSink();
    });
}

AudioPlayer::~AudioPlayer()
{
    stop();
}

void AudioPlayer::setOutputDevice(const QAudioDevice &device)
{
    if (!device.isNull()) {
        m_outputDevice = device;
    } else {
        m_outputDevice = QAudioDevice();
    }
    if (m_sink && !m_waitingForDrain) {
        restartSink();
    }
}

void AudioPlayer::setVolume(float volume)
{
    if (m_sink) {
        m_sink->setVolume(volume);
    }
}

void AudioPlayer::setInputGain(float gain)
{
    if (gain > 0.f && gain <= 2.f) {
        m_inputGain = gain;
    }
}

void AudioPlayer::markInputComplete()
{
    m_inputComplete = true;
    m_device.setStreamOpen(false);
    qCInfo(lcAudio) << "inputComplete queued" << m_device.queuedBytes();
    scheduleDrainCheck();
}

void AudioPlayer::restartSink()
{
    m_device.clearQueue();
    if (m_sink) {
        m_sink->stop();
        m_sink.reset();
    }
}

void AudioPlayer::logSink(const char *reason) const
{
    if (!m_sink) {
        qCInfo(lcAudio) << reason << "no sink";
        return;
    }
    const QAudioFormat actual = m_sink->format();
    const QAudio::State state = m_sink->state();
    const char *stateName = "Unknown";
    switch (state) {
    case QAudio::ActiveState:
        stateName = "Active";
        break;
    case QAudio::IdleState:
        stateName = "Idle";
        break;
    case QAudio::StoppedState:
        stateName = "Stopped";
        break;
    case QAudio::SuspendedState:
        stateName = "Suspended";
        break;
    }
    qCInfo(lcAudio) << reason << "sinkState" << stateName << "error" << m_sink->error()
                    << "vol" << m_sink->volume() << "queueBytes" << m_device.queuedBytes()
                    << "pulled" << m_device.bytesPulled()
                    << "partialReadsDropped" << m_device.partialReadsDropped()
                    << "processedUSecs" << m_sink->processedUSecs()
                    << "actualRate" << actual.sampleRate() << "actualCh" << actual.channelCount()
                    << "actualFmt" << PcmConverter::sampleFormatName(actual.sampleFormat())
                    << "actualBpf" << actual.bytesPerFrame();
}

bool AudioPlayer::prepareSink()
{
    if (m_sink) {
        return true;
    }
    const QAudioDevice device =
        m_outputDevice.isNull() ? QMediaDevices::defaultAudioOutput() : m_outputDevice;
    if (device.isNull()) {
        const QString err = QStringLiteral("No audio output device");
        qCWarning(lcAudio) << err;
        emit playbackError(err);
        return false;
    }

    m_requestedFormat = pickPlaybackFormat(device, m_sourceFormat);
    m_sink = std::make_unique<QAudioSink>(device, m_requestedFormat, this);
    m_playbackFormat = m_sink->format();
    if (!m_playbackFormat.isValid()) {
        m_playbackFormat = m_requestedFormat;
    }
    m_sink->setVolume(1.0f);
    m_device.setBytesPerFrame(qMax(1, m_playbackFormat.bytesPerFrame()));

    qCInfo(lcAudio) << "opening sink" << device.description()
                    << "id" << device.id()
                    << "requested" << m_requestedFormat.sampleRate() << "Hz"
                    << "ch" << m_requestedFormat.channelCount()
                    << PcmConverter::sampleFormatName(m_requestedFormat.sampleFormat())
                    << "reqBpf" << m_requestedFormat.bytesPerFrame()
                    << "actual" << m_playbackFormat.sampleRate() << "Hz"
                    << "ch" << m_playbackFormat.channelCount()
                    << PcmConverter::sampleFormatName(m_playbackFormat.sampleFormat())
                    << "actualBpf" << m_playbackFormat.bytesPerFrame()
                    << "direct22050" << device.isFormatSupported(m_sourceFormat);

    connect(m_sink.get(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        if (!m_sink) {
            return;
        }
        logSink("stateChanged");
        const QAudio::Error err = m_sink->error();
        if (err != QAudio::NoError && state == QAudio::StoppedState) {
            emit playbackError(QStringLiteral("Audio output error (%1)").arg(static_cast<int>(err)));
        }
        if (state == QAudio::IdleState) {
            scheduleDrainCheck();
        }
    });
    return true;
}

void AudioPlayer::kickSink()
{
    if (!m_sink || !m_device.hasData()) {
        return;
    }
    const QAudio::State state = m_sink->state();
    if (state == QAudio::ActiveState) {
        return;
    }
    // Idle means the pull stream underran. resume() does not restart pull on
    // WASAPI; stop()+start() does, after readyRead from the next sentence.
    qCInfo(lcAudio) << "kickSink from" << state << "queueBytes" << m_device.queuedBytes();
    if (state == QAudio::IdleState || state == QAudio::SuspendedState) {
        m_processedAccumulated += segmentPlayedUSecs();
        m_pulledAtSegmentStart = m_device.bytesPulled();
        m_sink->stop();
    }
    m_sink->start(&m_device);
    logSink("kickSink");
}

QByteArray AudioPlayer::processSpeechPcm(const QByteArray &src22050MonoInt16,
                                         PcmConverter::ConversionStats *stats) const
{
    QAudioFormat dst = m_playbackFormat.isValid() ? m_playbackFormat
                                                  : PcmConverter::mixFormat48000StereoFloat();
    return PcmConverter::convertMonoInt16(src22050MonoInt16, m_sourceFormat, dst, m_inputGain,
                                          stats);
}

QByteArray AudioPlayer::convertPcm(const QByteArray &pcm22050Mono) const
{
    return processSpeechPcm(pcm22050Mono);
}

void AudioPlayer::finishCurrentTurn(bool cancelled)
{
    if (m_activeTurnId == -1 && !m_waitingForDrain) {
        return;
    }
    const int id = m_activeTurnId;
    m_waitingForDrain = false;
    m_inputComplete = true;
    m_activeTurnId = -1;
    if (id != -1) {
        emit speechFinished(id, cancelled);
    }
}

QString AudioPlayer::convertedDumpPath() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("eaglegd_piper/converted.wav"));
}

void AudioPlayer::appendConvertedDump(const QByteArray &pcm)
{
    m_convertedDump.append(pcm);
    const QString path = convertedDumpPath();
    if (!PcmConverter::writeWav(path, m_convertedDump, m_playbackFormat)) {
        qCWarning(lcAudio) << "failed to write" << path;
        return;
    }
    qCInfo(lcAudio) << "wrote converted dump" << path
                    << "bytes" << m_convertedDump.size()
                    << "rate" << m_playbackFormat.sampleRate()
                    << "ch" << m_playbackFormat.channelCount()
                    << PcmConverter::sampleFormatName(m_playbackFormat.sampleFormat());
}

void AudioPlayer::beginPlayStats(int turnId)
{
    m_sourceFramesThisPlay = 0;
    m_convertedBytesThisPlay = 0;
    m_convertedDump.clear();
    m_processedUSecsAtPlayStart = 0;
    m_processedAccumulated = 0;
    m_pulledAtSegmentStart = m_device.bytesPulled();
    m_pulledAtPlayStart = m_device.bytesPulled();
    m_playWall.restart();
    m_inputComplete = false;
    m_device.setStreamOpen(true);
    m_lastProcessedUSecs = -1;
    m_stallTicks = 0;
    qCInfo(lcAudio) << "play start turn" << turnId
                    << "processedUSecsAtStart" << m_processedUSecsAtPlayStart
                    << "gain" << m_inputGain;
}

void AudioPlayer::enqueuePcm(int turnId, int sentenceIndex, const QByteArray &pcm,
                             bool countsTowardT1)
{
    Q_UNUSED(sentenceIndex);
    if (pcm.isEmpty()) {
        qCWarning(lcAudio) << "enqueuePcm: empty PCM for turn" << turnId;
        emit playbackError(QStringLiteral("Empty audio data from speech engine"));
        return;
    }
    m_cancelled = false;
    m_waitingForDrain = true;
    m_device.setStreamOpen(true);
    const bool newTurn = m_activeTurnId != turnId;
    if (newTurn) {
        if (m_activeTurnId != -1) {
            finishCurrentTurn(true);
            m_waitingForDrain = true;
        }
        m_activeTurnId = turnId;
        if (countsTowardT1) {
            m_firstAudioSignalled = false;
        }
        emit speechStarted(turnId);
    }

    if (!prepareSink()) {
        finishCurrentTurn(false);
        return;
    }
    if (newTurn) {
        beginPlayStats(turnId);
    } else {
        m_inputComplete = false;
    }

    const qint64 srcFrames = PcmConverter::frameCountForMonoInt16(pcm);
    PcmConverter::ConversionStats stats;
    const QByteArray converted = processSpeechPcm(pcm, &stats);
    qCInfo(lcAudio) << "levels peakPre" << stats.peakPre << "peak" << stats.peak
                    << "peakDb" << stats.peakDb << "rmsDb" << stats.rmsDb
                    << "limiter" << stats.limiterEngaged << "atOrAbove1" << stats.clippedSamples
                    << "gain" << m_inputGain;
    if (stats.clippedSamples > 0) {
        qCWarning(lcAudio) << "samples at or above 1.0:" << stats.clippedSamples
                           << "peak" << stats.peak;
    }
    const int bpf = qMax(1, m_playbackFormat.bytesPerFrame());
    if (converted.size() % bpf != 0) {
        qCWarning(lcAudio) << "converted buffer is not a whole number of frames"
                           << converted.size() << "bpf" << bpf;
    }
    m_sourceFramesThisPlay += srcFrames;
    m_convertedBytesThisPlay += converted.size();
    appendConvertedDump(converted);

    const qint64 outFrames = bpf > 0 ? converted.size() / bpf : 0;
    const qint64 srcUsec = PcmConverter::expectedUSecs(srcFrames, m_sourceFormat.sampleRate());
    const qint64 dstUsec = m_playbackFormat.sampleRate() > 0
                               ? outFrames * 1000000 / m_playbackFormat.sampleRate()
                               : 0;

    qCInfo(lcAudio) << "enqueue turn" << turnId << "srcBytes" << pcm.size()
                    << "srcFrames" << srcFrames << "srcUsec" << srcUsec
                    << "convertedBytes" << converted.size() << "outFrames" << outFrames
                    << "dstUsec" << dstUsec
                    << "rate" << m_playbackFormat.sampleRate()
                    << "ch" << m_playbackFormat.channelCount()
                    << "fmt" << PcmConverter::sampleFormatName(m_playbackFormat.sampleFormat())
                    << "bpf" << bpf;

    m_device.enqueue(converted);
    kickSink();
    if (countsTowardT1 && !m_firstAudioSignalled) {
        m_firstAudioSignalled = true;
        emit firstAudioDelivered(turnId);
    }
    scheduleDrainCheck();
}

void AudioPlayer::insertSilenceMs(int ms)
{
    const int srcSamples = m_sourceFormat.sampleRate() * ms / 1000;
    QByteArray silence(srcSamples * static_cast<int>(sizeof(qint16)), 0);
    PcmConverter::ConversionStats stats;
    const QByteArray converted = processSpeechPcm(silence, &stats);
    m_sourceFramesThisPlay += srcSamples;
    m_convertedBytesThisPlay += converted.size();
    appendConvertedDump(converted);
    m_device.enqueue(converted);
    kickSink();
    scheduleDrainCheck();
}

void AudioPlayer::playTestTone(int milliseconds)
{
    const QByteArray pcm = makeSinePcm(m_sourceFormat.sampleRate(), milliseconds, 440.0, 0.45);
    qCInfo(lcAudio) << "playTestTone ms" << milliseconds << "bytes" << pcm.size();
    enqueuePcm(kTestToneTurnId, 0, pcm, false);
    markInputComplete();
    QTimer::singleShot(milliseconds + 2500, this, [this]() {
        if (m_activeTurnId != kTestToneTurnId) {
            return;
        }
        qCWarning(lcAudio) << "test tone drain timed out; forcing finish";
        finishCurrentTurn(false);
    });
}

void AudioPlayer::scheduleDrainCheck()
{
    QTimer::singleShot(80, this, &AudioPlayer::checkPlaybackDrained);
}

void AudioPlayer::logDurationCheck()
{
    if (!m_sink) {
        return;
    }
    const qint64 expectedUs =
        PcmConverter::expectedUSecs(m_sourceFramesThisPlay, m_sourceFormat.sampleRate());
    const qint64 processedUs = processedUSecs();
    const qint64 wallUs = m_playWall.nsecsElapsed() / 1000;
    const qint64 pulled = m_device.bytesPulled() - m_pulledAtPlayStart;
    const int bpf = qMax(1, m_playbackFormat.bytesPerFrame());
    const int rate = qMax(1, m_playbackFormat.sampleRate());
    const qint64 expectedBytesPerSec = qint64(rate) * bpf;
    const qreal actualBytesPerSec =
        wallUs > 0 ? (qreal(pulled) * 1000000.0 / qreal(wallUs)) : 0;

    qCInfo(lcAudio) << "durationCheck expectedUs" << expectedUs << "processedUSecs" << processedUs
                    << "wallUs" << wallUs << "convertedBytes" << m_convertedBytesThisPlay
                    << "pulledBytes" << pulled << "expectedBytesPerSec" << expectedBytesPerSec
                    << "actualBytesPerSec" << qint64(actualBytesPerSec)
                    << "sinkFmt" << m_playbackFormat.sampleRate() << "Hz"
                    << "ch" << m_playbackFormat.channelCount()
                    << PcmConverter::sampleFormatName(m_playbackFormat.sampleFormat());

    if (expectedUs > 200000) {
        const qreal ratio = qreal(processedUs) / qreal(expectedUs);
        if (ratio < 0.95 || ratio > 1.05) {
            qCWarning(lcAudio) << "playback duration mismatch: processed/expected =" << ratio
                               << "processedUs" << processedUs << "expectedUs" << expectedUs;
        }
    }
}

void AudioPlayer::checkPlaybackDrained()
{
    if (m_cancelled || !m_waitingForDrain || m_activeTurnId == -1) {
        return;
    }
    if (!m_sink) {
        return;
    }
    if (!m_inputComplete) {
        if (m_device.hasData()) {
            kickSink();
        }
        scheduleDrainCheck();
        return;
    }
    if (m_device.hasData()) {
        kickSink();
        scheduleDrainCheck();
        return;
    }
    const QAudio::State state = m_sink->state();
        const qint64 processed = processedUSecs();
    if (state == QAudio::ActiveState) {
        if (processed == m_lastProcessedUSecs) {
            ++m_stallTicks;
        } else {
            m_stallTicks = 0;
        }
        m_lastProcessedUSecs = processed;
        if (m_stallTicks < 4) {
            scheduleDrainCheck();
            return;
        }
        qCInfo(lcAudio) << "sink Active but stalled with empty queue; treating as drained";
    }
    logDurationCheck();
    qCInfo(lcAudio) << "playback drained pulled" << m_device.bytesPulled() << "state" << state;
    m_firstAudioSignalled = false;
    finishCurrentTurn(false);
}

void AudioPlayer::stop()
{
    m_cancelled = true;
    m_device.clearQueue();
    if (m_sink) {
        m_sink->stop();
        m_sink.reset();
    }
    m_firstAudioSignalled = false;
    finishCurrentTurn(true);
}

qint64 AudioPlayer::segmentPlayedUSecs() const
{
    const int bpf = qMax(1, m_playbackFormat.bytesPerFrame());
    const int rate = qMax(1, m_playbackFormat.sampleRate());
    const qint64 pulled = qMax<qint64>(0, m_device.bytesPulled() - m_pulledAtSegmentStart);
    const qint64 pulledUs = pulled * 1000000 / (qint64(rate) * bpf);
    const qint64 clockUs = m_sink ? m_sink->processedUSecs() : 0;
    return qMax(clockUs, pulledUs);
}

qint64 AudioPlayer::processedUSecs() const
{
    return qMax<qint64>(0, m_processedAccumulated + segmentPlayedUSecs());
}

bool AudioPlayer::isPlaying() const
{
    return m_sink && m_sink->state() == QAudio::ActiveState;
}

bool AudioPlayer::isPlaybackBusy() const
{
    return m_waitingForDrain || m_device.hasData() || isPlaying();
}
