#include "WhisperServerProcess.h"
#include "stt/WhisperServerClient.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

// Supervision tests against tests/fixtures/whisper/fake_whisper_server.py, which
// behaves like whisper.cpp's whisper-server (flags, /health, /inference).
// Unix only: Windows cannot run a .py file as the server executable.

namespace {

quint16 freePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return probe.serverPort();
}

bool waitForState(WhisperServerProcess &p, WhisperServerProcess::State state, int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        if (p.state() == state) {
            return true;
        }
        QTest::qWait(50);
    }
    return p.state() == state;
}

bool healthOk(quint16 port)
{
    WhisperServerClient client;
    client.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port)));
    QSignalSpy spy(&client, &WhisperServerClient::healthChecked);
    client.checkHealth(1000);
    return spy.wait(3000) && spy.first().at(0).toBool();
}

} // namespace

class TestWhisperServerProcess : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void argumentsMatchWhisperServerFlags();
    void failsFastWhenExeOrModelMissing();
    void startsAndBecomesReady();
    void stopLeavesNoServerRunning();
    void restartsAfterCrash();
    void unknownArgumentIsNotRetried();
    void adoptsServerAlreadyOnPort();

private:
    WhisperServerProcess::Settings settings(quint16 port) const;
    QString m_fake;
    QTemporaryDir m_dir;
};

void TestWhisperServerProcess::initTestCase()
{
#ifdef Q_OS_WIN
    QSKIP("fake_whisper_server.py cannot be started directly on Windows");
#endif
    if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()) {
        QSKIP("python3 not found");
    }
    m_fake = QDir(QStringLiteral(EAGLE_FIXTURES_DIR)).filePath(QStringLiteral("whisper/fake_whisper_server.py"));
    QVERIFY(QFileInfo::exists(m_fake));
    QVERIFY(m_dir.isValid());
}

void TestWhisperServerProcess::init()
{
    qunsetenv("FAKE_CRASH_AFTER_MS");
    qunsetenv("FAKE_BAD_ARG");
    qunsetenv("FAKE_LOAD_MS");
}

WhisperServerProcess::Settings TestWhisperServerProcess::settings(quint16 port) const
{
    WhisperServerProcess::Settings s;
    s.exe = m_fake;
    s.model = m_fake;  // any existing file
    s.port = port;
    s.threads = 2;
    s.language = QStringLiteral("hi");
    s.logFile = m_dir.filePath(QStringLiteral("whisper-server-%1.log").arg(port));
    s.startTimeoutMs = 10000;
    return s;
}

void TestWhisperServerProcess::argumentsMatchWhisperServerFlags()
{
    WhisperServerProcess::Settings s;
    s.model = QStringLiteral("/models/ggml-base.bin");
    s.port = 8178;
    s.threads = 3;
    s.language = QStringLiteral("auto");
    const QStringList args = WhisperServerProcess::buildArguments(s);
    QCOMPARE(args, QStringList({QStringLiteral("-m"), QDir::toNativeSeparators(s.model),
                                QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                                QStringLiteral("--port"), QStringLiteral("8178"),
                                QStringLiteral("-t"), QStringLiteral("3"),
                                QStringLiteral("-l"), QStringLiteral("auto")}));
}

void TestWhisperServerProcess::failsFastWhenExeOrModelMissing()
{
    WhisperServerProcess p;
    WhisperServerProcess::Settings s = settings(freePort());
    s.exe = m_dir.filePath(QStringLiteral("missing-whisper-server"));
    p.start(s);
    QCOMPARE(p.state(), WhisperServerProcess::State::Failed);
    QVERIFY(p.detail().contains(QStringLiteral("not found")));

    WhisperServerProcess q;
    s = settings(freePort());
    s.model = m_dir.filePath(QStringLiteral("missing.bin"));
    q.start(s);
    QCOMPARE(q.state(), WhisperServerProcess::State::Failed);
    QVERIFY(q.detail().contains(QStringLiteral("model")));
}

void TestWhisperServerProcess::startsAndBecomesReady()
{
    const quint16 port = freePort();
    WhisperServerProcess p;
    p.start(settings(port));
    QVERIFY(waitForState(p, WhisperServerProcess::State::Ready, 8000));
    QVERIFY(!p.adopted());
    QVERIFY2(p.detail().contains(QStringLiteral("language hi")), qPrintable(p.detail()));
    QVERIFY(healthOk(port));

    // The server's own output goes to the log file, not to a pipe that could fill up.
    QFile log(settings(port).logFile);
    QVERIFY(log.open(QIODevice::ReadOnly));
    QVERIFY(log.readAll().contains("listening"));
    p.stop();
}

void TestWhisperServerProcess::stopLeavesNoServerRunning()
{
    const quint16 port = freePort();
    {
        WhisperServerProcess p;
        p.start(settings(port));
        QVERIFY(waitForState(p, WhisperServerProcess::State::Ready, 8000));
    }  // destructor kills the child
    QTest::qWait(200);
    QVERIFY(!healthOk(port));
}

void TestWhisperServerProcess::restartsAfterCrash()
{
    qputenv("FAKE_CRASH_AFTER_MS", "700");
    const quint16 port = freePort();
    WhisperServerProcess p;
    QSignalSpy states(&p, &WhisperServerProcess::stateChanged);
    p.start(settings(port));
    QVERIFY(waitForState(p, WhisperServerProcess::State::Ready, 8000));
    QVERIFY(waitForState(p, WhisperServerProcess::State::Starting, 4000));
    QVERIFY2(p.detail().contains(QStringLiteral("restarting")), qPrintable(p.detail()));
    QVERIFY(waitForState(p, WhisperServerProcess::State::Ready, 8000));
    qunsetenv("FAKE_CRASH_AFTER_MS");
    p.stop();
    QCOMPARE(p.state(), WhisperServerProcess::State::Stopped);
}

void TestWhisperServerProcess::unknownArgumentIsNotRetried()
{
    qputenv("FAKE_BAD_ARG", "1");
    WhisperServerProcess p;
    p.start(settings(freePort()));
    QVERIFY(waitForState(p, WhisperServerProcess::State::Failed, 8000));
    QVERIFY2(p.detail().contains(QStringLiteral("unknown argument")), qPrintable(p.detail()));
    QTest::qWait(1500);
    QCOMPARE(p.state(), WhisperServerProcess::State::Failed);  // no restart loop
}

void TestWhisperServerProcess::adoptsServerAlreadyOnPort()
{
    const quint16 port = freePort();
    QProcess orphan;
    orphan.start(m_fake, WhisperServerProcess::buildArguments(settings(port)));
    QVERIFY(orphan.waitForStarted(3000));
    QElapsedTimer t;
    t.start();
    while (!healthOk(port) && t.elapsed() < 5000) {
        QTest::qWait(100);
    }

    WhisperServerProcess p;
    p.start(settings(port));
    QVERIFY(waitForState(p, WhisperServerProcess::State::Ready, 5000));
    QVERIFY(p.adopted());
    QVERIFY(p.detail().contains(QStringLiteral("already running")));
    p.stop();
    QVERIFY(healthOk(port));  // Eagle does not kill a server it did not start
    orphan.kill();
    orphan.waitForFinished(2000);
}

QTEST_GUILESS_MAIN(TestWhisperServerProcess)
#include "tst_whisperserverprocess.moc"
