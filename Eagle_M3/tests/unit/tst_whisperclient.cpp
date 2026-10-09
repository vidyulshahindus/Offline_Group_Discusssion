#include "WhisperServerClient.h"

#include "MockWhisperServer.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>
#include <QTimer>

namespace {

QByteArray pcm(int ms)
{
    return QByteArray(ms * 32, '\0');  // 16 kHz mono PCM16 silence
}

} // namespace

class TestWhisperClient : public QObject
{
    Q_OBJECT

private slots:
    void sendsMultipartFieldsAndParsesText();
    void verboseJsonReturnsLanguage();
    void oneRequestInFlightAndOrdered();
    void enqueueFrontRunsNext();
    void serverErrorFails();
    void retriesWhileServerStarts();
    void timeoutFails();
    void cancelAllSilencesEverything();
    void healthStatuses();
    void parseReplyCases();
};

void TestWhisperClient::sendsMultipartFieldsAndParsesText()
{
    MockWhisper mock;
    QVERIFY(mock.listen());
    mock.responder = [](int, const QByteArray &) {
        return MockWhisper::Reply{200, QStringLiteral("{\"text\":\" कोशिका झिल्ली\\n\"}").toUtf8()};
    };
    WhisperServerClient client;
    client.setBaseUrl(mock.url());
    QSignalSpy done(&client, &WhisperServerClient::transcribed);
    QSignalSpy idle(&client, &WhisperServerClient::idle);
    const int seq = client.enqueue(pcm(500), QStringLiteral("phospholipid, bilayer."), QStringLiteral("hi"), false);
    QVERIFY(done.wait(5000));
    QCOMPARE(done.first().at(0).toInt(), seq);
    QCOMPARE(done.first().at(1).toString(), QStringLiteral(" कोशिका झिल्ली\n"));
    QVERIFY(idle.count() == 1 || idle.wait(1000));
    QVERIFY(client.isIdle());

    const QByteArray req = mock.requests.first();
    QVERIFY(req.startsWith("POST /inference "));
    QVERIFY(req.contains("multipart/form-data"));
    QVERIFY(req.contains("name=\"file\"; filename=\"segment.wav\""));
    QVERIFY(fieldValue(req, "file").startsWith("RIFF"));
    QCOMPARE(fieldValue(req, "file").size(), 44 + 500 * 32);
    QCOMPARE(fieldValue(req, "language"), QByteArray("hi"));
    QCOMPARE(fieldValue(req, "prompt"), QByteArray("phospholipid, bilayer."));
    QCOMPARE(fieldValue(req, "response_format"), QByteArray("json"));
    QCOMPARE(fieldValue(req, "temperature"), QByteArray("0.0"));
    QCOMPARE(fieldValue(req, "temperature_inc"), QByteArray("0.2"));
}

void TestWhisperClient::verboseJsonReturnsLanguage()
{
    MockWhisper mock;
    QVERIFY(mock.listen());
    mock.responder = [](int, const QByteArray &) {
        return MockWhisper::Reply{200, "{\"task\":\"transcribe\",\"language\":\"hindi\",\"duration\":0.5,"
                                       "\"text\":\"kya hai\",\"segments\":[]}"};
    };
    WhisperServerClient client;
    client.setBaseUrl(mock.url());
    QSignalSpy done(&client, &WhisperServerClient::transcribed);
    client.enqueue(pcm(300), QString(), QStringLiteral("auto"), true);
    QVERIFY(done.wait(5000));
    QCOMPARE(done.first().at(2).toString(), QStringLiteral("hindi"));
    QCOMPARE(fieldValue(mock.requests.first(), "response_format"), QByteArray("verbose_json"));
    QCOMPARE(fieldValue(mock.requests.first(), "language"), QByteArray("auto"));
}

void TestWhisperClient::oneRequestInFlightAndOrdered()
{
    MockWhisper mock;
    QVERIFY(mock.listen());
    mock.responder = [](int index, const QByteArray &) {
        return MockWhisper::Reply{200, "{\"text\":\"seg" + QByteArray::number(index) + "\"}", 150};
    };
    WhisperServerClient client;
    client.setBaseUrl(mock.url());
    QSignalSpy done(&client, &WhisperServerClient::transcribed);
    QSignalSpy idle(&client, &WhisperServerClient::idle);
    const int a = client.enqueue(pcm(100), {}, QStringLiteral("en"), false);
    const int b = client.enqueue(pcm(100), {}, QStringLiteral("en"), false);
    const int c = client.enqueue(pcm(100), {}, QStringLiteral("en"), false);
    QCOMPARE(client.pendingCount(), 3);
    QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 5000);
    QCOMPARE(done.count(), 3);
    QCOMPARE(done.at(0).at(0).toInt(), a);
    QCOMPARE(done.at(1).at(0).toInt(), b);
    QCOMPARE(done.at(2).at(0).toInt(), c);
    QCOMPARE(done.at(2).at(1).toString(), QStringLiteral("seg2"));
    QCOMPARE(mock.maxOpen, 1);
}

void TestWhisperClient::enqueueFrontRunsNext()
{
    MockWhisper mock;
    QVERIFY(mock.listen());
    mock.responder = [](int index, const QByteArray &) {
        return MockWhisper::Reply{200, "{\"text\":\"r" + QByteArray::number(index) + "\"}", 100};
    };
    WhisperServerClient client;
    client.setBaseUrl(mock.url());
    QSignalSpy done(&client, &WhisperServerClient::transcribed);
    QSignalSpy idle(&client, &WhisperServerClient::idle);
    const int a = client.enqueue(pcm(100), {}, QStringLiteral("auto"), true);  // in flight
    const int b = client.enqueue(pcm(100), {}, QStringLiteral("hi"), false);
    const int redo = client.enqueueFront(pcm(100), {}, QStringLiteral("hi"), false);
    QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 5000);
    QCOMPARE(done.count(), 3);
    QCOMPARE(done.at(0).at(0).toInt(), a);
    QCOMPARE(done.at(1).at(0).toInt(), redo);
    QCOMPARE(done.at(2).at(0).toInt(), b);
}

void TestWhisperClient::serverErrorFails()
{
    MockWhisper mock;
    QVERIFY(mock.listen());
    mock.responder = [](int, const QByteArray &) {
        return MockWhisper::Reply{500, "{\"error\":\"failed to process audio\"}"};
    };
    WhisperServerClient client;
    client.setBaseUrl(mock.url());
    QSignalSpy failed(&client, &WhisperServerClient::failed);
    QSignalSpy idle(&client, &WhisperServerClient::idle);
    client.enqueue(pcm(100), {}, QStringLiteral("en"), false);
    QVERIFY(failed.wait(5000));
    QVERIFY(failed.first().at(1).toString().contains(QStringLiteral("failed to process audio")));
    QVERIFY(idle.count() == 1 || idle.wait(1000));
}

void TestWhisperClient::retriesWhileServerStarts()
{
    quint16 port = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        port = probe.serverPort();
    }
    WhisperServerClient client;
    client.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port)));
    client.setStartupRetryMs(5000);
    QSignalSpy done(&client, &WhisperServerClient::transcribed);
    QSignalSpy failed(&client, &WhisperServerClient::failed);
    client.enqueue(pcm(100), {}, QStringLiteral("en"), false);

    MockWhisper mock;
    QTimer::singleShot(900, &mock, [&mock, port]() { QVERIFY(mock.listen(port)); });
    QVERIFY(done.wait(6000));
    QCOMPARE(failed.count(), 0);
}

void TestWhisperClient::timeoutFails()
{
    MockWhisper mock;
    QVERIFY(mock.listen());
    mock.responder = [](int, const QByteArray &) { return MockWhisper::Reply{200, {}, 0, true}; };
    WhisperServerClient client;
    client.setBaseUrl(mock.url());
    client.setRequestTimeoutMs(1000);
    QSignalSpy failed(&client, &WhisperServerClient::failed);
    client.enqueue(pcm(100), {}, QStringLiteral("en"), false);
    QVERIFY(failed.wait(4000));
    QVERIFY(failed.first().at(1).toString().contains(QStringLiteral("did not answer")));
    QVERIFY(client.isIdle());
}

void TestWhisperClient::cancelAllSilencesEverything()
{
    MockWhisper mock;
    QVERIFY(mock.listen());
    mock.responder = [](int, const QByteArray &) { return MockWhisper::Reply{200, "{\"text\":\"x\"}", 300}; };
    WhisperServerClient client;
    client.setBaseUrl(mock.url());
    QSignalSpy done(&client, &WhisperServerClient::transcribed);
    QSignalSpy failed(&client, &WhisperServerClient::failed);
    client.enqueue(pcm(100), {}, QStringLiteral("en"), false);
    client.enqueue(pcm(100), {}, QStringLiteral("en"), false);
    QTest::qWait(50);
    client.cancelAll();
    QVERIFY(client.isIdle());
    QTest::qWait(700);
    QCOMPARE(done.count(), 0);
    QCOMPARE(failed.count(), 0);
}

void TestWhisperClient::healthStatuses()
{
    for (const int status : {200, 404, 503}) {
        MockWhisper mock;
        QVERIFY(mock.listen());
        mock.responder = [status](int, const QByteArray &) {
            return MockWhisper::Reply{status, status == 200 ? "{\"status\":\"ok\"}" : "{}"};
        };
        WhisperServerClient client;
        client.setBaseUrl(mock.url());
        QSignalSpy health(&client, &WhisperServerClient::healthChecked);
        client.checkHealth(2000);
        QVERIFY(health.wait(4000));
        QCOMPARE(health.first().at(0).toBool(), status != 503);
        QCOMPARE(health.first().at(1).toInt(), status);
        QVERIFY(mock.requests.first().startsWith("GET /health "));
    }
    // Nothing listening: not ok, status 0.
    quint16 port = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        port = probe.serverPort();
    }
    WhisperServerClient client;
    client.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port)));
    QSignalSpy health(&client, &WhisperServerClient::healthChecked);
    client.checkHealth(1000);
    QVERIFY(health.wait(3000));
    QCOMPARE(health.first().at(0).toBool(), false);
    QCOMPARE(health.first().at(1).toInt(), 0);
}

void TestWhisperClient::parseReplyCases()
{
    QString text;
    QString lang;
    QString error;
    QVERIFY(WhisperServerClient::parseReply("{\"text\":\"hello\"}", &text, &lang, &error));
    QCOMPARE(text, QStringLiteral("hello"));
    QVERIFY(lang.isEmpty());
    QVERIFY(!WhisperServerClient::parseReply("{\"error\":\"bad\"}", &text, &lang, &error));
    QCOMPARE(error, QStringLiteral("bad"));
    QVERIFY(!WhisperServerClient::parseReply("hello", &text, &lang, &error));
    QVERIFY(!WhisperServerClient::parseReply("{\"status\":\"ok\"}", &text, &lang, &error));
}

QTEST_GUILESS_MAIN(TestWhisperClient)
#include "tst_whisperclient.moc"
