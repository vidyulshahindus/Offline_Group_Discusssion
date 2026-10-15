#pragma once

#include <QObject>
#include <QQueue>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

class WhisperServerClient : public QObject
{
    Q_OBJECT

public:
    explicit WhisperServerClient(QObject *parent = nullptr);

    void setBaseUrl(const QUrl &baseUrl);
    void setHallucinationFilter(const QStringList &phrases);

    void enqueue(const QByteArray &pcm16k, const QString &prompt, qint64 segmentStartMs = -1,
                 qint64 segmentEndMs = -1, bool fallbackPass = false);
    void drain();
    bool isBusy() const { return m_inFlight || !m_queue.isEmpty(); }
    int pendingCount() const { return m_queue.size() + (m_inFlight ? 1 : 0); }

signals:
    void segmentText(const QString &text);
    void inferenceFinished(qint64 segmentStartMs, qint64 segmentEndMs, qint64 replyMs, int charCount,
                         const QString &error, bool fallbackPass);
    void queueIdle();
    void errorOccurred(const QString &message);

private:
    struct Job {
        QByteArray pcm;
        QString prompt;
        qint64 segmentStartMs = -1;
        qint64 segmentEndMs = -1;
        bool fallbackPass = false;
        qint64 sentAtMs = 0;
    };

    void pump();
    void onFinished();
    static QString parseText(const QByteArray &body, QString *error);
    static QString filterText(const QString &text, const QStringList &hallucinations);

    QNetworkAccessManager *m_network = nullptr;
    QUrl m_baseUrl;
    QQueue<Job> m_queue;
    Job m_inFlightJob;
    bool m_inFlight = false;
    bool m_draining = false;
    QStringList m_hallucinations;
};
