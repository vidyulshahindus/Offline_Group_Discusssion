#include "AppConfig.h"
#include "OllamaBackend.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
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
    void unitChangeDuringPreloadDoesNotCrashAndWarmsLatestPrompt();
    void rapidWarmUpsSerializeAndKeepLastPrompt();
    void abortDuringAuxDoesNotCrashAndNextWarmUpWorks();
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

static QByteArray httpOkJson(const QByteArray &jsonBody)
{
    return QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
           + QByteArray::number(jsonBody.size()) + QByteArrayLiteral("\r\n\r\n") + jsonBody;
}

class MockOllamaHttpServer : public QObject
{
    Q_OBJECT

public:
    explicit MockOllamaHttpServer(QObject *parent = nullptr) : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, &MockOllamaHttpServer::onNewConnection);
    }

    bool listen()
    {
        return m_server.listen(QHostAddress::LocalHost);
    }

    quint16 serverPort() const { return m_server.serverPort(); }

    int maxConcurrentPosts() const { return m_maxConcurrentPosts; }
    int warmUpPostCount() const { return m_warmUpPostCount; }
    QString lastWarmSystemPrompt() const { return m_lastWarmSystemPrompt; }

    void setHoldAuxUntilReleased(bool hold) { m_holdAux = hold; }
    void releaseAux() { m_holdAux = false; }

private slots:
    void onNewConnection()
    {
        QTcpSocket *socket = m_server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
            const QByteArray raw = socket->readAll();
            const QByteArray body = httpRequestBody(raw);
            const bool isShow = raw.contains("/api/show");
            const bool isChat = raw.contains("/api/chat");

            if (isShow) {
                const QByteArray json =
                    QByteArrayLiteral("{\"model\":\"eagle-clm\",\"capabilities\":[]}");
                socket->write(httpOkJson(json));
                socket->flush();
                socket->disconnectFromHost();
                return;
            }

            if (!isChat) {
                socket->disconnectFromHost();
                return;
            }

            ++m_activePosts;
            m_maxConcurrentPosts = qMax(m_maxConcurrentPosts, m_activePosts);

            while (m_holdAux) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }

            const QJsonDocument doc = QJsonDocument::fromJson(body);
            const QJsonArray messages = doc.object().value(QStringLiteral("messages")).toArray();
            const bool isWarmUp =
                !messages.isEmpty()
                && messages.at(0).toObject().value(QStringLiteral("role")).toString()
                       == QStringLiteral("system");

            if (isWarmUp) {
                ++m_warmUpPostCount;
                m_lastWarmSystemPrompt =
                    messages.at(0).toObject().value(QStringLiteral("content")).toString();
            }

            const QByteArray json =
                QByteArrayLiteral("{\"message\":{\"role\":\"assistant\",\"content\":\"Ready\"},"
                                  "\"done\":true,\"done_reason\":\"stop\"}");
            socket->write(httpOkJson(json));
            socket->flush();
            socket->disconnectFromHost();
            --m_activePosts;
        });
    }

private:
    QTcpServer m_server;
    int m_activePosts = 0;
    int m_maxConcurrentPosts = 0;
    int m_warmUpPostCount = 0;
    QString m_lastWarmSystemPrompt;
    bool m_holdAux = false;
};

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

void TestOllamaBackend::unitChangeDuringPreloadDoesNotCrashAndWarmsLatestPrompt()
{
    MockOllamaHttpServer server;
    QVERIFY(server.listen());
    server.setHoldAuxUntilReleased(true);

    AppConfig config;
    QVERIFY(config.load(writeTempConfig(server.serverPort())));

    OllamaBackend backend(config);
    QEventLoop loop;
    bool startupDone = false;
    connect(&backend, &OllamaBackend::startupSequenceFinished, &loop, [&](bool ok) {
        startupDone = ok;
        loop.quit();
    });

    const QString startupPrompt = QStringLiteral("STARTUP_PROMPT");
    const QString unitPrompt = QStringLiteral("UNIT_CARD_PROMPT");
    backend.runStartupSequence(QStringLiteral("eagle-clm"), QString(), startupPrompt);

    QTimer::singleShot(30, &backend, [&]() {
        backend.scheduleWarmUp(QStringLiteral("eagle-clm"), unitPrompt);
        server.releaseAux();
    });

    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY(startupDone);
    QCOMPARE(server.warmUpPostCount(), 1);
    QCOMPARE(server.lastWarmSystemPrompt(), unitPrompt);
    QCOMPARE(server.maxConcurrentPosts(), 1);
    QCOMPARE(backend.auxConcurrentCount(), 0);
}

void TestOllamaBackend::rapidWarmUpsSerializeAndKeepLastPrompt()
{
    MockOllamaHttpServer server;
    QVERIFY(server.listen());

    AppConfig config;
    QVERIFY(config.load(writeTempConfig(server.serverPort())));

    OllamaBackend backend(config);
    QEventLoop loop;
    connect(&backend, &OllamaBackend::warmUpFinished, &loop, [&](bool) { loop.quit(); });

    for (int i = 0; i < 5; ++i) {
        backend.scheduleWarmUp(QStringLiteral("eagle-clm"),
                               QStringLiteral("PROMPT_%1").arg(i));
    }

    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();

    QCOMPARE(server.warmUpPostCount(), 1);
    QCOMPARE(server.lastWarmSystemPrompt(), QStringLiteral("PROMPT_4"));
    QVERIFY(server.maxConcurrentPosts() <= 1);
    QCOMPARE(server.maxConcurrentPosts(), 1);
}

void TestOllamaBackend::abortDuringAuxDoesNotCrashAndNextWarmUpWorks()
{
    MockOllamaHttpServer server;
    QVERIFY(server.listen());
    server.setHoldAuxUntilReleased(true);

    AppConfig config;
    QVERIFY(config.load(writeTempConfig(server.serverPort())));

    OllamaBackend backend(config);
    backend.scheduleWarmUp(QStringLiteral("eagle-clm"), QStringLiteral("FIRST"));

    QTimer::singleShot(40, &backend, [&]() {
        backend.abort();
        server.releaseAux();
        backend.scheduleWarmUp(QStringLiteral("eagle-clm"), QStringLiteral("SECOND"));
    });

    QEventLoop loop;
    connect(&backend, &OllamaBackend::warmUpFinished, &loop, [&](bool ok) {
        if (ok) {
            loop.quit();
        }
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();

    QCOMPARE(server.lastWarmSystemPrompt(), QStringLiteral("SECOND"));
    QCOMPARE(backend.auxConcurrentCount(), 0);
}

QTEST_MAIN(TestOllamaBackend)
#include "tst_ollamabackend.moc"
