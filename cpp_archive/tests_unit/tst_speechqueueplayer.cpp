#include "SpeechQueuePlayer.h"

#include <QDir>
#include <QFile>
#include <QMediaDevices>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <QtMath>

#include <cmath>

class TestSpeechQueuePlayer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void sentenceOrder();
    void stopMidTurn();
    void finishDetection();
    void lateSecondSentenceStillPlays();

private:
    QString writeSineWav(const QString &name, int milliseconds) const;

    QTemporaryDir m_dir;
};

void TestSpeechQueuePlayer::initTestCase()
{
    if (QMediaDevices::defaultAudioOutput().isNull()) {
        QSKIP("No audio output device");
    }
    QVERIFY(m_dir.isValid());
}

QString TestSpeechQueuePlayer::writeSineWav(const QString &name, int milliseconds) const
{
    const int rate = 22050;
    const int n = rate * milliseconds / 1000;
    QByteArray pcm(n * 2, Qt::Uninitialized);
    auto *dst = reinterpret_cast<qint16 *>(pcm.data());
    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < n; ++i) {
        dst[i] = qint16(qRound(std::sin(2.0 * kPi * 440.0 * i / rate) * 0.35 * 32767.0));
    }
    QByteArray header;
    auto le16 = [&](quint16 v) {
        char b[2];
        qToLittleEndian(v, b);
        header.append(b, 2);
    };
    auto le32 = [&](quint32 v) {
        char b[4];
        qToLittleEndian(v, b);
        header.append(b, 4);
    };
    header.append("RIFF", 4);
    le32(quint32(36 + pcm.size()));
    header.append("WAVE", 4);
    header.append("fmt ", 4);
    le32(16);
    le16(1);
    le16(1);
    le32(quint32(rate));
    le32(quint32(rate * 2));
    le16(2);
    le16(16);
    header.append("data", 4);
    le32(quint32(pcm.size()));

    const QString path = m_dir.filePath(name);
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    out.write(header);
    out.write(pcm);
    return path;
}

void TestSpeechQueuePlayer::sentenceOrder()
{
    SpeechQueuePlayer player;
    player.setSentenceSilenceMs(50);
    player.setVolume(0.7f);
    QSignalSpy started(&player, &SpeechQueuePlayer::sentenceStarted);
    QSignalSpy finished(&player, &SpeechQueuePlayer::speechFinished);
    QSignalSpy errors(&player, &SpeechQueuePlayer::playbackError);

    const QString a = writeSineWav(QStringLiteral("s0.wav"), 250);
    const QString b = writeSineWav(QStringLiteral("s1.wav"), 250);
    QVERIFY(!a.isEmpty() && !b.isEmpty());

    player.enqueueWav(7, 0, a, true, false);
    player.enqueueWav(7, 1, b, true, false);
    player.markInputComplete();

    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 15000);
    QVERIFY(errors.isEmpty());
    QCOMPARE(started.count(), 2);
    QCOMPARE(started.at(0).at(0).toInt(), 0);
    QCOMPARE(started.at(1).at(0).toInt(), 1);
    QVERIFY(!finished.first().at(1).toBool());
    if (player.lastInterSentenceGapMs() >= 0) {
        QVERIFY2(player.lastInterSentenceGapMs() <= 800,
                 qPrintable(QStringLiteral("gapMs=%1").arg(player.lastInterSentenceGapMs())));
    }
}

void TestSpeechQueuePlayer::stopMidTurn()
{
    SpeechQueuePlayer player;
    player.setSentenceSilenceMs(50);
    QSignalSpy started(&player, &SpeechQueuePlayer::sentenceStarted);
    QSignalSpy finished(&player, &SpeechQueuePlayer::speechFinished);

    const QString a = writeSineWav(QStringLiteral("long0.wav"), 2500);
    const QString b = writeSineWav(QStringLiteral("long1.wav"), 2500);
    player.enqueueWav(3, 0, a, true, false);
    player.enqueueWav(3, 1, b, true, false);

    QTRY_VERIFY_WITH_TIMEOUT(started.count() >= 1, 8000);
    player.stop();
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 3000);
    QVERIFY(finished.last().at(1).toBool());
    QCOMPARE(started.count(), 1);
    QVERIFY(!player.isPlaybackBusy());
}

void TestSpeechQueuePlayer::finishDetection()
{
    SpeechQueuePlayer player;
    player.setSentenceSilenceMs(40);
    QSignalSpy finished(&player, &SpeechQueuePlayer::speechFinished);
    QSignalSpy started(&player, &SpeechQueuePlayer::sentenceStarted);

    player.enqueueWav(4, 0, writeSineWav(QStringLiteral("f0.wav"), 200), true, false);
    player.enqueueWav(4, 1, writeSineWav(QStringLiteral("f1.wav"), 200), true, false);
    QVERIFY(player.isPlaybackBusy());
    QVERIFY(finished.isEmpty());
    player.markInputComplete();

    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 15000);
    QCOMPARE(started.count(), 2);
    QCOMPARE(finished.first().at(0).toInt(), 4);
    QVERIFY(!finished.first().at(1).toBool());
    QVERIFY(!player.isPlaybackBusy());
}

void TestSpeechQueuePlayer::lateSecondSentenceStillPlays()
{
    SpeechQueuePlayer player;
    player.setSentenceSilenceMs(200);
    QSignalSpy started(&player, &SpeechQueuePlayer::sentenceStarted);
    QSignalSpy finished(&player, &SpeechQueuePlayer::speechFinished);
    QSignalSpy errors(&player, &SpeechQueuePlayer::playbackError);

    const int sentenceMs = 300;
    player.enqueueWav(9, 0, writeSineWav(QStringLiteral("g0.wav"), sentenceMs), true, false);
    QTRY_VERIFY_WITH_TIMEOUT(started.count() >= 1, 8000);
    QTest::qWait(sentenceMs + 800);
    QCOMPARE(started.count(), 1);
    player.enqueueWav(9, 1, writeSineWav(QStringLiteral("g1.wav"), sentenceMs), true, false);
    player.markInputComplete();

    QTRY_VERIFY_WITH_TIMEOUT(started.count() >= 2, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 15000);
    QVERIFY(errors.isEmpty());
    QCOMPARE(started.at(1).at(0).toInt(), 1);
    QVERIFY(!finished.first().at(1).toBool());
}

QTEST_MAIN(TestSpeechQueuePlayer)
#include "tst_speechqueueplayer.moc"
