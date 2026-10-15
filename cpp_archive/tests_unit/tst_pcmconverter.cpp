#include "PcmConverter.h"

#include <QTest>
#include <QtMath>
#include <cmath>

class TestPcmConverter : public QObject
{
    Q_OBJECT

private slots:
    void oneSecondMonoInt16To48000StereoFloat();
    void durationUSecs();
    void sine440KeepsFrequencyAndRange();
    void fullScaleSpeechDoesNotReachUnity();
};

namespace {

QByteArray makeSineInt16(int sampleRate, int samples, double hz, double amplitude)
{
    QByteArray pcm(samples * 2, Qt::Uninitialized);
    auto *dst = reinterpret_cast<qint16 *>(pcm.data());
    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < samples; ++i) {
        dst[i] = static_cast<qint16>(
            qRound(std::sin(2.0 * kPi * hz * i / sampleRate) * amplitude * 32767.0));
    }
    return pcm;
}

double goertzelPower(const float *interleaved, int frames, int channels, double freq, int rate)
{
    const double w = 2.0 * 3.14159265358979323846 * freq / rate;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0;
    double s1 = 0;
    double s2 = 0;
    for (int i = 0; i < frames; ++i) {
        s0 = double(interleaved[i * channels]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

} // namespace

void TestPcmConverter::oneSecondMonoInt16To48000StereoFloat()
{
    const QAudioFormat src = PcmConverter::sourceFormat22050MonoInt16();
    QByteArray pcm(22050 * 2, 0);
    auto *samples = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < 22050; ++i) {
        samples[i] = 1000;
    }

    QAudioFormat dst;
    dst.setSampleRate(48000);
    dst.setChannelCount(2);
    dst.setSampleFormat(QAudioFormat::Float);

    PcmConverter::ConversionStats stats;
    const QByteArray out = PcmConverter::convertMonoInt16(pcm, src, dst, 0.7f, &stats);
    QCOMPARE(out.size(), 48000 * 2 * 4);
    QCOMPARE(out.size(), 384000);
    QCOMPARE(stats.clippedSamples, 0);

    const auto *f = reinterpret_cast<const float *>(out.constData());
    const float expected = (1000.0f / 32768.0f) * 0.7f;
    QVERIFY(qAbs(f[out.size() / 8] - expected) < 0.002f);
    QCOMPARE(f[1000], f[1001]);
}

void TestPcmConverter::durationUSecs()
{
    QCOMPARE(PcmConverter::expectedUSecs(22050, 22050), 1000000);
    QCOMPARE(PcmConverter::expectedUSecs(44100, 22050), 2000000);
}

void TestPcmConverter::sine440KeepsFrequencyAndRange()
{
    const QAudioFormat src = PcmConverter::sourceFormat22050MonoInt16();
    const QByteArray pcm = makeSineInt16(22050, 22050, 440.0, 0.5);

    QAudioFormat dst;
    dst.setSampleRate(48000);
    dst.setChannelCount(2);
    dst.setSampleFormat(QAudioFormat::Float);

    PcmConverter::ConversionStats stats;
    const QByteArray out = PcmConverter::convertMonoInt16(pcm, src, dst, 0.7f, &stats);
    QCOMPARE(out.size(), 384000);
    QCOMPARE(stats.clippedSamples, 0);
    QVERIFY(stats.peak <= 1.0);

    const auto *f = reinterpret_cast<const float *>(out.constData());
    const int frames = 48000;
    float peak = 0;
    for (int i = 0; i < frames * 2; ++i) {
        QVERIFY(f[i] >= -1.0f);
        QVERIFY(f[i] <= 1.0f);
        peak = qMax(peak, qAbs(f[i]));
        if (i % 2 == 1) {
            QCOMPARE(f[i], f[i - 1]);
        }
    }
    QVERIFY(peak > 0.2f);
    QVERIFY(peak <= 1.0f);

    const double p440 = goertzelPower(f, frames, 2, 440.0, 48000);
    const double p400 = goertzelPower(f, frames, 2, 400.0, 48000);
    const double p500 = goertzelPower(f, frames, 2, 500.0, 48000);
    QVERIFY(p440 > p400);
    QVERIFY(p440 > p500);
    QVERIFY(p440 > p400 * 4);
}

void TestPcmConverter::fullScaleSpeechDoesNotReachUnity()
{
    const QAudioFormat src = PcmConverter::sourceFormat22050MonoInt16();
    QByteArray pcm(22050 * 2, Qt::Uninitialized);
    auto *samples = reinterpret_cast<qint16 *>(pcm.data());
    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < 22050; ++i) {
        const double env = 0.4 + 0.6 * std::fabs(std::sin(2.0 * kPi * i / 180.0));
        samples[i] = static_cast<qint16>(qRound(env * 32767.0 * ((i / 40) % 2 ? 1.0 : -1.0)));
    }

    QAudioFormat dst;
    dst.setSampleRate(48000);
    dst.setChannelCount(2);
    dst.setSampleFormat(QAudioFormat::Float);

    PcmConverter::ConversionStats stats;
    const QByteArray out = PcmConverter::convertMonoInt16(pcm, src, dst, 0.7f, &stats);
    QCOMPARE(out.size(), 384000);
    QCOMPARE(stats.clippedSamples, 0);
    QVERIFY(stats.peak <= 0.75);
    QVERIFY(stats.peak < 1.0);

    const auto *f = reinterpret_cast<const float *>(out.constData());
    for (int i = 0; i < 48000 * 2; ++i) {
        QVERIFY(f[i] > -1.0f);
        QVERIFY(f[i] < 1.0f);
    }
}

QTEST_MAIN(TestPcmConverter)
#include "tst_pcmconverter.moc"
