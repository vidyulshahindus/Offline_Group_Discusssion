#include "PcmTo16kMono.h"

#include <QRandomGenerator>
#include <QTest>

#include <cmath>
#include <cstring>

namespace {

constexpr double kPi = 3.14159265358979323846;

QVector<qint16> samplesOf(const QByteArray &pcm)
{
    QVector<qint16> out(pcm.size() / 2);
    std::memcpy(out.data(), pcm.constData(), size_t(out.size()) * 2);
    return out;
}

template <typename T>
void appendValue(QByteArray &out, T v)
{
    char b[sizeof(T)];
    std::memcpy(b, &v, sizeof(T));
    out.append(b, sizeof(T));
}

// Interleaved test tone in the given format.
QByteArray tone(int rate, int channels, PcmTo16kMono::SampleType type, double seconds, double freq,
                double amplitude)
{
    QByteArray out;
    const int frames = int(rate * seconds);
    for (int i = 0; i < frames; ++i) {
        const double v = amplitude * std::sin(2.0 * kPi * freq * i / rate);
        for (int c = 0; c < channels; ++c) {
            switch (type) {
            case PcmTo16kMono::SampleType::Int16:
                appendValue<qint16>(out, qint16(std::lround(v * 32767.0)));
                break;
            case PcmTo16kMono::SampleType::Int32:
                appendValue<qint32>(out, qint32(std::llround(v * 2147483647.0)));
                break;
            case PcmTo16kMono::SampleType::Float:
                appendValue<float>(out, float(v));
                break;
            case PcmTo16kMono::SampleType::UInt8:
                out.append(char(quint8(std::lround(128.0 + v * 127.0))));
                break;
            }
        }
    }
    return out;
}

double rms(const QVector<qint16> &s, int skip = 0)
{
    double sum = 0.0;
    int n = 0;
    for (int i = skip; i < s.size(); ++i, ++n) {
        const double v = s[i] / 32768.0;
        sum += v * v;
    }
    return n ? std::sqrt(sum / n) : 0.0;
}

} // namespace

class TestPcmTo16kMono : public QObject
{
    Q_OBJECT

private slots:
    void passThrough16kMono();
    void downmixesAndDecimates48kStereoFloat();
    void resamples44100();
    void upsamples8k();
    void int32AndUInt8Formats();
    void oddChunksGiveSameOutput();
};

void TestPcmTo16kMono::passThrough16kMono()
{
    PcmTo16kMono conv;
    conv.reset(16000, 1, PcmTo16kMono::SampleType::Int16);
    const QByteArray in = tone(16000, 1, PcmTo16kMono::SampleType::Int16, 0.5, 440.0, 0.5);
    QCOMPARE(conv.convert(in), in);
}

void TestPcmTo16kMono::downmixesAndDecimates48kStereoFloat()
{
    PcmTo16kMono conv;
    conv.reset(48000, 2, PcmTo16kMono::SampleType::Float);
    const QByteArray out =
        conv.convert(tone(48000, 2, PcmTo16kMono::SampleType::Float, 1.0, 440.0, 0.5));
    const QVector<qint16> s = samplesOf(out);
    QVERIFY(std::abs(s.size() - 16000) <= 1);
    // A 440 Hz sine of amplitude 0.5 has RMS ≈ 0.354; a 3-sample box filter loses < 2%.
    QVERIFY2(std::abs(rms(s) - 0.3536) < 0.01, qPrintable(QString::number(rms(s))));
}

void TestPcmTo16kMono::resamples44100()
{
    PcmTo16kMono conv;
    conv.reset(44100, 1, PcmTo16kMono::SampleType::Int16);
    const QVector<qint16> s =
        samplesOf(conv.convert(tone(44100, 1, PcmTo16kMono::SampleType::Int16, 2.0, 300.0, 0.5)));
    QVERIFY(std::abs(s.size() - 32000) <= 2);
    QVERIFY(std::abs(rms(s) - 0.3536) < 0.01);
}

void TestPcmTo16kMono::upsamples8k()
{
    PcmTo16kMono conv;
    conv.reset(8000, 1, PcmTo16kMono::SampleType::Int16);
    const QVector<qint16> s =
        samplesOf(conv.convert(tone(8000, 1, PcmTo16kMono::SampleType::Int16, 1.0, 300.0, 0.5)));
    QVERIFY(std::abs(s.size() - 16000) <= 2);
    QVERIFY(std::abs(rms(s) - 0.3536) < 0.01);
}

void TestPcmTo16kMono::int32AndUInt8Formats()
{
    for (const auto type : {PcmTo16kMono::SampleType::Int32, PcmTo16kMono::SampleType::UInt8}) {
        PcmTo16kMono conv;
        conv.reset(16000, 1, type);
        const QVector<qint16> s = samplesOf(conv.convert(tone(16000, 1, type, 0.5, 440.0, 0.5)));
        QCOMPARE(s.size(), 8000);
        QVERIFY2(std::abs(rms(s) - 0.3536) < 0.01, qPrintable(QString::number(rms(s))));
    }
}

void TestPcmTo16kMono::oddChunksGiveSameOutput()
{
    const QByteArray in = tone(48000, 2, PcmTo16kMono::SampleType::Int16, 1.0, 523.0, 0.4);
    PcmTo16kMono whole;
    whole.reset(48000, 2, PcmTo16kMono::SampleType::Int16);
    const QByteArray expected = whole.convert(in);

    PcmTo16kMono chunked;
    chunked.reset(48000, 2, PcmTo16kMono::SampleType::Int16);
    QByteArray got;
    QRandomGenerator rng(3);
    qsizetype offset = 0;
    while (offset < in.size()) {
        const qsizetype len = qMin<qsizetype>(in.size() - offset, 1 + rng.bounded(997));
        got.append(chunked.convert(in.mid(offset, len)));
        offset += len;
    }
    QCOMPARE(got, expected);
}

QTEST_GUILESS_MAIN(TestPcmTo16kMono)
#include "tst_pcmto16kmono.moc"
