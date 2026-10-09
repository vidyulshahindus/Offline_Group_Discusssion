#include "SttEngine.h"

#include "MockWhisperServer.h"

#include <QIODevice>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>

#include <cmath>
#include <cstring>

// End-to-end speech-input pipeline without a microphone: PCM is pushed through an
// injected QIODevice, segmented by the VAD and sent to a mock whisper-server.

namespace {

constexpr double kPi = 3.14159265358979323846;

// A QIODevice the test writes captured audio into, like QAudioSource's device.
class FeedDevice : public QIODevice
{
public:
    FeedDevice() { open(QIODevice::ReadOnly); }
    void feed(const QByteArray &bytes)
    {
        m_data.append(bytes);
        emit readyRead();
    }
    qint64 bytesAvailable() const override { return m_data.size() + QIODevice::bytesAvailable(); }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 n = qMin<qint64>(maxSize, m_data.size());
        std::memcpy(data, m_data.constData(), size_t(n));
        m_data.remove(0, n);
        return n;
    }
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QByteArray m_data;
};

struct Span {
    int ms;
    double tone;
};

QByteArray signal16k(const QVector<Span> &spans)
{
    QRandomGenerator rng(11);
    QByteArray out;
    qint64 n = 0;
    for (const Span &s : spans) {
        for (qint64 i = 0; i < qint64(s.ms) * 16; ++i, ++n) {
            double v = (rng.generateDouble() * 2.0 - 1.0) * 0.00308;
            if (s.tone > 0) {
                v += s.tone * std::sin(2.0 * kPi * 300.0 * double(n) / 16000.0);
            }
            const qint16 x = qint16(std::lround(v * 32767.0));
            out.append(reinterpret_cast<const char *>(&x), 2);
        }
    }
    return out;
}

// 16 kHz mono PCM16 -> 48 kHz stereo float (sample-and-hold, fine for this test).
QByteArray to48kStereoFloat(const QByteArray &pcm16k)
{
    QByteArray out;
    for (qsizetype i = 0; i + 1 < pcm16k.size(); i += 2) {
        qint16 s = 0;
        std::memcpy(&s, pcm16k.constData() + i, 2);
        const float f = float(s) / 32768.0f;
        for (int k = 0; k < 6; ++k) {
            out.append(reinterpret_cast<const char *>(&f), 4);
        }
    }
    return out;
}

void feedInChunks(FeedDevice &dev, const QByteArray &pcm, int chunkBytes = 640)
{
    for (qsizetype off = 0; off < pcm.size(); off += chunkBytes) {
        dev.feed(pcm.mid(off, chunkBytes));
    }
}

SttSettings settingsFor(const QUrl &url, const QString &lock)
{
    SttSettings s;
    s.serverUrl = url;
    s.languageLock = lock;
    s.hallucinations = {QStringLiteral("Thank you."), QStringLiteral("you")};
    s.biasTerms = {QStringLiteral("phospholipid"), QStringLiteral("bilayer")};
    return s;
}

QByteArray json(const QString &text, const QString &language = QString())
{
    QString body = QStringLiteral("{\"text\":\"%1\"").arg(text);
    if (!language.isEmpty()) {
        body += QStringLiteral(",\"language\":\"%1\"").arg(language);
    }
    body += QLatin1Char('}');
    return body.toUtf8();
}

} // namespace

class TestSttEngine : public QObject
{
    Q_OBJECT

private slots:
    void incrementalSegmentsAndFinalText();
    void stopFlushesOpenSegment();
    void autoModeRedoesUrduAsHindiAndSticks();
    void mutedWhileEagleSpeaks();
    void hallucinationIsDropped();
    void serverDownReportsError();
    void convertsMicrophoneFormat();
    void cancelDropsEverything();
};

void TestSttEngine::incrementalSegmentsAndFinalText()
{
    MockWhisper mock;
    QVERIFY(mock.listen());
    mock.responder = [](int i, const QByteArray &) {
        return MockWhisper::Reply{200, json(QStringLiteral("seg%1").arg(i)), 50};
    };
    SttEngine engine;
    engine.configure(settingsFor(mock.url(), QStringLiteral("en")));
    QSignalSpy final(&engine, &SttEngine::finalText);
    QSignalSpy updates(&engine, &SttEngine::transcriptUpdated);
    FeedDevice dev;
    QVERIFY(engine.startListening(&dev, 16000, 1, PcmTo16kMono::SampleType::Int16));
    QCOMPARE(engine.state(), SttEngine::State::Listening);

    feedInChunks(dev, signal16k({{500, 0}, {1000, 0.1}, {1200, 0}, {1000, 0.1}, {1200, 0}}));
    // Both segments are transcribed while "listening", before Stop is pressed.
    QTRY_COMPARE_WITH_TIMEOUT(engine.transcript(), QStringLiteral("seg0 seg1"), 5000);
    QCOMPARE(mock.requests.size(), 2);
    QCOMPARE(fieldValue(mock.requests[0], "language"), QByteArray("en"));
    QCOMPARE(fieldValue(mock.requests[0], "prompt"), QByteArray("phospholipid, bilayer."));
    // Second prompt carries the tail of the first segment's text.
    QCOMPARE(fieldValue(mock.requests[1], "prompt"), QByteArray("phospholipid, bilayer. seg0"));

    engine.stopListening();
    QCOMPARE(final.count(), 1);  // nothing pending: final text at once
    QCOMPARE(final.first().at(0).toString(), QStringLiteral("seg0 seg1"));
    QCOMPARE(final.first().at(1).toString(), QStringLiteral("en"));
    QVERIFY(final.first().at(2).toLongLong() < 200);
    QCOMPARE(engine.state(), SttEngine::State::Idle);
    QVERIFY(updates.count() >= 3);
}

void TestSttEngine::stopFlushesOpenSegment()
{
    MockWhisper mock;
    QVERIFY(mock.listen());
    mock.responder = [](int, const QByteArray &) {
        return MockWhisper::Reply{200, json(QStringLiteral("still talking")), 300};
    };
    SttEngine engine;
    engine.configure(settingsFor(mock.url(), QStringLiteral("en")));
    QSignalSpy final(&engine, &SttEngine::finalText);
    FeedDevice dev;
    QVERIFY(engine.startListening(&dev, 16000, 1, PcmTo16kMono::SampleType::Int16));
    feedInChunks(dev, signal16k({{500, 0}, {1500, 0.1}}));  // Stop while speaking
    QCOMPARE(mock.requests.size(), 0);
    engine.stopListening();
    QCOMPARE(engine.state(), SttEngine::State::Finishing);
    QVERIFY(final.wait(3000));
    QCOMPARE(final.first().at(0).toString(), QStringLiteral("still talking"));
    const qint64 stopToFinal = final.first().at(2).toLongLong();
    QVERIFY2(stopToFinal >= 250 && stopToFinal < 2000, qPrintable(QString::number(stopToFinal)));
}

void TestSttEngine::autoModeRedoesUrduAsHindiAndSticks()
{
    MockWhisper mock;
    QVERIFY(mock.listen());
    mock.responder = [](int i, const QByteArray &req) {
        const QByteArray lang = fieldValue(req, "language");
        if (i == 0) {
            return MockWhisper::Reply{200, json(QStringLiteral("فوٹوسنتھیسس"), QStringLiteral("urdu"))};
        }
        if (i == 1) {
            return MockWhisper::Reply{200, json(lang == "hi" ? QStringLiteral("प्रकाश संश्लेषण") : QStringLiteral("WRONG"))};
        }
        return MockWhisper::Reply{200, json(lang == "hi" ? QStringLiteral("क्या है") : QStringLiteral("WRONG"))};
    };
    SttEngine engine;
    engine.configure(settingsFor(mock.url(), QStringLiteral("auto")));
    QSignalSpy final(&engine, &SttEngine::finalText);
    FeedDevice dev;
    QVERIFY(engine.startListening(&dev, 16000, 1, PcmTo16kMono::SampleType::Int16));
    feedInChunks(dev, signal16k({{500, 0}, {1000, 0.1}, {1200, 0}, {1000, 0.1}, {1200, 0}}));
    engine.stopListening();
    QVERIFY(final.count() == 1 || final.wait(5000));
    QCOMPARE(final.first().at(0).toString(), QStringLiteral("प्रकाश संश्लेषण क्या है"));
    QCOMPARE(final.first().at(1).toString(), QStringLiteral("hi"));
    QCOMPARE(mock.requests.size(), 3);
    QCOMPARE(fieldValue(mock.requests[0], "language"), QByteArray("auto"));
    QCOMPARE(fieldValue(mock.requests[0], "response_format"), QByteArray("verbose_json"));
    QCOMPARE(fieldValue(mock.requests[1], "language"), QByteArray("hi"));  // redo
    QCOMPARE(fieldValue(mock.requests[2], "language"), QByteArray("hi"));  // sticky
}

void TestSttEngine::mutedWhileEagleSpeaks()
{
    MockWhisper mock;
    QVERIFY(mock.listen());
    SttEngine engine;
    engine.configure(settingsFor(mock.url(), QStringLiteral("en")));
    FeedDevice dev;
    QVERIFY(engine.startListening(&dev, 16000, 1, PcmTo16kMono::SampleType::Int16));
    feedInChunks(dev, signal16k({{500, 0}}));
    engine.setOutputActive(true);  // Eagle's voice is playing
    feedInChunks(dev, signal16k({{1000, 0.1}, {1200, 0}}));
    QTest::qWait(200);
    QCOMPARE(mock.requests.size(), 0);
    engine.setOutputActive(false);
    feedInChunks(dev, signal16k({{1000, 0.1}, {1200, 0}}));
    QTRY_COMPARE_WITH_TIMEOUT(mock.requests.size(), 1, 3000);
    engine.cancel();
}

void TestSttEngine::hallucinationIsDropped()
{
    MockWhisper mock;
    QVERIFY(mock.listen());
    mock.responder = [](int, const QByteArray &) { return MockWhisper::Reply{200, json(QStringLiteral(" Thank you."))}; };
    SttEngine engine;
    engine.configure(settingsFor(mock.url(), QStringLiteral("en")));
    QSignalSpy final(&engine, &SttEngine::finalText);
    QSignalSpy errors(&engine, &SttEngine::errorOccurred);
    FeedDevice dev;
    QVERIFY(engine.startListening(&dev, 16000, 1, PcmTo16kMono::SampleType::Int16));
    feedInChunks(dev, signal16k({{500, 0}, {1000, 0.1}, {1200, 0}}));
    QTRY_COMPARE_WITH_TIMEOUT(mock.requests.size(), 1, 3000);
    QTest::qWait(100);
    engine.stopListening();
    QVERIFY(final.count() == 1 || final.wait(3000));
    QCOMPARE(final.first().at(0).toString(), QString());
    QCOMPARE(errors.count(), 0);
}

void TestSttEngine::serverDownReportsError()
{
    quint16 port = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        port = probe.serverPort();
    }
    SttSettings s = settingsFor(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port)), QStringLiteral("en"));
    s.startupRetryMs = 300;
    SttEngine engine;
    engine.configure(s);
    QSignalSpy final(&engine, &SttEngine::finalText);
    QSignalSpy errors(&engine, &SttEngine::errorOccurred);
    FeedDevice dev;
    QVERIFY(engine.startListening(&dev, 16000, 1, PcmTo16kMono::SampleType::Int16));
    feedInChunks(dev, signal16k({{500, 0}, {1000, 0.1}, {1200, 0}}));
    engine.stopListening();
    QVERIFY(final.count() == 1 || final.wait(5000));
    QCOMPARE(final.first().at(0).toString(), QString());
    QCOMPARE(errors.count(), 1);
    QVERIFY(errors.first().at(0).toString().contains(QStringLiteral("whisper-server")));
}

void TestSttEngine::convertsMicrophoneFormat()
{
    MockWhisper mock;
    QVERIFY(mock.listen());
    mock.responder = [](int, const QByteArray &) { return MockWhisper::Reply{200, json(QStringLiteral("stereo ok"))}; };
    SttEngine engine;
    engine.configure(settingsFor(mock.url(), QStringLiteral("en")));
    QSignalSpy final(&engine, &SttEngine::finalText);
    FeedDevice dev;
    QVERIFY(engine.startListening(&dev, 48000, 2, PcmTo16kMono::SampleType::Float));
    feedInChunks(dev, to48kStereoFloat(signal16k({{500, 0}, {1000, 0.1}, {1200, 0}})), 3840);
    QTRY_COMPARE_WITH_TIMEOUT(mock.requests.size(), 1, 3000);
    // The WAV sent to whisper is 16 kHz mono PCM16 whatever the microphone gives.
    const QByteArray wav = fieldValue(mock.requests[0], "file");
    QVERIFY(wav.startsWith("RIFF"));
    quint32 rate = 0;
    quint16 channels = 0;
    std::memcpy(&channels, wav.constData() + 22, 2);
    std::memcpy(&rate, wav.constData() + 24, 4);
    QCOMPARE(channels, quint16(1));
    QCOMPARE(rate, quint32(16000));
    engine.stopListening();
    QVERIFY(final.count() == 1 || final.wait(3000));
    QCOMPARE(final.first().at(0).toString(), QStringLiteral("stereo ok"));
}

void TestSttEngine::cancelDropsEverything()
{
    MockWhisper mock;
    QVERIFY(mock.listen());
    mock.responder = [](int, const QByteArray &) { return MockWhisper::Reply{200, json(QStringLiteral("late")), 400}; };
    SttEngine engine;
    engine.configure(settingsFor(mock.url(), QStringLiteral("en")));
    QSignalSpy final(&engine, &SttEngine::finalText);
    FeedDevice dev;
    QVERIFY(engine.startListening(&dev, 16000, 1, PcmTo16kMono::SampleType::Int16));
    feedInChunks(dev, signal16k({{500, 0}, {1000, 0.1}, {1200, 0}}));
    QTRY_COMPARE_WITH_TIMEOUT(mock.requests.size(), 1, 3000);
    engine.cancel();
    QCOMPARE(engine.state(), SttEngine::State::Idle);
    QTest::qWait(700);
    QCOMPARE(final.count(), 0);
    QVERIFY(engine.transcript().isEmpty());
}

QTEST_GUILESS_MAIN(TestSttEngine)
#include "tst_sttengine.moc"
