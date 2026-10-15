#include "AudioPlayer.h"
#include "PcmConverter.h"

#include <QMediaDevices>
#include <QSignalSpy>
#include <QTest>
#include <QtMath>
#include <cmath>

class TestAudioPlayer : public QObject
{
    Q_OBJECT

private slots:
    void speechPathPeakAtVolume07();
    void resumesAfterIdleGapBetweenSentences();
};

namespace {

QByteArray sineSentenceMs(int milliseconds)
{
    const int samples = 22050 * milliseconds / 1000;
    QByteArray pcm(samples * 2, Qt::Uninitialized);
    auto *dst = reinterpret_cast<qint16 *>(pcm.data());
    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < samples; ++i) {
        dst[i] = static_cast<qint16>(
            qRound(std::sin(2.0 * kPi * 440.0 * i / 22050.0) * 0.4 * 32767.0));
    }
    return pcm;
}

} // namespace

void TestAudioPlayer::speechPathPeakAtVolume07()
{
    AudioPlayer player;
    player.setInputGain(0.7f);
    QCOMPARE(player.inputGain(), 0.7f);

    QByteArray pcm(22050 * 2, Qt::Uninitialized);
    auto *samples = reinterpret_cast<qint16 *>(pcm.data());
    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < 22050; ++i) {
        const double env = 0.5 + 0.5 * std::fabs(std::sin(2.0 * kPi * i / 90.0));
        samples[i] = static_cast<qint16>(qRound(env * 32767.0 * ((i / 17) % 2 ? 1.0 : -1.0)));
    }

    PcmConverter::ConversionStats stats;
    const QByteArray out = player.processSpeechPcm(pcm, &stats);
    QVERIFY(out.size() > 0);
    QCOMPARE(out.size() % 8, 0);
    QVERIFY(stats.peak <= 0.75);
    QCOMPARE(stats.clippedSamples, 0);

    const auto *f = reinterpret_cast<const float *>(out.constData());
    const int n = out.size() / int(sizeof(float));
    float peak = 0;
    for (int i = 0; i < n; ++i) {
        QVERIFY(f[i] < 0.75f);
        QVERIFY(f[i] > -0.75f);
        peak = qMax(peak, qAbs(f[i]));
    }
    QVERIFY(peak > 0.2f);
    QVERIFY(peak <= 0.75f);
}

void TestAudioPlayer::resumesAfterIdleGapBetweenSentences()
{
    if (QMediaDevices::defaultAudioOutput().isNull()) {
        QSKIP("No audio output device");
    }

    AudioPlayer player;
    player.setInputGain(0.7f);
    QSignalSpy finished(&player, &AudioPlayer::speechFinished);
    QSignalSpy errors(&player, &AudioPlayer::playbackError);

    const int sentenceMs = 400;
    const QByteArray a = sineSentenceMs(sentenceMs);
    const QByteArray b = sineSentenceMs(sentenceMs);

    player.enqueuePcm(1, 0, a, true);
    QTest::qWait(800);
    QVERIFY(errors.isEmpty());
    player.enqueuePcm(1, 1, b, true);
    player.markInputComplete();

    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 8000);
    QVERIFY(errors.isEmpty());
    QVERIFY(!finished.first().at(1).toBool());

    const qint64 expectedUs = qint64(sentenceMs) * 2 * 1000;
    const qint64 got = player.processedUSecs();
    QVERIFY(got > 0);
    const qreal ratio = qreal(got) / qreal(expectedUs);
    QVERIFY2(ratio >= 0.95 && ratio <= 1.05,
             qPrintable(QStringLiteral("processed %1 expected %2 ratio %3")
                            .arg(got)
                            .arg(expectedUs)
                            .arg(ratio)));
}

QTEST_MAIN(TestAudioPlayer)
#include "tst_audioplayer.moc"
