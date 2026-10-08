#include "ollamaclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

OllamaClient::OllamaClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_baseUrl(QStringLiteral("http://127.0.0.1:11434"))
{
}

OllamaClient::~OllamaClient()
{
    abortRequest();
}

void OllamaClient::setBaseUrl(const QString &baseUrl)
{
    m_baseUrl = baseUrl.trimmed();
    if (m_baseUrl.endsWith(QLatin1Char('/'))) {
        m_baseUrl.chop(1);
    }
}

QString OllamaClient::baseUrl() const
{
    return m_baseUrl;
}

bool OllamaClient::isBusy() const
{
    return m_reply != nullptr;
}

void OllamaClient::sendChat(const QString &model, const QJsonArray &messages)
{
    abortRequest();

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("stream"), true);

    const QUrl url(m_baseUrl + QStringLiteral("/api/chat"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    m_lineBuffer.clear();
    m_reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(m_reply, &QNetworkReply::readyRead, this, &OllamaClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &OllamaClient::onFinished);
}

void OllamaClient::abortRequest()
{
    if (!m_reply) {
        return;
    }

    m_reply->abort();
    cleanupReply();
}

void OllamaClient::onReadyRead()
{
    if (!m_reply) {
        return;
    }

    m_lineBuffer.append(m_reply->readAll());

    int newlineIndex = -1;
    while ((newlineIndex = m_lineBuffer.indexOf('\n')) != -1) {
        QByteArray line = m_lineBuffer.left(newlineIndex).trimmed();
        m_lineBuffer.remove(0, newlineIndex + 1);

        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        const QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("error"))) {
            emit errorOccurred(obj.value(QStringLiteral("error")).toString());
            continue;
        }

        const QJsonObject message = obj.value(QStringLiteral("message")).toObject();
        const QString content = message.value(QStringLiteral("content")).toString();
        if (!content.isEmpty()) {
            emit responseChunk(content);
        }
    }
}

void OllamaClient::onFinished()
{
    if (!m_reply) {
        return;
    }

    if (m_reply->error() == QNetworkReply::OperationCanceledError) {
        cleanupReply();
        return;
    }

    if (!m_lineBuffer.trimmed().isEmpty()) {
        onReadyRead();
    }

    if (m_reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(m_reply->errorString());
    } else {
        emit responseFinished();
    }

    cleanupReply();
}

void OllamaClient::cleanupReply()
{
    if (!m_reply) {
        return;
    }

    m_reply->deleteLater();
    m_reply = nullptr;
    m_lineBuffer.clear();
}
