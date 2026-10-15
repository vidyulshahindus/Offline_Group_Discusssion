#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QString>

#include <QProcess>

class QAudioSource;
class QIODevice;
class QTimer;

class SpeechInput : public QObject
{
    Q_OBJECT

public:
    explicit SpeechInput(QObject *parent = nullptr);
    ~SpeechInput() override;

    QList<QAudioDevice> microphones() const;
    void setMicrophone(const QAudioDevice &device);

    bool isRecording() const;
    bool isBusy() const;

    void setWhisperPaths(const QString &cliPath, const QString &modelPath);

public slots:
    void startRecording();
    void stopRecording();

signals:
    void recordingChanged(bool recording);
    void partialTranscription(const QString &text);
    void transcriptionReady(const QString &text);
    void errorOccurred(const QString &message);
    void statusChanged(const QString &status);

private slots:
    void onAudioReady();
    void onPartialTick();
    void onWhisperFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void startWhisper(bool finalPass);
    QString whisperCliPath() const;
    QString whisperModelPath() const;
    bool writeWavFile(const QString &path, const QByteArray &pcm16k) const;
    QByteArray toPcm16kMono(const QByteArray &raw) const;
    static QString textFromWhisperOutput(const QString &stdoutText);

    QAudioDevice m_device;
    QAudioFormat m_format;
    QAudioSource *m_source = nullptr;
    QIODevice *m_capture = nullptr;
    QProcess *m_whisper = nullptr;
    QTimer *m_partialTimer = nullptr;
    QByteArray m_captured;
    QString m_wavPath;
    QString m_whisperCliOverride;
    QString m_whisperModelOverride;
    bool m_recording = false;
    bool m_finalPass = false;
    bool m_finalQueued = false;
};
