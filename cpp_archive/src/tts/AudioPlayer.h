#pragma once

#include "PcmConverter.h"
#include "PcmQueueDevice.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QElapsedTimer>
#include <QObject>

#include <memory>

class AudioPlayer : public QObject
{
    Q_OBJECT

public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer() override;

    void setOutputDevice(const QAudioDevice &device);
    void setVolume(float volume);
    void setInputGain(float gain);
    float inputGain() const { return m_inputGain; }
    void markInputComplete();
    // Same convert path enqueuePcm uses (gain after resample, then limiter).
    QByteArray processSpeechPcm(const QByteArray &src22050MonoInt16,
                                PcmConverter::ConversionStats *stats = nullptr) const;
    void enqueuePcm(int turnId, int sentenceIndex, const QByteArray &pcm,
                    bool countsTowardT1 = true);
    void insertSilenceMs(int ms);
    void playTestTone(int milliseconds = 1000);
    void stop();
    bool isPlaying() const;
    bool isPlaybackBusy() const;
    qint64 processedUSecs() const;
    QAudioFormat playbackFormat() const { return m_playbackFormat; }

signals:
    void speechStarted(int turnId);
    void speechFinished(int turnId, bool cancelled);
    void firstAudioDelivered(int turnId);
    void playbackError(const QString &message);

private:
    bool prepareSink();
    void kickSink();
    void restartSink();
    QByteArray convertPcm(const QByteArray &pcm22050Mono) const;
    void scheduleDrainCheck();
    void checkPlaybackDrained();
    void logSink(const char *reason) const;
    void logDurationCheck();
    void beginPlayStats(int turnId);
    void finishCurrentTurn(bool cancelled);
    qint64 segmentPlayedUSecs() const;
    void appendConvertedDump(const QByteArray &pcm);
    QString convertedDumpPath() const;

    PcmQueueDevice m_device;
    std::unique_ptr<QAudioSink> m_sink;
    QAudioFormat m_sourceFormat;
    QAudioFormat m_requestedFormat;
    QAudioFormat m_playbackFormat;
    QAudioDevice m_outputDevice;
    QElapsedTimer m_playWall;
    int m_activeTurnId = -1;
    bool m_cancelled = false;
    bool m_firstAudioSignalled = false;
    bool m_waitingForDrain = false;
    bool m_inputComplete = false;
    qint64 m_lastProcessedUSecs = -1;
    int m_stallTicks = 0;
    float m_inputGain = 0.7f;
    qint64 m_sourceFramesThisPlay = 0;
    qint64 m_convertedBytesThisPlay = 0;
    qint64 m_processedUSecsAtPlayStart = 0;
    qint64 m_processedAccumulated = 0;
    qint64 m_pulledAtSegmentStart = 0;
    qint64 m_pulledAtPlayStart = 0;
    QByteArray m_convertedDump;
};
