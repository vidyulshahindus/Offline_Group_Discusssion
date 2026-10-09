#include "WhisperServerClient.h"

#include "WavUtil.h"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

Q_LOGGING_CATEGORY(lcWhisperClient, "eagle.stt.client")

namespace {

QHttpPart textPart(const QString &name, const QString &value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"%1\"").arg(name));
    part.setBody(value.toUtf8());
    return part;
}

QUrl withPath(const QUrl &base, const QString &endpoint)
{
    QUrl url = base;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    url.setPath(path + endpoint);
    return url;
}

} // namespace

WhisperServerClient::WhisperServerClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &WhisperServerClient::onTimeout);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &WhisperServerClient::startNext);
    m_clock.start();
}

WhisperServerClient::~WhisperServerClient()
{
    cancelAll();
}

void WhisperServerClient::setBaseUrl(const QUrl &url)
{
    if (url.isValid() && !url.isEmpty()) {
        m_baseUrl = url;
    }
}

int WhisperServerClient::enqueue(const QByteArray &pcm16k, const QString &prompt,
                                 const QString &language, bool wantLanguage)
{
    return addJob(pcm16k, prompt, language, wantLanguage, false);
}

int WhisperServerClient::enqueueFront(const QByteArray &pcm16k, const QString &prompt,
                                      const QString &language, bool wantLanguage)
{
    return addJob(pcm16k, prompt, language, wantLanguage, true);
}

int WhisperServerClient::addJob(const QByteArray &pcm16k, const QString &prompt,
                                const QString &language, bool wantLanguage, bool front)
{
    Job job;
    job.seq = ++m_nextSeq;
    job.wav = WavUtil::wrapPcm16Mono(pcm16k, 16000);
    job.prompt = prompt;
    job.language = language;
    job.wantLanguage = wantLanguage;
    if (front) {
        m_queue.prepend(job);
    } else {
        m_queue.enqueue(job);
    }
    qCInfo(lcWhisperClient) << (front ? "enqueue (front) seq" : "enqueue seq") << job.seq << "audioMs" << (pcm16k.size() / 32)
                            << "lang" << language << "queue" << m_queue.size();
    if (!m_busy && !m_retry.isActive()) {
        startNext();
    }
    return job.seq;
}

void WhisperServerClient::startNext()
{
    if (m_busy || m_queue.isEmpty()) {
        return;
    }
    m_current = m_queue.dequeue();
    m_busy = true;
    if (m_current.firstTryMs < 0) {
        m_current.firstTryMs = m_clock.elapsed();
    }

    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart file;
    file.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"file\"; filename=\"segment.wav\""));
    file.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    file.setBody(m_current.wav);
    multi->append(file);
    multi->append(textPart(QStringLiteral("temperature"), QStringLiteral("0.0")));
    multi->append(textPart(QStringLiteral("temperature_inc"), QStringLiteral("0.2")));
    multi->append(textPart(QStringLiteral("response_format"),
                           m_current.wantLanguage ? QStringLiteral("verbose_json")
                                                  : QStringLiteral("json")));
    multi->append(textPart(QStringLiteral("no_timestamps"), QStringLiteral("true")));
    // Unknown fields are ignored by older servers, so these are safe to send.
    multi->append(textPart(QStringLiteral("suppress_non_speech"), QStringLiteral("true")));
    if (!m_current.language.isEmpty()) {
        multi->append(textPart(QStringLiteral("language"), m_current.language));
    }
    const QString prompt = m_promptProvider ? m_promptProvider() : m_current.prompt;
    if (!prompt.isEmpty()) {
        multi->append(textPart(QStringLiteral("prompt"), prompt));
    }

    QNetworkRequest request(withPath(m_baseUrl, QStringLiteral("/inference")));
    m_requestClock.start();
    m_reply = m_network->post(request, multi);
    multi->setParent(m_reply);
    connect(m_reply, &QNetworkReply::finished, this, &WhisperServerClient::onReplyFinished);
    m_timeout.start(m_timeoutMs);
}

void WhisperServerClient::onReplyFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }
    reply->deleteLater();
    if (reply != m_reply) {
        return;
    }
    m_reply = nullptr;
    m_timeout.stop();

    const QNetworkReply::NetworkError error = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const qint64 elapsed = m_requestClock.elapsed();

    const bool notUpYet = error == QNetworkReply::ConnectionRefusedError
                          || (status == 503 && error != QNetworkReply::NoError);
    if (notUpYet && m_clock.elapsed() - m_current.firstTryMs < m_retryWindowMs) {
        qCInfo(lcWhisperClient) << "server not ready, retrying seq" << m_current.seq;
        m_queue.prepend(m_current);
        m_current = Job{};
        m_busy = false;
        m_retry.start(400);
        return;
    }

    const int seq = m_current.seq;
    if (error != QNetworkReply::NoError && status != 200) {
        const QString detail = body.isEmpty() ? reply->errorString()
                                              : QString::fromUtf8(body.left(200)).trimmed();
        qCWarning(lcWhisperClient) << "seq" << seq << "failed" << status << detail;
        emit failed(seq, QStringLiteral("whisper-server: %1").arg(detail));
        finishJob();
        return;
    }

    QString text;
    QString language;
    QString parseError;
    if (!parseReply(body, &text, &language, &parseError)) {
        qCWarning(lcWhisperClient) << "seq" << seq << "bad reply" << parseError;
        emit failed(seq, QStringLiteral("whisper-server: %1").arg(parseError));
        finishJob();
        return;
    }
    qCInfo(lcWhisperClient) << "seq" << seq << "ms" << elapsed << "lang" << language << "chars"
                            << text.size();
    emit transcribed(seq, text, language, elapsed);
    finishJob();
}

void WhisperServerClient::onTimeout()
{
    if (!m_reply) {
        return;
    }
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
    const int seq = m_current.seq;
    qCWarning(lcWhisperClient) << "seq" << seq << "timed out after" << m_timeoutMs << "ms";
    emit failed(seq, QStringLiteral("whisper-server did not answer within %1 s")
                         .arg(m_timeoutMs / 1000));
    finishJob();
}

void WhisperServerClient::finishJob()
{
    m_busy = false;
    m_current = Job{};
    if (m_queue.isEmpty()) {
        emit idle();
    } else {
        startNext();
    }
}

void WhisperServerClient::cancelAll()
{
    m_retry.stop();
    m_timeout.stop();
    m_queue.clear();
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();  // the server aborts decoding when the connection closes
        reply->deleteLater();
    }
    m_busy = false;
    m_current = Job{};
}

QList<int> WhisperServerClient::retargetQueued(const QString &fromLanguage, const QString &toLanguage)
{
    QList<int> changed;
    for (Job &job : m_queue) {
        if (job.language == fromLanguage) {
            job.language = toLanguage;
            job.wantLanguage = false;
            changed.append(job.seq);
        }
    }
    return changed;
}

void WhisperServerClient::checkHealth(int timeoutMs)
{
    QNetworkRequest request(withPath(m_baseUrl, QStringLiteral("/health")));
    request.setTransferTimeout(qMax(200, timeoutMs));
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const bool ok = status == 200 || status == 404;
        QString detail;
        if (status == 0) {
            detail = reply->errorString();
        } else {
            detail = QStringLiteral("HTTP %1 %2").arg(status).arg(QString::fromUtf8(body.left(80)).trimmed());
        }
        emit healthChecked(ok, status, detail);
    });
}

bool WhisperServerClient::parseReply(const QByteArray &body, QString *text, QString *language,
                                     QString *error)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = QStringLiteral("reply is not JSON (%1)").arg(parseError.errorString());
        }
        return false;
    }
    const QJsonObject obj = doc.object();
    if (obj.contains(QStringLiteral("error"))) {
        if (error) {
            *error = obj.value(QStringLiteral("error")).toString();
        }
        return false;
    }
    if (!obj.contains(QStringLiteral("text"))) {
        if (error) {
            *error = QStringLiteral("reply has no text field");
        }
        return false;
    }
    if (text) {
        *text = obj.value(QStringLiteral("text")).toString();
    }
    if (language) {
        *language = obj.value(QStringLiteral("language")).toString();
    }
    return true;
}
