#pragma once

#include "PromptTypes.h"
#include "Vad.h"
#include "WhisperServerClient.h"
#include "AudioCapture.h"
#include "LanguageService.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>

class AppConfig;
class Paths;
class ServiceSupervisor;

class SttEngine : public QObject
{
    Q_OBJECT

public:
    explicit SttEngine(const AppConfig &config, const Paths &paths, ServiceSupervisor &supervisor,
                       QObject *parent = nullptr);

    QList<QAudioDevice> microphones() const;
    void setMicrophone(const QAudioDevice &device);
    void setLockedReplyLanguage(SessionLanguage language);
    void setBiasTerms(const QStringList &terms);
    void setPlaybackActive(bool active);

    bool isListening() const { return m_listening; }
    bool isBusy() const;

    void toggleListening();
    void startListening();
    void stopListening();

signals:
    void listeningChanged(bool listening);
    void partialText(const QString &text);
    void finalText(const QString &text, SessionLanguage replyLanguage);
    void statusChanged(const QString &status);
    void errorOccurred(const QString &message);
    void inputLevel(float normalized);

private:
    void onPcm(const QByteArray &pcm);
    void onSegmentText(const QString &text);
    void onInferenceFinished(qint64 segmentStartMs, qint64 segmentEndMs, qint64 replyMs,
                             int charCount, const QString &error, bool fallbackPass);
    void onQueueIdle();
    void onStatsTick();
    void enqueueSegment(const Vad::Segment &seg, bool fallbackPass = false);
    void appendSessionPcm(const QByteArray &pcm);
    QString buildPrompt() const;
    QString displayTranscript() const;
    QString whisperLanguageArg() const;
    static QString sampleFormatName(QAudioFormat::SampleFormat fmt);
    static float levelFromDb(double db);

    const AppConfig &m_config;
    ServiceSupervisor &m_supervisor;
    LanguageService m_language;
    AudioCapture m_capture;
    Vad m_vad;
    WhisperServerClient m_client;
    QTimer m_statsTimer;
    QStringList m_biasTerms;
    SessionLanguage m_lockedReply = SessionLanguage::English;
    QByteArray m_micDeviceId;
    QString m_confirmed;
    QString m_lastSegmentTail;
    QByteArray m_sessionPcm;
    qint64 m_streamMs = 0;
    qint64 m_sessionAudioMs = 0;
    qint64 m_bytesThisSecond = 0;
    double m_maxFrameDb = -70.0;
    int m_segmentsSent = 0;
    int m_segmentsReturned = 0;
    bool m_gotSegmentText = false;
    bool m_listening = false;
    bool m_waitingDrain = false;
    bool m_pendingFallback = false;
    bool m_playbackActive = false;
    QElapsedTimer m_stopTimer;
    static QHash<QByteArray, double> s_micNoiseFloors;
};
