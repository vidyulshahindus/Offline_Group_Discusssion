#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QTimer>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// HTTP client for whisper.cpp `whisper-server` (examples/server).
// POST <base>/inference, multipart/form-data, one request in flight, FIFO queue,
// so segment results always arrive in speaking order (spec §10.1, §10.3).
class WhisperServerClient : public QObject
{
    Q_OBJECT

public:
    explicit WhisperServerClient(QObject *parent = nullptr);
    ~WhisperServerClient() override;

    void setBaseUrl(const QUrl &url);
    QUrl baseUrl() const { return m_baseUrl; }
    void setRequestTimeoutMs(int ms) { m_timeoutMs = qMax(1000, ms); }
    // While the server is still loading its model, connection-refused requests are
    // retried for this long before failing.
    void setStartupRetryMs(int ms) { m_retryWindowMs = qMax(0, ms); }

    // pcm16k: raw 16 kHz mono PCM16. language: "en", "hi", "auto"...
    // wantLanguage: ask for verbose_json so the reply names the detected language.
    int enqueue(const QByteArray &pcm16k, const QString &prompt, const QString &language,
                bool wantLanguage);
    // Same, but runs before anything already queued (used to redo a segment
    // without breaking the speaking order).
    int enqueueFront(const QByteArray &pcm16k, const QString &prompt, const QString &language,
                     bool wantLanguage);
    void cancelAll();
    // Called when a request is about to be sent; its result replaces the job's own
    // prompt. Lets the bias prompt include the text of every earlier segment even
    // if this segment was queued before that text came back.
    void setPromptProvider(std::function<QString()> provider) { m_promptProvider = std::move(provider); }
    // Changes the language of jobs still waiting in the queue (not the one in
    // flight). Returns their sequence numbers.
    QList<int> retargetQueued(const QString &fromLanguage, const QString &toLanguage);
    bool isIdle() const { return !m_busy && m_queue.isEmpty(); }
    int pendingCount() const { return int(m_queue.size()) + (m_busy ? 1 : 0); }

    // GET <base>/health. ok=true for 200, or 404 (older servers have no /health but
    // only listen after the model is loaded).
    void checkHealth(int timeoutMs = 1000);

    // Parses a /inference JSON reply. Returns false and fills error on failure.
    static bool parseReply(const QByteArray &body, QString *text, QString *language, QString *error);

signals:
    void transcribed(int seq, const QString &text, const QString &language, qint64 elapsedMs);
    void failed(int seq, const QString &error);
    void idle();
    void healthChecked(bool ok, int httpStatus, const QString &detail);

private:
    struct Job {
        int seq = 0;
        QByteArray wav;
        QString prompt;
        QString language;
        bool wantLanguage = false;
        qint64 firstTryMs = -1;
    };

    int addJob(const QByteArray &pcm16k, const QString &prompt, const QString &language,
               bool wantLanguage, bool front);
    void startNext();
    void onReplyFinished();
    void onTimeout();
    void finishJob();

    QNetworkAccessManager *m_network = nullptr;
    QUrl m_baseUrl = QUrl(QStringLiteral("http://127.0.0.1:8178"));
    QQueue<Job> m_queue;
    Job m_current;
    bool m_busy = false;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    QTimer m_retry;
    QElapsedTimer m_clock;
    QElapsedTimer m_requestClock;
    int m_timeoutMs = 20000;
    int m_retryWindowMs = 15000;
    int m_nextSeq = 0;
    std::function<QString()> m_promptProvider;
};
