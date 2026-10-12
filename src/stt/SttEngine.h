#pragma once

#include "PromptTypes.h"
#include "Vad.h"
#include "WhisperServerClient.h"
#include "AudioCapture.h"
#include "LanguageService.h"

#include <QObject>

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

private:
    void onPcm(const QByteArray &pcm);
    void onSegmentText(const QString &text);
    void onQueueIdle();
    void enqueueSegment(const QByteArray &pcm16k);
    QString buildPrompt() const;
    QString displayTranscript() const;
    QString whisperLanguageArg() const;

    const AppConfig &m_config;
    ServiceSupervisor &m_supervisor;
    LanguageService m_language;
    AudioCapture m_capture;
    Vad m_vad;
    WhisperServerClient m_client;
    QStringList m_biasTerms;
    SessionLanguage m_lockedReply = SessionLanguage::English;
    QString m_confirmed;
    QString m_lastSegmentTail;
    qint64 m_streamMs = 0;
    bool m_listening = false;
    bool m_waitingDrain = false;
    bool m_playbackActive = false;
};
