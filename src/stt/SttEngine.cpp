#include "SttEngine.h"

#include "AppConfig.h"
#include "Paths.h"
#include "ServiceSupervisor.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcStt, "eagle.stt")

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

    connect(&m_capture, &AudioCapture::pcm16kReady, this, &SttEngine::onPcm);
    connect(&m_capture, &AudioCapture::errorOccurred, this, &SttEngine::errorOccurred);
    connect(&m_client, &WhisperServerClient::segmentText, this, &SttEngine::onSegmentText);
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
    m_playbackActive = active;
    if (active && m_listening) {
        stopListening();
    }
}

bool SttEngine::isBusy() const
{
    return m_listening || m_client.isBusy() || m_waitingDrain;
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
    if (m_playbackActive && m_config.audioMuteMicWhileSpeaking()) {
        emit errorOccurred(QStringLiteral("Stop Eagle's speech before listening."));
        return;
    }
    const QString lang = whisperLanguageArg();
    if (!m_supervisor.ensureWhisperServer(lang)) {
        emit errorOccurred(QStringLiteral("whisper-server is not available. Check Readiness."));
        return;
    }
    m_client.setBaseUrl(m_supervisor.whisperBaseUrl());
    m_confirmed.clear();
    m_lastSegmentTail.clear();
    m_streamMs = 0;
    m_waitingDrain = false;
    m_capture.start();
    m_listening = true;
    emit listeningChanged(true);
    emit statusChanged(QStringLiteral("Listening…"));
    emit partialText(displayTranscript());
}

void SttEngine::stopListening()
{
    if (!m_listening && !m_client.isBusy()) {
        return;
    }
    m_capture.stop();
    m_listening = false;
    emit listeningChanged(false);

    for (const Vad::Segment &seg : m_vad.flush(m_streamMs)) {
        enqueueSegment(seg.pcm16k);
    }
    m_waitingDrain = true;
    emit statusChanged(QStringLiteral("Transcribing…"));
    m_client.drain();
    if (!m_client.isBusy()) {
        onQueueIdle();
    }
}

void SttEngine::onPcm(const QByteArray &pcm)
{
    if (!m_listening || (m_playbackActive && m_config.audioMuteMicWhileSpeaking())) {
        return;
    }
    for (const Vad::Segment &seg : m_vad.pushPcm16k(pcm, 0)) {
        enqueueSegment(seg.pcm16k);
    }
    m_streamMs += qint64(pcm.size() / 2) * 1000 / 16000;
    emit partialText(displayTranscript());
}

void SttEngine::enqueueSegment(const QByteArray &pcm16k)
{
    if (pcm16k.isEmpty()) {
        return;
    }
    m_client.enqueue(pcm16k, buildPrompt());
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

void SttEngine::onSegmentText(const QString &text)
{
    const QString cleaned = m_language.filterWhisperHallucination(text);
    if (cleaned.isEmpty()) {
        return;
    }
    if (!m_confirmed.isEmpty()) {
        m_confirmed.append(QLatin1Char(' '));
    }
    m_confirmed.append(cleaned);
    m_lastSegmentTail = cleaned;
    emit partialText(displayTranscript());
}

void SttEngine::onQueueIdle()
{
    if (!m_waitingDrain) {
        return;
    }
    m_waitingDrain = false;
    emit statusChanged(QStringLiteral("Ready"));
    QString note;
    const SessionLanguage reply =
        m_language.replyLanguageFor(m_confirmed, m_lockedReply, &note);
    if (!note.isEmpty()) {
        qCInfo(lcStt) << note;
    }
    emit finalText(m_confirmed, reply);
}
