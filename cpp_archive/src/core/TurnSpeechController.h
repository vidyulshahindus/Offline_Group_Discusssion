#pragma once

#include "SentenceStreamer.h"
#include "TurnTimestamps.h"
#include "tts/SpeechQueuePlayer.h"
#include "tts/TtsEngine.h"

#include <QAudioDevice>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class AppConfig;
class Paths;

class TurnSpeechController : public QObject
{
    Q_OBJECT

public:
    explicit TurnSpeechController(const AppConfig &config, const Paths &paths,
                                  QObject *parent = nullptr);

    void beginTurn(int turnId, EagleMode mode, SessionLanguage language, int wordLimit);
    void markPromptBuilt();
    void markRequestSent();
    void onLlmToken(const QString &token);
    void onLlmFinished(bool hitTokenLimit = false);
    void stop();
    QString spokenAssistantText() const;

    void setOutputDevice(const QAudioDevice &device);
    void playReadinessVoiceTest();
    void playTestTone();

    bool isTurnActive() const { return m_turnActive; }
    bool isSpeechBusy() const;
    bool isAudioPlaybackBusy() const;

    const TurnTimestamps &timestamps() const { return m_timestamps; }
    qint64 turnFirstSynthMs() const { return m_turnFirstSynthMs; }
    qint64 turnWarmSynthMs() const { return m_turnWarmSynthMs; }
    qint64 workerColdSynthMs() const { return m_tts.lastColdSynthMs(m_language); }
    qint64 workerWarmSynthMs() const { return m_tts.lastWarmSynthMs(m_language); }

signals:
    void wordBudgetReached(int turnId);
    void firstAudio(int turnId, qint64 t1Ms);
    void speechDone(int turnId);
    void turnIdle(int turnId);
    void ttsError(const QString &message);
    void readinessVoiceTestFinished(bool ok, const QString &detail);
    void testToneFinished(bool ok, const QString &detail);
    void audioPlaybackChanged();

private:
    void onSentence(int turnId, int idx, const QString &text);
    void onWav(int turnId, int idx, const QString &wavPath, qint64 synthMs, bool countsForT1);
    void onSynthesisFailed(int turnId, int sentenceIndex, const QString &error);
    void onFillerTimeout();
    void maybePlayFiller();
    void tryCompleteTurn();
    void finishTurn();

    const AppConfig &m_config;
    TtsEngine m_tts;
    SpeechQueuePlayer m_audio;
    SentenceStreamer m_streamer;
    TurnTimestamps m_timestamps;
    QElapsedTimer m_turnClock;
    QTimer m_fillerTimer;
    int m_turnId = -1;
    EagleMode m_mode = EagleMode::Doubt;
    SessionLanguage m_language = SessionLanguage::English;
    bool m_speechEnabled = true;
    bool m_turnActive = false;
    bool m_firstTtsMarked = false;
    int m_pendingTts = 0;
    bool m_fillerPlayedThisTurn = false;
    bool m_answerAudioStarted = false;
    int m_lastFillerIndex = -1;
    qint64 m_turnFirstSynthMs = -1;
    qint64 m_turnWarmSynthMs = -1;
    int m_readinessTestPending = 0;
};
