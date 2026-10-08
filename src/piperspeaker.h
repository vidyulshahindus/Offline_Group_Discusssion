#pragma once

#include <QAudioDevice>
#include <QObject>
#include <QProcess>
#include <QString>

class QAudioOutput;
class QMediaPlayer;

class PiperSpeaker : public QObject
{
    Q_OBJECT

public:
    explicit PiperSpeaker(QObject *parent = nullptr);
    ~PiperSpeaker() override;

    bool isBusy() const;
    QList<QAudioDevice> speakers() const;
    void setSpeaker(const QAudioDevice &device);
    void setPiperPaths(const QString &executable, const QString &voiceOnnxPath);

public slots:
    void speak(const QString &text);
    void stop();

signals:
    void speakingChanged(bool speaking);
    void errorOccurred(const QString &message);
    void statusChanged(const QString &status);

private slots:
    void onPiperFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onPlaybackStateChanged();

private:
    QString piperExecutable() const;
    QString voiceModelPath() const;
    void playWav(const QString &path);

    QProcess *m_piper = nullptr;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    QString m_wavPath;
    QString m_piperExeOverride;
    QString m_voiceOnnxOverride;
    bool m_playing = false;
};
