#include "WhisperServerClient.h"

#include "WavUtil.h"

#include <QDateTime>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

WhisperServerClient::WhisperServerClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void WhisperServerClient::setBaseUrl(const QUrl &baseUrl)
{
    m_baseUrl = baseUrl;
}

void WhisperServerClient::setHallucinationFilter(const QStringList &phrases)
{
    m_hallucinations = phrases;
}

void WhisperServerClient::enqueue(const QByteArray &pcm16k, const QString &prompt,
                                  qint64 segmentStartMs, qint64 segmentEndMs, bool fallbackPass)
{
    if (pcm16k.size() < 8000) {
        return;
    }
    Job job;
    job.pcm = pcm16k;
    job.prompt = prompt;
    job.segmentStartMs = segmentStartMs;
    job.segmentEndMs = segmentEndMs;
    job.fallbackPass = fallbackPass;
    m_queue.enqueue(job);
    pump();
}

void WhisperServerClient::drain()
{
    m_draining = true;
    if (!m_inFlight && m_queue.isEmpty()) {
        m_draining = false;
        emit queueIdle();
    }
}

void WhisperServerClient::pump()
{
    if (m_inFlight || m_queue.isEmpty() || !m_baseUrl.isValid()) {
        return;
    }
    m_inFlightJob = m_queue.dequeue();
    m_inFlightJob.sentAtMs = QDateTime::currentMSecsSinceEpoch();
    m_inFlight = true;

    auto *filePart = new QHttpPart;
    filePart->setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    filePart->setHeader(QNetworkRequest::ContentDispositionHeader,
                        QStringLiteral("form-data; name=\"file\"; filename=\"segment.wav\""));
    filePart->setBody(WavUtil::pcm16kMonoToWav(m_inFlightJob.pcm));

    auto *tempPart = new QHttpPart;
    tempPart->setHeader(QNetworkRequest::ContentDispositionHeader,
                        QStringLiteral("form-data; name=\"temperature\""));
    tempPart->setBody("0.0");

    auto *tempIncPart = new QHttpPart;
    tempIncPart->setHeader(QNetworkRequest::ContentDispositionHeader,
                           QStringLiteral("form-data; name=\"temperature_inc\""));
    tempIncPart->setBody("0.2");

    auto *formatPart = new QHttpPart;
    formatPart->setHeader(QNetworkRequest::ContentDispositionHeader,
                          QStringLiteral("form-data; name=\"response_format\""));
    formatPart->setBody("json");

    auto *promptPart = new QHttpPart;
    promptPart->setHeader(QNetworkRequest::ContentDispositionHeader,
                          QStringLiteral("form-data; name=\"prompt\""));
    promptPart->setBody(m_inFlightJob.prompt.toUtf8());

    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multi->append(*filePart);
    multi->append(*tempPart);
    multi->append(*tempIncPart);
    multi->append(*formatPart);
    multi->append(*promptPart);

    const QUrl url = m_baseUrl.resolved(QUrl(QStringLiteral("/inference")));
    QNetworkRequest request(url);
    QNetworkReply *reply = m_network->post(request, multi);
    multi->setParent(reply);
    delete filePart;
    delete tempPart;
    delete tempIncPart;
    delete formatPart;
    delete promptPart;

    connect(reply, &QNetworkReply::finished, this, &WhisperServerClient::onFinished);
}

void WhisperServerClient::onFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }
    reply->deleteLater();
    const qint64 replyMs = QDateTime::currentMSecsSinceEpoch() - m_inFlightJob.sentAtMs;
    m_inFlight = false;

    QString error;
    QString text;
    if (reply->error() != QNetworkReply::NoError) {
        error = reply->errorString();
    } else {
        text = parseText(reply->readAll(), &error);
        text = filterText(text, m_hallucinations);
    }

    emit inferenceFinished(m_inFlightJob.segmentStartMs, m_inFlightJob.segmentEndMs, replyMs,
                           text.size(), error, m_inFlightJob.fallbackPass);

    if (!error.isEmpty()) {
        emit errorOccurred(error);
    } else if (!text.isEmpty()) {
        emit segmentText(text);
    }

    if (!m_queue.isEmpty()) {
        pump();
    } else if (m_draining && !m_inFlight) {
        m_draining = false;
        emit queueIdle();
    }
}

QString WhisperServerClient::parseText(const QByteArray &body, QString *error)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        if (error) {
            *error = QStringLiteral("Invalid JSON from whisper-server");
        }
        return {};
    }
    return doc.object().value(QStringLiteral("text")).toString().trimmed();
}

QString WhisperServerClient::filterText(const QString &text, const QStringList &hallucinations)
{
    QString trimmed = text.trimmed();
    for (const QString &h : hallucinations) {
        if (trimmed.compare(h, Qt::CaseInsensitive) == 0) {
            return {};
        }
    }
    if (trimmed.compare(QStringLiteral("you"), Qt::CaseInsensitive) == 0) {
        return {};
    }
    return trimmed;
}
