#include "TurnSpeechController.h"

#include "AppConfig.h"
#include "Paths.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcTurnSpeech, "eagle.turn")

TurnSpeechController::TurnSpeechController(const AppConfig &config, const Paths &paths,
                                           QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_tts(config, paths, this)
{
    m_tts.startSession();
    const QString backend = config.audioPlaybackBackend();
    if (backend == QLatin1String("sink")) {
        qCWarning(lcTurnSpeech)
            << "audio.backend=sink is no longer used; QAudioSink WASAPI path had format/speed "
               "bugs. Using mediaplayer (QMediaPlayer + QAudioOutput).";
    }
    m_audio.setVolume(float(m_config.ttsVolume()));
    m_audio.setSentenceSilenceMs(int(m_config.ttsSentenceSilenceS() * 1000.0));
    m_fillerTimer.setSingleShot(true);
    connect(&m_fillerTimer, &QTimer::timeout, this, &TurnSpeechController::onFillerTimeout);
    // Output-active tracking for the mic mute: event driven, plus a light poll
    // while audio is busy to catch the queue draining between signals.
    m_outputPoll.setInterval(150);
    connect(&m_outputPoll, &QTimer::timeout, this, &TurnSpeechController::syncOutputActive);
    connect(&m_audio, &SpeechQueuePlayer::sentenceStarted, this,
            [this](int, qint64) { syncOutputActive(); });
    connect(&m_streamer, &SentenceStreamer::sentenceReady, this, &TurnSpeechController::onSentence);
    connect(&m_streamer, &SentenceStreamer::wordBudgetReached, this,
            &TurnSpeechController::wordBudgetReached);
    connect(&m_tts, &TtsEngine::wavReady, this,
            [this](int turnId, int idx, const QString &wavPath, qint64 synthMs) {
                if (turnId == -1) {
                    onWav(-1, idx, wavPath, synthMs, false);
                    if (m_readinessTestPending > 0) {
                        --m_readinessTestPending;
                        if (m_readinessTestPending == 0) {
                            emit readinessVoiceTestFinished(true, QString());
                        }
                    }
                    return;
                }
                if (turnId < 0) {
                    return;
                }
                onWav(turnId, idx, wavPath, synthMs, true);
            });
    connect(&m_tts, &TtsEngine::firstTtsDone, this, [this](int turnId, int) {
        if (!m_firstTtsMarked && turnId == m_turnId) {
            m_firstTtsMarked = true;
            m_timestamps.firstTtsDoneMs = m_turnClock.elapsed();
        }
    });
    connect(&m_tts, &TtsEngine::errorOccurred, this, &TurnSpeechController::ttsError);
    connect(&m_tts, &TtsEngine::synthesisFailed, this, &TurnSpeechController::onSynthesisFailed);
    connect(&m_audio, &SpeechQueuePlayer::firstAudioDelivered, this, [this](int turnId) {
        if (m_timestamps.firstAudioMs < 0 && turnId == m_turnId) {
            m_timestamps.firstAudioMs = m_turnClock.elapsed();
            m_fillerTimer.stop();
            emit firstAudio(turnId, m_timestamps.firstAudioMs);
        }
    });
    connect(&m_audio, &SpeechQueuePlayer::speechFinished, this, [this](int turnId, bool cancelled) {
        syncOutputActive();
        if (turnId == SpeechQueuePlayer::kTestToneTurnId) {
            emit testToneFinished(!cancelled,
                                  cancelled ? QStringLiteral("cancelled") : QString());
            return;
        }
        if (!cancelled && turnId == m_turnId) {
            m_timestamps.speechDoneMs = m_turnClock.elapsed();
        }
        if (turnId == m_turnId) {
            tryCompleteTurn();
        }
    });
    connect(&m_audio, &SpeechQueuePlayer::playbackError, this, [this](const QString &message) {
        emit ttsError(message);
        if (!m_turnActive) {
            emit testToneFinished(false, message);
        }
        if (m_turnActive && m_pendingTts > 0) {
            m_pendingTts = 0;
        }
        tryCompleteTurn();
    });
}

void TurnSpeechController::setOutputDevice(const QAudioDevice &device)
{
    m_audio.setOutputDevice(device);
}

bool TurnSpeechController::isSpeechBusy() const
{
    return m_pendingTts > 0 || m_tts.isBusy() || m_audio.isPlaybackBusy();
}

void TurnSpeechController::setSpeechEnabled(bool enabled)
{
    m_speechEnabled = enabled;
    if (!enabled) {
        m_fillerTimer.stop();
    }
}

void TurnSpeechController::syncOutputActive()
{
    const bool active = m_audio.isPlaybackBusy();
    if (active && !m_outputPoll.isActive()) {
        m_outputPoll.start();
    } else if (!active) {
        m_outputPoll.stop();
    }
    if (active != m_outputActive) {
        m_outputActive = active;
        emit outputActiveChanged(active);
    }
}

void TurnSpeechController::beginTurn(int turnId, EagleMode mode, SessionLanguage language,
                                     int wordLimit)
{
    m_fillerTimer.stop();
    m_audio.stop();
    m_streamer.reset(-1, EagleMode::Doubt, SessionLanguage::English, 75);
    m_pendingTts = 0;
    m_turnActive = false;

    m_turnId = turnId;
    m_mode = mode;
    m_language = language;
    m_turnActive = true;
    m_firstTtsMarked = false;
    m_pendingTts = 0;
    m_fillerPlayedThisTurn = false;
    m_answerAudioStarted = false;
    m_turnFirstSynthMs = -1;
    m_turnWarmSynthMs = -1;
    m_timestamps = TurnTimestamps{};
    m_turnClock.start();
    m_timestamps.t0Ms = 0;
    m_streamer.reset(turnId, mode, language, wordLimit);
    m_fillerTimer.stop();
    if (m_config.ttsFillersEnabled() && mode != EagleMode::Practice && m_speechEnabled) {
        m_fillerTimer.start(m_config.ttsFillerAfterMs());
    }
}

void TurnSpeechController::markPromptBuilt()
{
    m_timestamps.promptBuiltMs = m_turnClock.elapsed();
}

void TurnSpeechController::markRequestSent()
{
    m_timestamps.requestSentMs = m_turnClock.elapsed();
}

void TurnSpeechController::onLlmToken(const QString &token)
{
    if (m_timestamps.firstTokenMs < 0) {
        m_timestamps.firstTokenMs = m_turnClock.elapsed();
    }
    m_streamer.setClockMs(m_turnClock.elapsed());
    m_streamer.pushToken(token);
}

void TurnSpeechController::onLlmFinished()
{
    m_timestamps.llmDoneMs = m_turnClock.elapsed();
    m_streamer.finishStream();
    tryCompleteTurn();
}

void TurnSpeechController::stop()
{
    m_fillerTimer.stop();
    const int endedTurn = m_turnId;
    m_audio.stop();
    m_streamer.reset(-1, EagleMode::Doubt, SessionLanguage::English, 75);
    m_pendingTts = 0;
    syncOutputActive();
    if (m_turnActive) {
        m_turnActive = false;
        emit turnIdle(endedTurn);
    }
}

void TurnSpeechController::onSentence(int turnId, int idx, const QString &text)
{
    if (turnId != m_turnId) {
        return;
    }
    if (m_timestamps.firstSentenceMs < 0) {
        m_timestamps.firstSentenceMs = m_turnClock.elapsed();
    }
    if (!m_speechEnabled) {
        tryCompleteTurn();
        return;
    }
    ++m_pendingTts;
    m_tts.speakSentence(turnId, idx, text, m_language);
}

void TurnSpeechController::onWav(int turnId, int idx, const QString &wavPath, qint64 synthMs,
                                 bool countsForT1)
{
    if (countsForT1 && turnId != m_turnId) {
        return;
    }
    if (countsForT1) {
        if (m_turnFirstSynthMs < 0) {
            m_turnFirstSynthMs = synthMs;
        } else {
            m_turnWarmSynthMs = synthMs;
        }
        m_answerAudioStarted = true;
        m_fillerTimer.stop();
    }
    const bool deleteAfter = countsForT1 || (turnId < 0 && idx >= 0);
    m_audio.enqueueWav(turnId >= 0 ? turnId : m_turnId, idx, wavPath, countsForT1, deleteAfter);
    syncOutputActive();
    if (countsForT1) {
        if (m_pendingTts > 0) {
            --m_pendingTts;
        }
        tryCompleteTurn();
    }
}

void TurnSpeechController::onSynthesisFailed(int turnId, int sentenceIndex, const QString &error)
{
    Q_UNUSED(sentenceIndex);
    if (turnId == -1) {
        if (m_readinessTestPending > 0) {
            --m_readinessTestPending;
        }
        emit readinessVoiceTestFinished(false, error);
        return;
    }
    if (turnId != m_turnId) {
        return;
    }
    if (m_pendingTts > 0) {
        --m_pendingTts;
    }
    emit ttsError(error);
    tryCompleteTurn();
}

void TurnSpeechController::tryCompleteTurn()
{
    if (!m_turnActive || m_turnId < 0) {
        return;
    }
    if (m_timestamps.llmDoneMs < 0) {
        return;
    }
    if (m_pendingTts > 0) {
        return;
    }
    m_audio.markInputComplete();
    if (m_audio.isPlaybackBusy()) {
        return;
    }
    finishTurn();
}

void TurnSpeechController::finishTurn()
{
    if (!m_turnActive) {
        return;
    }
    m_turnActive = false;
    emit speechDone(m_turnId);
    emit turnIdle(m_turnId);
}

void TurnSpeechController::onFillerTimeout()
{
    maybePlayFiller();
}

void TurnSpeechController::maybePlayFiller()
{
    if (m_mode == EagleMode::Practice || m_fillerPlayedThisTurn || m_answerAudioStarted
        || m_timestamps.firstAudioMs >= 0 || !m_speechEnabled
        || !m_tts.fillers().isReady()) {
        return;
    }
    const int idx = m_tts.fillers().pickIndex(m_language, m_lastFillerIndex);
    if (idx < 0) {
        return;
    }
    const QString wav = m_tts.fillers().wavPathFor(m_language, idx);
    if (wav.isEmpty()) {
        return;
    }
    m_fillerPlayedThisTurn = true;
    m_lastFillerIndex = idx;
    onWav(-1, -1, wav, 0, false);
}

void TurnSpeechController::playReadinessVoiceTest()
{
    if (m_readinessTestPending > 0 || m_turnActive) {
        emit readinessVoiceTestFinished(false,
                                        QStringLiteral("Finish the current turn before testing voice."));
        return;
    }
    m_readinessTestPending = 2;
    m_tts.speakTestPhrase(SessionLanguage::English, 0,
                          QStringLiteral("Hello. I am Eagle, your study assistant."));
    m_tts.speakTestPhrase(SessionLanguage::Hindi, 1,
                          QStringLiteral("नमस्ते। मैं ईगल हूँ, आपकी अध्ययन सहायक।"));
}

void TurnSpeechController::playTestTone()
{
    if (m_turnActive || m_readinessTestPending > 0) {
        emit testToneFinished(false, QStringLiteral("Finish the current turn first."));
        return;
    }
    m_audio.playTestTone(1000);
    syncOutputActive();
}
