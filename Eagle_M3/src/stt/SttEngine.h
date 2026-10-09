#pragma once

#include "PcmTo16kMono.h"
#include "TranscriptAssembler.h"
#include "Vad.h"
#include "WhisperServerClient.h"

#include <QAudioDevice>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <limits>

class QAudioSource;
class QIODevice;

struct SttSettings {
    QUrl serverUrl = QUrl(QStringLiteral("http://127.0.0.1:8178"));
    QString languageLock = QStringLiteral("en");   // "en", "hi", ... or "auto"
    QStringList biasTerms;
    QStringList hallucinations;
    VadParams vad;
    int requestTimeoutMs = 20000;
    int startupRetryMs = 15000;
    int flushMinMs = 300;           // Stop listening: flush the open segment if >= this
    int drainTimeoutMs = 8000;      // Stop listening: give up waiting for whisper after this
    int autoSubmitOnSilenceMs = 0;  // 0 = off (School default)
    bool muteWhileSpeaking = true;  // never capture while Eagle's audio plays
    bool useBiasPrompt = true;
    bool urduScriptAsHindi = true;  // auto mode: redo an Urdu-script segment as Hindi
    int captureBufferMs = 500;
};

// Speech input pipeline (spec §10): QAudioSource -> 16 kHz mono -> Vad -> whisper-server.
// Each VAD segment is transcribed while the person keeps talking, so pressing
// Stop only waits for the last segment.
class SttEngine : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Listening, Finishing };
    Q_ENUM(State)

    explicit SttEngine(QObject *parent = nullptr);
    ~SttEngine() override;

    void configure(const SttSettings &settings);
    const SttSettings &settings() const { return m_settings; }

    static QList<QAudioDevice> microphones();
    void setDevice(const QAudioDevice &device);
    QAudioDevice device() const { return m_device; }

    void setLanguageLock(const QString &code);
    QString languageLock() const { return m_settings.languageLock; }
    void setBiasTerms(const QStringList &terms) { m_settings.biasTerms = terms; }

    // True while Eagle's audio is playing. With mute_mic_while_speaking, captured
    // audio is dropped and any open segment is abandoned.
    void setOutputActive(bool active);

    State state() const { return m_state; }
    bool isListening() const { return m_state == State::Listening; }
    bool isBusy() const { return m_state != State::Idle; }
    QString transcript() const { return m_transcript.text(); }

    // Listens to an already-open device with raw PCM instead of a microphone
    // (unit tests; later a "replay a recording" self-test). Not owned.
    bool startListening(QIODevice *input, int sampleRate, int channels,
                        PcmTo16kMono::SampleType sampleType);

public slots:
    bool startListening();
    void stopListening();
    void cancel();

signals:
    void stateChanged(SttEngine::State state);
    void transcriptUpdated(const QString &text, bool speechInProgress);
    void finalText(const QString &text, const QString &whisperLanguage, qint64 stopToFinalMs);
    void level(float level01);
    void errorOccurred(const QString &message);
    void statusChanged(const QString &status);
    void autoSubmitRequested();

private slots:
    void onAudioReady();
    void onSegment(const QByteArray &pcm, qint64 startMs, qint64 endMs);
    void onTranscribed(int seq, const QString &text, const QString &language, qint64 elapsedMs);
    void onFailed(int seq, const QString &error);
    void onClientIdle();

private:
    void beginSession(int sampleRate, int channels, PcmTo16kMono::SampleType sampleType);
    void processBytes(const QByteArray &raw);
    void closeSource();
    void setState(State state);
    void finish(bool timedOut);
    QString majorityLanguage() const;

    SttSettings m_settings;
    QAudioDevice m_device;
    QAudioSource *m_source = nullptr;
    QIODevice *m_io = nullptr;
    PcmTo16kMono m_converter;
    Vad m_vad;
    WhisperServerClient m_client;
    TranscriptAssembler m_transcript;
    State m_state = State::Idle;
    bool m_outputActive = false;
    double m_lastFloorDb = std::numeric_limits<double>::quiet_NaN();
    QElapsedTimer m_levelClock;
    QElapsedTimer m_stopClock;
    QElapsedTimer m_listenClock;
    QTimer m_drainTimer;
    QTimer m_autoSubmitTimer;
    QStringList m_languageVotes;
    // Auto mode: the language found in the first segment is used for the rest of
    // this Listen session (more stable than detecting every segment).
    QString m_sessionLanguage;
    struct SentSegment {
        QByteArray pcm;
        QString prompt;
    };
    QHash<int, SentSegment> m_autoSegments;  // auto-detect requests awaiting a result
    int m_segmentsSent = 0;
    int m_failures = 0;
    QString m_lastError;
};
