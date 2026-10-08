#include "AppConfig.h"
#include "OllamaBackend.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

class TestOllamaBackend : public QObject
{
    Q_OBJECT

private slots:
    void streamWithContentEmitsFinished();
    void streamThinkingOnlyEmitsFailed();
    void streamThinkingOnlyRetriesOnceWithThinkFalse();
};

static QByteArray httpNdjsonResponse(const QByteArray &lines)
{
    return QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
           + QByteArray::number(lines.size()) + QByteArrayLiteral("\r\n\r\n") + lines;
}

static QByteArray httpRequestBody(const QByteArray &raw)
{
    const int headerEnd = raw.indexOf("\r\n\r\n");
    return headerEnd >= 0 ? raw.mid(headerEnd + 4) : raw;
}

static QString writeTempConfig(quint16 port)
{
    QJsonObject root = AppConfig::defaultDocument();
    QJsonObject llm = root.value(QStringLiteral("llm")).toObject();
    llm.insert(QStringLiteral("base_url"),
               QStringLiteral("http://127.0.0.1:%1").arg(port));
    root.insert(QStringLiteral("llm"), llm);

    const QString path =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("eaglegd_tst_ollama_config.json"));
    QFile file(path);
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(QJsonDocument(root).toJson());
    return path;
}

void TestOllamaBackend::streamWithContentEmitsFinished()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    connect(&server, &QTcpServer::newConnection, &server, [&server]() {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
            socket->readAll();
            const QByteArray lines =
                QByteArrayLiteral("{\"message\":{\"role\":\"assistant\",\"content\":\"Hello\"},\"done\":false}\n")
                + QByteArrayLiteral(
                      "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},\"done\":true,"
                      "\"done_reason\":\"stop\",\"prompt_eval_count\":12,\"eval_count\":3}\n");
            socket->write(httpNdjsonResponse(lines));
            socket->flush();
            socket->disconnectFromHost();
        });
    });

    AppConfig config;
    QVERIFY(config.load(writeTempConfig(server.serverPort())));

    OllamaBackend backend(config);
    QEventLoop loop;
    QString fullText;
    connect(&backend, &OllamaBackend::finished, &loop,
            [&](int, const QString &text, const LlmMetrics &) {
                fullText = text;
                loop.quit();
            });
    connect(&backend, &OllamaBackend::failed, &loop, [&](int, const QString &, bool) { loop.quit(); });

    LlmRequest req;
    req.model = QStringLiteral("eagle-clm");
    req.turnId = 1;
    LlmMessage user;
    user.role = QStringLiteral("user");
    user.content = QStringLiteral("Hi");
    req.messages = {user};

    backend.start(req);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    QCOMPARE(fullText, QStringLiteral("Hello"));
}

void TestOllamaBackend::streamThinkingOnlyEmitsFailed()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    connect(&server, &QTcpServer::newConnection, &server, [&server]() {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
            socket->readAll();
            const QByteArray lines =
                QByteArrayLiteral(
                    "{\"message\":{\"role\":\"assistant\",\"content\":\"\",\"thinking\":\"secret\"},\"done\":false}\n")
                + QByteArrayLiteral(
                      "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},\"done\":true,"
                      "\"done_reason\":\"stop\"}\n");
            socket->write(httpNdjsonResponse(lines));
            socket->flush();
            socket->disconnectFromHost();
        });
    });

    AppConfig config;
    QVERIFY(config.load(writeTempConfig(server.serverPort())));

    OllamaBackend backend(config);
    QEventLoop loop;
    QString error;
    connect(&backend, &OllamaBackend::failed, &loop,
            [&](int, const QString &message, bool) {
                error = message;
                loop.quit();
            });
    connect(&backend, &OllamaBackend::finished, &loop, [&](int, const QString &, const LlmMetrics &) {
        loop.quit();
    });

    LlmRequest req;
    req.model = QStringLiteral("hybrid-7b-test");
    req.turnId = 2;
    LlmMessage user;
    user.role = QStringLiteral("user");
    user.content = QStringLiteral("Hi");
    req.messages = {user};

    backend.start(req);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("no speakable content")));
}

void TestOllamaBackend::streamThinkingOnlyRetriesOnceWithThinkFalse()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    connect(&server, &QTcpServer::newConnection, &server, [&server]() {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
            const QByteArray body = httpRequestBody(socket->readAll());
            const QByteArray thinkingOnly =
                QByteArrayLiteral(
                    "{\"message\":{\"role\":\"assistant\",\"content\":\"\",\"thinking\":\"x\"},\"done\":false}\n")
                + QByteArrayLiteral(
                      "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},\"done\":true,"
                      "\"done_reason\":\"stop\"}\n");
            const QByteArray withContent =
                QByteArrayLiteral("{\"message\":{\"role\":\"assistant\",\"content\":\"Hello\"},\"done\":false}\n")
                + QByteArrayLiteral(
                      "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},\"done\":true,"
                      "\"done_reason\":\"stop\"}\n");
            const bool thinkFalse = body.contains("\"think\":false");
            socket->write(httpNdjsonResponse(thinkFalse ? withContent : thinkingOnly));
            socket->flush();
            socket->disconnectFromHost();
        });
    });

    AppConfig config;
    QVERIFY(config.load(writeTempConfig(server.serverPort())));

    OllamaBackend backend(config);
    QEventLoop loop;
    QString fullText;
    connect(&backend, &OllamaBackend::finished, &loop,
            [&](int, const QString &text, const LlmMetrics &) {
                fullText = text;
                loop.quit();
            });
    connect(&backend, &OllamaBackend::failed, &loop, [&](int, const QString &, bool) { loop.quit(); });

    LlmRequest req;
    req.model = QStringLiteral("hybrid-7b-test");
    req.turnId = 3;
    LlmMessage user;
    user.role = QStringLiteral("user");
    user.content = QStringLiteral("Hi");
    req.messages = {user};

    backend.start(req);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    QCOMPARE(fullText, QStringLiteral("Hello"));
}

QTEST_MAIN(TestOllamaBackend)
#include "tst_ollamabackend.moc"
