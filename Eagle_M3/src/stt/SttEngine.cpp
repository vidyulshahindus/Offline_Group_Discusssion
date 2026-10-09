#include "SttEngine.h"

#include "LanguageService.h"

#include <QAudioFormat>
#include <QAudioSource>
#include <QHash>
#include <QIODevice>
#include <QLoggingCategory>
#include <QMediaDevices>

#include <cmath>

Q_LOGGING_CATEGORY(lcStt, "eagle.stt")

namespace {

PcmTo16kMono::SampleType toSampleType(QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8:
        return PcmTo16kMono::SampleType::UInt8;
    case QAudioFormat::Int32:
        return PcmTo16kMono::SampleType::Int32;
    case QAudioFormat::Float:
        return PcmTo16kMono::SampleType::Float;
    case QAudioFormat::Int16:
    default:
        return PcmTo16kMono::SampleType::Int16;
    }
}

} // namespace

SttEngine::SttEngine(QObject *parent)
    : QObject(parent)
    , m_device(QMediaDevices::defaultAudioInput())
{
    connect(&m_vad, &Vad::segmentReady, this, &SttEngine::onSegment);
    connect(&m_vad, &Vad::segmentDiscarded, this, [](qint64 startMs, qint64 endMs) {
        qCInfo(lcStt) << "segment discarded (too short)" << startMs << "-" << endMs << "ms";
    });
    connect(&m_vad, &Vad::speechStarted, this, [this](qint64) {
        m_autoSubmitTimer.stop();
        if (m_state == State::Listening) {
            emit transcriptUpdated(m_transcript.text(), true);
        }
    });
    connect(&m_vad, &Vad::speechEnded, this, [this](qint64) {
        if (m_state != State::Listening) {
            return;
        }
        emit transcriptUpdated(m_transcript.text(), false);
        if (m_settings.autoSubmitOnSilenceMs > 0 && m_segmentsSent > 0) {
            const int hangover = m_settings.vad.endHangoverMs;
            m_autoSubmitTimer.start(qMax(0, m_settings.autoSubmitOnSilenceMs - hangover));
        }
    });

    connect(&m_client, &WhisperServerClient::transcribed, this, &SttEngine::onTranscribed);
    connect(&m_client, &WhisperServerClient::failed, this, &SttEngine::onFailed);
    connect(&m_client, &WhisperServerClient::idle, this, &SttEngine::onClientIdle);
    // The bias prompt is built when a request is sent, so it includes every
    // segment transcribed so far (spec §10.1: unit terms + previous text tail).
    m_client.setPromptProvider([this]() {
        return m_settings.useBiasPrompt
                   ? TranscriptAssembler::biasPrompt(m_settings.biasTerms, m_transcript.tail(120))
                   : QString();
    });

    m_drainTimer.setSingleShot(true);
    connect(&m_drainTimer, &QTimer::timeout, this, [this]() { finish(true); });
    m_autoSubmitTimer.setSingleShot(true);
    connect(&m_autoSubmitTimer, &QTimer::timeout, this, [this]() {
        if (m_state == State::Listening) {
            emit autoSubmitRequested();
        }
    });
    configure(SttSettings{});
}

SttEngine::~SttEngine()
{
    closeSource();
}

void SttEngine::configure(const SttSettings &settings)
{
    m_settings = settings;
    m_client.setBaseUrl(settings.serverUrl);
    m_client.setRequestTimeoutMs(settings.requestTimeoutMs);
    m_client.setStartupRetryMs(settings.startupRetryMs);
    m_transcript.setHallucinations(settings.hallucinations);
}

QList<QAudioDevice> SttEngine::microphones()
{
    return QMediaDevices::audioInputs();
}

void SttEngine::setDevice(const QAudioDevice &device)
{
    if (m_state == State::Listening) {
        return;  // applies to the next Listen
    }
    if (device.id() != m_device.id()) {
        m_lastFloorDb = std::numeric_limits<double>::quiet_NaN();  // new mic: measure again
    }
    m_device = device;
}

void SttEngine::setLanguageLock(const QString &code)
{
    const QString lock = code.trimmed().isEmpty() ? QStringLiteral("en") : code.trimmed().toLower();
    if (lock != m_settings.languageLock) {
        qCInfo(lcStt) << "language lock" << m_settings.languageLock << "->" << lock;
        m_settings.languageLock = lock;
    }
}

void SttEngine::setOutputActive(bool active)
{
    if (m_outputActive == active) {
        return;
    }
    m_outputActive = active;
    if (active && m_settings.muteWhileSpeaking && m_state == State::Listening) {
        m_vad.abandonSegment();
        emit statusChanged(QStringLiteral("Microphone paused while Eagle speaks"));
    } else if (!active && m_state == State::Listening) {
        emit statusChanged(QStringLiteral("Listening…"));
    }
}

void SttEngine::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(state);
}

void SttEngine::beginSession(int sampleRate, int channels, PcmTo16kMono::SampleType sampleType)
{
    m_converter.reset(sampleRate, channels, sampleType);
    VadParams vadParams = m_settings.vad;
    vadParams.sampleRate = PcmTo16kMono::kTargetRate;
    if (!std::isnan(m_lastFloorDb)) {
        vadParams.seedFloorDb = m_lastFloorDb;
    }
    m_vad.setParams(vadParams);
    m_transcript.reset();
    m_client.cancelAll();
    m_languageVotes.clear();
    m_sessionLanguage.clear();
    m_autoSegments.clear();
    m_segmentsSent = 0;
    m_failures = 0;
    m_lastError.clear();
    m_autoSubmitTimer.stop();
    m_drainTimer.stop();
    m_listenClock.start();
    m_levelClock.start();
}

bool SttEngine::startListening(QIODevice *input, int sampleRate, int channels,
                               PcmTo16kMono::SampleType sampleType)
{
    if (m_state != State::Idle || !input || !input->isOpen()) {
        return false;
    }
    beginSession(sampleRate, channels, sampleType);
    m_io = input;
    connect(m_io, &QIODevice::readyRead, this, &SttEngine::onAudioReady);
    qCInfo(lcStt) << "listening on an injected device" << sampleRate << "Hz" << channels << "ch";
    setState(State::Listening);
    emit transcriptUpdated(QString(), false);
    emit statusChanged(QStringLiteral("Listening…"));
    return true;
}

bool SttEngine::startListening()
{
    if (m_state != State::Idle) {
        return false;
    }
    if (m_device.isNull()) {
        m_device = QMediaDevices::defaultAudioInput();
    }
    if (m_device.isNull()) {
        emit errorOccurred(QStringLiteral("No microphone found. Plug one in and press ↻."));
        return false;
    }

    QAudioFormat preferred;
    preferred.setSampleRate(16000);
    preferred.setChannelCount(1);
    preferred.setSampleFormat(QAudioFormat::Int16);
    const QAudioFormat format =
        m_device.isFormatSupported(preferred) ? preferred : m_device.preferredFormat();
    if (!format.isValid()) {
        emit errorOccurred(QStringLiteral("The selected microphone reports no usable audio format."));
        return false;
    }
    beginSession(format.sampleRate(), format.channelCount(), toSampleType(format.sampleFormat()));

    m_source = new QAudioSource(m_device, format, this);
    m_source->setBufferSize(format.bytesForDuration(qint64(qMax(100, m_settings.captureBufferMs)) * 1000));
    connect(m_source, &QAudioSource::stateChanged, this, [this](QAudio::State state) {
        if (!m_source || state != QAudio::StoppedState || m_source->error() == QAudio::NoError) {
            return;
        }
        const QString message =
            QStringLiteral("The microphone stopped (error %1). Check the device and press ↻.")
                .arg(int(m_source->error()));
        qCWarning(lcStt) << message;
        if (m_state == State::Listening) {
            stopListening();
        }
        emit errorOccurred(message);
    });
    m_io = m_source->start();
    if (!m_io) {
        const int err = int(m_source->error());
        closeSource();
        emit errorOccurred(QStringLiteral("Could not open the microphone (error %1).").arg(err));
        return false;
    }
    connect(m_io, &QIODevice::readyRead, this, &SttEngine::onAudioReady);

    qCInfo(lcStt) << "listening on" << m_device.description() << "format" << format.sampleRate()
                  << "Hz" << format.channelCount() << "ch" << format.sampleFormat() << "lang"
                  << m_settings.languageLock << "floorSeedDb" << m_lastFloorDb;
    setState(State::Listening);
    emit transcriptUpdated(QString(), false);
    emit statusChanged(QStringLiteral("Listening…"));
    return true;
}

void SttEngine::onAudioReady()
{
    if (m_io) {
        processBytes(m_io->readAll());
    }
}

void SttEngine::processBytes(const QByteArray &raw)
{
    if (raw.isEmpty() || m_state != State::Listening) {
        return;
    }
    if (m_outputActive && m_settings.muteWhileSpeaking) {
        return;  // never transcribe Eagle's own voice
    }
    m_vad.pushPcm(m_converter.convert(raw));
    if (m_levelClock.elapsed() >= 50) {
        m_levelClock.restart();
        const double db = m_vad.lastFrameDb();
        emit level(float(qBound(0.0, (db + 60.0) / 50.0, 1.0)));
    }
}

void SttEngine::onSegment(const QByteArray &pcm, qint64 startMs, qint64 endMs)
{
    if (m_state == State::Idle) {
        return;
    }
    QString lang = m_settings.languageLock;
    if (lang == QLatin1String("auto") && !m_sessionLanguage.isEmpty()) {
        lang = m_sessionLanguage;
    }
    ++m_segmentsSent;
    qCInfo(lcStt) << "segment" << m_segmentsSent << startMs << "-" << endMs << "ms, floor"
                  << m_vad.noiseFloorDb() << "dB, lang" << lang;
    const bool detect = lang == QLatin1String("auto");
    const int seq = m_client.enqueue(pcm, QString(), lang, detect);
    if (detect) {
        m_autoSegments.insert(seq, SentSegment{pcm, QString()});
    }
}

void SttEngine::onTranscribed(int seq, const QString &text, const QString &language, qint64 elapsedMs)
{
    if (m_state == State::Idle) {
        return;
    }
    const SentSegment sent = m_autoSegments.take(seq);
    const QString code = LanguageService::whisperNameToCode(language);
    if (!code.isEmpty() && m_sessionLanguage.isEmpty()) {
        if (code == QLatin1String("ur") && m_settings.urduScriptAsHindi && !sent.pcm.isEmpty()) {
            // Hindi speech is often written in Urdu script under auto-detect. Redo this
            // segment as Hindi (Devanagari) before anything else in the queue.
            m_sessionLanguage = QStringLiteral("hi");
            m_languageVotes.append(m_sessionLanguage);
            qCInfo(lcStt) << "seq" << seq << "came back in Urdu script; redoing as Hindi";
            for (const int queued : m_client.retargetQueued(QStringLiteral("auto"), m_sessionLanguage)) {
                m_autoSegments.remove(queued);
            }
            m_client.enqueueFront(sent.pcm, sent.prompt, m_sessionLanguage, false);
            return;
        }
        if (code == QLatin1String("en") || code == QLatin1String("hi")) {
            m_sessionLanguage = code;
            for (const int queued : m_client.retargetQueued(QStringLiteral("auto"), m_sessionLanguage)) {
                m_autoSegments.remove(queued);
            }
        }
    }
    const bool kept = m_transcript.appendSegment(text);
    if (!code.isEmpty()) {
        m_languageVotes.append(code);
    }
    // Lengths only: the log must not hold what students said (privacy, §18.2).
    qCInfo(lcStt) << "seq" << seq << "whisperMs" << elapsedMs << (kept ? "kept" : "dropped")
                  << TranscriptAssembler::clean(text).size() << "chars";
    emit transcriptUpdated(m_transcript.text(), m_state == State::Listening && m_vad.inSpeech());
}

void SttEngine::onFailed(int seq, const QString &error)
{
    m_autoSegments.remove(seq);
    ++m_failures;
    m_lastError = error;
    emit statusChanged(QStringLiteral("Part of the speech could not be transcribed"));
}

void SttEngine::onClientIdle()
{
    if (m_state == State::Finishing) {
        finish(false);
    }
}

void SttEngine::stopListening()
{
    if (m_state != State::Listening) {
        return;
    }
    if (m_io) {
        processBytes(m_io->readAll());
    }
    closeSource();
    if (m_vad.calibrated()) {
        m_lastFloorDb = m_vad.noiseFloorDb();
    }
    m_autoSubmitTimer.stop();
    m_stopClock.start();
    setState(State::Finishing);
    emit statusChanged(QStringLiteral("Transcribing…"));
    m_vad.flush(m_settings.flushMinMs);  // may enqueue the last segment
    if (m_client.isIdle()) {
        finish(false);
    } else {
        m_drainTimer.start(qMax(1000, m_settings.drainTimeoutMs));
    }
}

void SttEngine::finish(bool timedOut)
{
    if (m_state != State::Finishing) {
        return;
    }
    m_drainTimer.stop();
    if (timedOut) {
        qCWarning(lcStt) << "drain timeout; pending" << m_client.pendingCount();
        m_client.cancelAll();
        ++m_failures;
        m_lastError = QStringLiteral("whisper-server was too slow");
    }
    const QString text = m_transcript.text();
    const qint64 stopToFinal = m_stopClock.elapsed();
    qCInfo(lcStt) << "final: stopToFinalMs" << stopToFinal << "segments" << m_segmentsSent
                  << "kept" << m_transcript.segmentCount() << "failures" << m_failures
                  << "listenMs" << m_listenClock.elapsed();
    setState(State::Idle);
    emit finalText(text, majorityLanguage(), stopToFinal);
    if (text.isEmpty() && m_failures > 0) {
        emit errorOccurred(QStringLiteral("Speech could not be transcribed (%1). Open Readiness "
                                          "(Ctrl+Shift+R) and check whisper-server.")
                               .arg(m_lastError.left(160)));
    }
}

void SttEngine::cancel()
{
    closeSource();
    m_vad.reset();
    m_client.cancelAll();
    m_drainTimer.stop();
    m_autoSubmitTimer.stop();
    m_transcript.reset();
    m_autoSegments.clear();
    setState(State::Idle);
}

void SttEngine::closeSource()
{
    if (m_io) {
        disconnect(m_io, nullptr, this, nullptr);
        m_io = nullptr;
    }
    if (m_source) {
        disconnect(m_source, nullptr, this, nullptr);
        m_source->stop();
        m_source->deleteLater();
        m_source = nullptr;
    }
}

QString SttEngine::majorityLanguage() const
{
    if (m_settings.languageLock != QLatin1String("auto")) {
        return m_settings.languageLock;
    }
    QHash<QString, int> counts;
    QString best;
    int bestCount = 0;
    for (const QString &code : m_languageVotes) {
        const int c = ++counts[code];
        if (c > bestCount) {
            bestCount = c;
            best = code;
        }
    }
    return best;
}
