#pragma once

#include <QAudioDevice>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>
#include <QVector>

class QAudioOutput;
class QMediaPlayer;

class SpeechQueuePlayer : public QObject
{
    Q_OBJECT

public:
    static constexpr int kTestToneTurnId = -2;

    explicit SpeechQueuePlayer(QObject *parent = nullptr);
    ~SpeechQueuePlayer() override;

    void setOutputDevice(const QAudioDevice &device);
    void setVolume(float volume);
    void setSentenceSilenceMs(int milliseconds);

    void enqueueWav(int turnId, int sentenceIndex, const QString &wavPath,
                    bool countsTowardT1 = true, bool deleteAfterPlayback = true);
    void markInputComplete();
    void dropFillers();
    void playTestTone(int milliseconds = 1000);
    void stop();

    bool isPlaying() const;
    bool isPlaybackBusy() const;
    qint64 lastInterSentenceGapMs() const { return m_lastGapMs; }

signals:
    void speechStarted(int turnId);
    void sentenceStarted(int sentenceIndex, qint64 durationMs);
    void speechFinished(int turnId, bool cancelled);
    void firstAudioDelivered(int turnId);
    void level(float rms);
    void playbackError(const QString &message);

private:
    struct Item {
        int turnId = -1;
        int sentenceIndex = -1;
        QString wavPath;
        bool countsTowardT1 = true;
        bool deleteAfter = true;
        qint64 wavDurationMs = 0;
        QVector<float> envelope;
    };

    void wirePlayer(int index);
    void bindOutput(int index);
    void loadPlayer(int index, const QString &path);
    void preloadBack();
    void tryPlayNext();
    void startFront();
    void onPlaybackState(int index, int state);
    void onMediaStatus(int index, int status);
    void onPosition(int index, qint64 positionMs);
    void onPlayerError(int index, const QString &error);
    void finishTurn(bool cancelled);
    void clearQueue(bool deleteFiles);
    void releaseItem(const Item &item);
    void emitEnvelope(qint64 positionMs);
    static QString writeToneWav(int milliseconds);
    static Item inspectWav(const QString &path, int turnId, int sentenceIndex, bool countsTowardT1,
                           bool deleteAfter);

    QAudioOutput *m_output = nullptr;
    QMediaPlayer *m_players[2] = {nullptr, nullptr};
    QString m_loadedPath[2];
    int m_front = 0;
    Item m_current;
    bool m_haveCurrent = false;
    bool m_awaitingEnd = false;
    bool m_inputComplete = false;
    bool m_t1Emitted = false;
    bool m_finishing = false;
    QQueue<Item> m_pending;
    QTimer m_silenceTimer;
    qint64 m_endOfMediaMs = -1;
    qint64 m_lastGapMs = -1;
    int m_silenceMs = 200;
    int m_activeTurnId = -1;
    bool m_destroying = false;
};
