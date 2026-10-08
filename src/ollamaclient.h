#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class OllamaClient : public QObject
{
    Q_OBJECT

public:
    explicit OllamaClient(QObject *parent = nullptr);
    ~OllamaClient() override;

    void setBaseUrl(const QString &baseUrl);
    QString baseUrl() const;

    bool isBusy() const;

public slots:
    void sendChat(const QString &model, const QJsonArray &messages);
    void abortRequest();

signals:
    void responseChunk(const QString &text);
    void responseFinished();
    void errorOccurred(const QString &message);

private slots:
    void onReadyRead();
    void onFinished();

private:
    void cleanupReply();

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QString m_baseUrl;
    QByteArray m_lineBuffer;
};
