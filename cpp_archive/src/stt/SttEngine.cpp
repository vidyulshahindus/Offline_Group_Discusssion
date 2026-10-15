#include "SttEngine.h"

#include "AppConfig.h"
#include "Paths.h"
#include "ServiceSupervisor.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcStt, "eagle.stt")

QHash<QByteArray, double> SttEngine::s_micNoiseFloors;

namespace {

constexpr int kMaxSessionMs = 120000;
constexpr int kMaxSessionBytes = 16000 * 2 * 120;

} // namespace

SttEngine::SttEngine(const AppConfig &config, const Paths &paths, ServiceSupervisor &supervisor,
                       QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_supervisor(supervisor)
    , m_vad(Vad::Config{
          config.sttVadFrameMs(),
          config.sttVadStartThresholdDb(),
          config.sttVadEndHangoverMs(),
          config.sttVadMinSegmentMs(),
          config.sttVadMaxSegmentMs(),
          config.sttVadPreRollMs(),
      })
{
    m_language.load(config, paths);
    m_biasTerms = config.sttBiasTerms();
    m_client.setBaseUrl(supervisor.whisperBaseUrl());
    m_client.setHallucinationFilter(m_language.whisperHallucinations());

    m_statsTimer.setInterval(1000);
    connect(&m_statsTimer, &QTimer::timeout, this, &SttEngine::onStatsTick);
    connect(&m_capture, &AudioCapture::pcm16kReady, this, &SttEngine::onPcm);
    connect(&m_capture, &AudioCapture::errorOccurred, this, &SttEngine::errorOccurred);
    connect(&m_client, &WhisperServerClient::segmentText, this, &SttEngine::onSegmentText);
    connect(&m_client, &WhisperServerClient::inferenceFinished, this,
            &SttEngine::onInferenceFinished);
    connect(&m_client, &WhisperServerClient::queueIdle, this, &SttEngine::onQueueIdle);
    connect(&m_client, &WhisperServerClient::errorOccurred, this, &SttEngine::errorOccurred);
}

QList<QAudioDevice> SttEngine::microphones() const
{
    return m_capture.microphones();
}

void SttEngine::setMicrophone(const QAudioDevice &device)
{
    m_capture.setMicrophone(device);
}

void SttEngine::setLockedReplyLanguage(SessionLanguage language)
{
    m_lockedReply = language;
}

void SttEngine::setBiasTerms(const QStringList &terms)
{
    m_biasTerms = terms;
}

void SttEngine::setPlaybackActive(bool active)
{
    if (m_playbackActive == active) {
        return;
    }
    m_playbackActive = active;
    if (!m_config.audioMuteMicWhileSpeaking()) {
        return;
    }
    qCInfo(lcStt) << "playbackMute" << (active ? "on" : "off");
}

bool SttEngine::isBusy() const
{
    return m_listening || m_client.isBusy() || m_waitingDrain || m_pendingFallback;
}

void SttEngine::toggleListening()
{
    if (m_listening) {
        stopListening();
    } else {
        startListening();
    }
}

void SttEngine::startListening()
{
    if (m_listening) {
        return;
    }
    qCInfo(lcStt) << "listenStart requested";
    if (m_playbackActive && m_config.audioMuteMicWhileSpeaking()) {
        qCInfo(lcStt) << "listenStart blocked playbackMute=on";
        emit errorOccurred(QStringLiteral("Stop Eagle's speech before listening."));
        return;
    }
    const QString lang = whisperLanguageArg();
    if (!m_supervisor.ensureWhisperServer(lang)) {
        qCWarning(lcStt) << "listenStart whisper-server unavailable";
        emit errorOccurred(QStringLiteral("whisper-server is not available. Check Readiness."));
        return;
    }
    m_client.setBaseUrl(m_supervisor.whisperBaseUrl());
    m_confirmed.clear();
    m_lastSegmentTail.clear();
    m_sessionPcm.clear();
    m_streamMs = 0;
    m_sessionAudioMs = 0;
    m_bytesThisSecond = 0;
    m_maxFrameDb = -70.0;
    m_segmentsSent = 0;
    m_segmentsReturned = 0;
    m_gotSegmentText = false;
    m_waitingDrain = false;
    m_pendingFallback = false;

    const QAudioDevice dev = m_capture.device();
    m_micDeviceId = dev.id();
    const bool hasPriorFloor = s_micNoiseFloors.contains(m_micDeviceId);
    const double inherited = s_micNoiseFloors.value(m_micDeviceId, -50.0);
    m_vad.resetSession(inherited, hasPriorFloor);

    m_capture.start();
    const QAudioFormat fmt = m_capture.inputFormat();
    qCInfo(lcStt) << "listenStart device" << dev.description() << "format"
                  << fmt.sampleRate() << "Hz" << fmt.channelCount() << "ch"
                  << sampleFormatName(fmt.sampleFormat()) << "sourceState"
                  << int(m_capture.sourceState()) << "sourceError"
                  << int(m_capture.sourceError()) << "capturing" << m_capture.isCapturing();
    if (!m_capture.isCapturing()) {
        qCWarning(lcStt) << "listenStart aborted capture not active";
        return;
    }

    m_listening = true;
    m_statsTimer.start();
    emit listeningChanged(true);
    emit statusChanged(QStringLiteral("Listening…"));
    emit partialText(displayTranscript());
    emit inputLevel(0.f);
}

void SttEngine::stopListening()
{
    if (!m_listening && !m_client.isBusy()) {
        return;
    }
    m_statsTimer.stop();
    m_capture.stop();
    m_listening = false;
    emit listeningChanged(false);
    emit inputLevel(0.f);

    s_micNoiseFloors.insert(m_micDeviceId, m_vad.noiseFloorDb());

    for (const Vad::Segment &seg : m_vad.flush(m_streamMs)) {
        enqueueSegment(seg, false);
    }

    m_waitingDrain = true;
    m_stopTimer.start();
    emit statusChanged(QStringLiteral("Transcribing…"));
    m_client.drain();
    if (!m_client.isBusy()) {
        onQueueIdle();
    }
}

void SttEngine::onPcm(const QByteArray &pcm)
{
    const bool muted = m_playbackActive && m_config.audioMuteMicWhileSpeaking();
    if (!m_listening || muted) {
        return;
    }

    m_bytesThisSecond += pcm.size();
    appendSessionPcm(pcm);
    for (const Vad::Segment &seg : m_vad.pushPcm16k(pcm, 0)) {
        if (!seg.pcm16k.isEmpty()) {
            enqueueSegment(seg, false);
        }
    }
    m_streamMs += qint64(pcm.size() / 2) * 1000 / 16000;
    m_sessionAudioMs = m_streamMs;
    m_maxFrameDb = qMax(m_maxFrameDb, m_vad.lastFrameDb());
    emit partialText(displayTranscript());
    emit inputLevel(levelFromDb(m_vad.lastFrameDb()));
}

void SttEngine::appendSessionPcm(const QByteArray &pcm)
{
    if (m_sessionPcm.size() >= kMaxSessionBytes) {
        return;
    }
    const int room = kMaxSessionBytes - m_sessionPcm.size();
    m_sessionPcm.append(pcm.constData(), qMin(room, pcm.size()));
}

void SttEngine::enqueueSegment(const Vad::Segment &seg, bool fallbackPass)
{
    if (seg.pcm16k.isEmpty() && !fallbackPass) {
        return;
    }
    const QByteArray &pcm = fallbackPass ? m_sessionPcm : seg.pcm16k;
    if (pcm.size() < 8000) {
        return;
    }
    ++m_segmentsSent;
    qCInfo(lcStt) << "segmentRequest" << (fallbackPass ? "fallback" : "vad") << "startMs"
                  << (fallbackPass ? 0 : seg.startMs) << "endMs"
                  << (fallbackPass ? m_sessionAudioMs : seg.endMs) << "pcmBytes" << pcm.size();
    m_client.enqueue(pcm, buildPrompt(), fallbackPass ? 0 : seg.startMs,
                     fallbackPass ? m_sessionAudioMs : seg.endMs, fallbackPass);
}

void SttEngine::onStatsTick()
{
    if (!m_listening) {
        return;
    }
    const bool muted = m_playbackActive && m_config.audioMuteMicWhileSpeaking();
    qCInfo(lcStt) << "listenTick bytes" << m_bytesThisSecond << "frameDbFS" << m_vad.lastFrameDb()
                  << "noiseFloorDbFS" << m_vad.noiseFloorDb() << "inSpeech" << m_vad.inSpeech()
                  << "playbackMuted" << muted;
    m_bytesThisSecond = 0;
}

void SttEngine::onSegmentText(const QString &text)
{
    const QString cleaned = m_language.filterWhisperHallucination(text);
    if (cleaned.isEmpty()) {
        return;
    }
    m_gotSegmentText = true;
    if (!m_confirmed.isEmpty()) {
        m_confirmed.append(QLatin1Char(' '));
    }
    m_confirmed.append(cleaned);
    m_lastSegmentTail = cleaned;
    emit partialText(displayTranscript());
}

void SttEngine::onInferenceFinished(qint64 segmentStartMs, qint64 segmentEndMs, qint64 replyMs,
                                  int charCount, const QString &error, bool fallbackPass)
{
    ++m_segmentsReturned;
    if (error.isEmpty()) {
        qCInfo(lcStt) << "segmentReply" << (fallbackPass ? "fallback" : "vad") << "startMs"
                      << segmentStartMs << "endMs" << segmentEndMs << "replyMs" << replyMs
                      << "chars" << charCount;
    } else {
        qCWarning(lcStt) << "segmentError" << (fallbackPass ? "fallback" : "vad") << "startMs"
                         << segmentStartMs << "endMs" << segmentEndMs << "replyMs" << replyMs
                         << "error" << error;
    }
}

void SttEngine::onQueueIdle()
{
    if (!m_waitingDrain) {
        return;
    }
    if (!m_gotSegmentText && !m_pendingFallback && m_sessionPcm.size() >= 8000) {
        m_pendingFallback = true;
        qCInfo(lcStt) << "fallbackWholeRecording pcmBytes" << m_sessionPcm.size() << "audioMs"
                      << m_sessionAudioMs;
        Vad::Segment whole;
        whole.startMs = 0;
        whole.endMs = m_sessionAudioMs;
        enqueueSegment(whole, true);
        m_client.drain();
        return;
    }

    m_waitingDrain = false;
    m_pendingFallback = false;
    const qint64 stopToFinalMs = m_stopTimer.isValid() ? m_stopTimer.elapsed() : -1;
    qCInfo(lcStt) << "listenStop totalAudioMs" << m_sessionAudioMs << "maxFrameDbFS"
                  << m_maxFrameDb << "segmentsSent" << m_segmentsSent << "segmentsReturned"
                  << m_segmentsReturned << "stopToFinalMs" << stopToFinalMs;

    emit statusChanged(QStringLiteral("Ready"));
    QString note;
    const SessionLanguage reply =
        m_language.replyLanguageFor(m_confirmed, m_lockedReply, &note);
    if (!note.isEmpty()) {
        qCInfo(lcStt) << note;
    }
    emit finalText(m_confirmed, reply);
}

QString SttEngine::buildPrompt() const
{
    QStringList parts;
    if (!m_biasTerms.isEmpty()) {
        QString terms = m_biasTerms.join(QStringLiteral(", "));
        if (terms.size() > 150) {
            terms = terms.left(147) + QStringLiteral("...");
        }
        parts.append(terms);
    }
    if (!m_lastSegmentTail.isEmpty()) {
        QString tail = m_lastSegmentTail;
        if (tail.size() > 120) {
            tail = tail.right(120);
        }
        parts.append(tail);
    }
    return parts.join(QStringLiteral(", "));
}

QString SttEngine::displayTranscript() const
{
    if (m_confirmed.isEmpty()) {
        return m_listening ? QStringLiteral("…") : QString();
    }
    return m_listening ? m_confirmed + QStringLiteral(" …") : m_confirmed;
}

QString SttEngine::whisperLanguageArg() const
{
    const QString mode = m_config.sttLanguageMode();
    if (mode == QLatin1String("auto")) {
        return QStringLiteral("auto");
    }
    if (mode == QLatin1String("hi")) {
        return QStringLiteral("hi");
    }
    if (mode == QLatin1String("en")) {
        return QStringLiteral("en");
    }
    return m_lockedReply == SessionLanguage::Hindi ? QStringLiteral("hi") : QStringLiteral("en");
}

QString SttEngine::sampleFormatName(QAudioFormat::SampleFormat fmt)
{
    switch (fmt) {
    case QAudioFormat::UInt8:
        return QStringLiteral("UInt8");
    case QAudioFormat::Int16:
        return QStringLiteral("Int16");
    case QAudioFormat::Int32:
        return QStringLiteral("Int32");
    case QAudioFormat::Float:
        return QStringLiteral("Float");
    default:
        return QStringLiteral("Unknown");
    }
}

float SttEngine::levelFromDb(double db)
{
    const double norm = (db + 60.0) / 60.0;
    return float(qBound(0.0, norm, 1.0));
}
