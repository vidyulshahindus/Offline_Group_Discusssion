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

    void enqueue(const QByteArray &pcm16k, const QString &prompt);
    void drain();
    bool isBusy() const { return m_inFlight || !m_queue.isEmpty(); }
    int pendingCount() const { return m_queue.size() + (m_inFlight ? 1 : 0); }

signals:
    void segmentText(const QString &text);
    void queueIdle();
    void errorOccurred(const QString &message);

private:
    struct Job {
        QByteArray pcm;
        QString prompt;
    };

    void pump();
    void onFinished();
    static QString parseText(const QByteArray &body, QString *error);
    static QString filterText(const QString &text, const QStringList &hallucinations);

    QNetworkAccessManager *m_network = nullptr;
    QUrl m_baseUrl;
    QQueue<Job> m_queue;
    bool m_inFlight = false;
    bool m_draining = false;
    QStringList m_hallucinations;
};
