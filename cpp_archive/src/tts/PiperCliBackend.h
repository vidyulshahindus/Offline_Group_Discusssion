#pragma once

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QString>
#include <QTimer>

class PiperCliBackend : public QObject
{
    Q_OBJECT

public:
    struct Job {
        int turnId = -1;
        int sentenceIndex = -1;
        QString text;
        QString outputPath;
        qint64 queuedAtMs = 0;
    };

    explicit PiperCliBackend(QObject *parent = nullptr);
    ~PiperCliBackend() override;

    void configure(const QString &executable, const QString &modelOnnx, double lengthScale);
    bool isConfigured() const;
    bool isWorkerRunning() const;
    bool hasPendingJobs() const;

    void startWorker();
    void stopWorker();
    void synthesize(int turnId, int sentenceIndex, const QString &text);

    double lengthScale() const { return m_lengthScale; }
    qint64 lastColdSynthMs() const { return m_lastColdSynthMs; }
    qint64 lastWarmSynthMs() const { return m_lastWarmSynthMs; }

signals:
    void wavReady(int turnId, int sentenceIndex, const QString &wavPath, qint64 synthMs);
    void synthesisFailed(int turnId, int sentenceIndex, const QString &error);

private:
    void onStdoutReady();
    void onStderrReady();
    void onJobTimeout();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void pumpQueue();
    void completeJob(const QString &wavPath);
    void failFrontJob(const QString &error);
    QString nextOutputPath() const;

    QString m_executable;
    QString m_modelOnnx;
    QString m_configJson;
    double m_lengthScale = 1.1;
    QString m_outputDir;
    QProcess *m_process = nullptr;
    QQueue<Job> m_queue;
    bool m_waitingForPath = false;
    QByteArray m_stdoutBuffer;
    Job m_activeJob;
    int m_jobsCompletedOnWorker = 0;
    qint64 m_lastColdSynthMs = -1;
    qint64 m_lastWarmSynthMs = -1;
    qint64 m_jobStartedMs = 0;
    QTimer m_jobTimer;
};
