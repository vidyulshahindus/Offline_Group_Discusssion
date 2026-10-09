#include "Vad.h"

#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <cstring>

namespace {

constexpr int kRate = 16000;
constexpr double kPi = 3.14159265358979323846;

struct Span {
    int ms = 0;
    double toneAmplitude = 0.0;  // 0 = noise only; 0.1 ≈ -23 dBFS RMS
};

// Uniform noise around -55 dBFS RMS plus optional 300 Hz tone bursts.
QByteArray makeSignal(const QVector<Span> &spans, quint32 seed = 7)
{
    QRandomGenerator rng(seed);
    QByteArray out;
    qint64 n = 0;
    for (const Span &span : spans) {
        const qint64 count = qint64(span.ms) * kRate / 1000;
        for (qint64 i = 0; i < count; ++i, ++n) {
            double v = (rng.generateDouble() * 2.0 - 1.0) * 0.00308;
            if (span.toneAmplitude > 0.0) {
                v += span.toneAmplitude * std::sin(2.0 * kPi * 300.0 * double(n) / kRate);
            }
            const qint16 s = qint16(qBound(-32768.0, std::round(v * 32767.0), 32767.0));
            char b[2];
            std::memcpy(b, &s, 2);
            out.append(b, 2);
        }
    }
    return out;
}

struct Segment {
    qint64 startMs;
    qint64 endMs;
    qsizetype bytes;
};

QVector<Segment> segmentsFrom(const QSignalSpy &spy)
{
    QVector<Segment> out;
    for (const QList<QVariant> &args : spy) {
        out.append({args.at(1).toLongLong(), args.at(2).toLongLong(), args.at(0).toByteArray().size()});
    }
    return out;
}

} // namespace

class TestVad : public QObject
{
    Q_OBJECT

private slots:
    void noiseOnlyGivesNoSegment();
    void singleUtteranceWithPreRoll();
    void twoUtterancesSeparatedBySilence();
    void shortPauseStaysOneSegment();
    void shortBurstIsDiscarded();
    void longSpeechIsCutAtQuietestFrame();
    void flushEmitsOpenSegment();
    void flushDropsTinySegment();
    void chunkSizeDoesNotMatter();
    void seededFloorSkipsCalibration();
    void abandonDropsOpenSegment();
};

void TestVad::noiseOnlyGivesNoSegment()
{
    Vad vad;
    QSignalSpy segments(&vad, &Vad::segmentReady);
    vad.pushPcm(makeSignal({{5000, 0.0}}));
    QCOMPARE(segments.count(), 0);
    QVERIFY(vad.calibrated());
    QVERIFY2(std::abs(vad.noiseFloorDb() - (-55.0)) < 4.0, qPrintable(QString::number(vad.noiseFloorDb())));
}

void TestVad::singleUtteranceWithPreRoll()
{
    Vad vad;
    QSignalSpy segments(&vad, &Vad::segmentReady);
    QSignalSpy started(&vad, &Vad::speechStarted);
    vad.pushPcm(makeSignal({{500, 0.0}, {1500, 0.1}, {1500, 0.0}}));
    QCOMPARE(segments.count(), 1);
    QCOMPARE(started.count(), 1);
    const Segment s = segmentsFrom(segments).first();
    // Starts about pre_roll_ms (200) before the tone, ends about tail_keep_ms (200) after it.
    QVERIFY2(s.startMs >= 500 - 200 - 60 && s.startMs <= 500 - 200 + 30, qPrintable(QString::number(s.startMs)));
    QVERIFY2(s.endMs >= 2000 + 200 - 30 && s.endMs <= 2000 + 200 + 60, qPrintable(QString::number(s.endMs)));
    QCOMPARE(s.bytes, (s.endMs - s.startMs) * kRate / 1000 * 2);
}

void TestVad::twoUtterancesSeparatedBySilence()
{
    Vad vad;
    QSignalSpy segments(&vad, &Vad::segmentReady);
    vad.pushPcm(makeSignal({{500, 0.0}, {1000, 0.1}, {1200, 0.0}, {1000, 0.1}, {1200, 0.0}}));
    QCOMPARE(segments.count(), 2);
    const QVector<Segment> s = segmentsFrom(segments);
    QVERIFY(s[0].endMs <= s[1].startMs);
    QVERIFY(s[1].startMs >= 2700 - 260);
}

void TestVad::shortPauseStaysOneSegment()
{
    Vad vad;
    QSignalSpy segments(&vad, &Vad::segmentReady);
    vad.pushPcm(makeSignal({{500, 0.0}, {1000, 0.1}, {300, 0.0}, {1000, 0.1}, {1200, 0.0}}));
    QCOMPARE(segments.count(), 1);
}

void TestVad::shortBurstIsDiscarded()
{
    Vad vad;
    QSignalSpy segments(&vad, &Vad::segmentReady);
    QSignalSpy discarded(&vad, &Vad::segmentDiscarded);
    vad.pushPcm(makeSignal({{500, 0.0}, {300, 0.1}, {1200, 0.0}}));
    QCOMPARE(segments.count(), 0);
    QCOMPARE(discarded.count(), 1);
}

void TestVad::longSpeechIsCutAtQuietestFrame()
{
    Vad vad;
    QSignalSpy segments(&vad, &Vad::segmentReady);
    // 20 s of speech with a short dip (still above the end threshold) at 11.40–11.46 s.
    vad.pushPcm(makeSignal({{500, 0.0}, {10900, 0.1}, {60, 0.01}, {9040, 0.1}, {1500, 0.0}}));
    QCOMPARE(segments.count(), 2);
    const QVector<Segment> s = segmentsFrom(segments);
    QVERIFY2(s[0].endMs - s[0].startMs <= 12000 + 30, qPrintable(QString::number(s[0].endMs - s[0].startMs)));
    QVERIFY2(s[0].endMs >= 11400 && s[0].endMs <= 11490, qPrintable(QString::number(s[0].endMs)));
    QCOMPARE(s[1].startMs, s[0].endMs);  // no audio lost or duplicated at the cut
}

void TestVad::flushEmitsOpenSegment()
{
    Vad vad;
    QSignalSpy segments(&vad, &Vad::segmentReady);
    vad.pushPcm(makeSignal({{500, 0.0}, {800, 0.1}}));
    QVERIFY(vad.inSpeech());
    QVERIFY(vad.flush(300));
    QCOMPARE(segments.count(), 1);
    QVERIFY(!vad.inSpeech());
}

void TestVad::flushDropsTinySegment()
{
    Vad vad;
    QSignalSpy segments(&vad, &Vad::segmentReady);
    vad.pushPcm(makeSignal({{500, 0.0}, {200, 0.1}}));
    QVERIFY(vad.inSpeech());
    QVERIFY(!vad.flush(300));
    QCOMPARE(segments.count(), 0);
}

void TestVad::chunkSizeDoesNotMatter()
{
    const QByteArray signal =
        makeSignal({{500, 0.0}, {1000, 0.1}, {1200, 0.0}, {2500, 0.1}, {300, 0.0}, {700, 0.1}, {1500, 0.0}});

    Vad whole;
    QSignalSpy wholeSpy(&whole, &Vad::segmentReady);
    whole.pushPcm(signal);

    Vad chunked;
    QSignalSpy chunkSpy(&chunked, &Vad::segmentReady);
    QRandomGenerator rng(42);
    qsizetype offset = 0;
    while (offset < signal.size()) {
        const qsizetype len = qMin<qsizetype>(signal.size() - offset, 1 + rng.bounded(2000));
        chunked.pushPcm(signal.mid(offset, len));  // includes odd byte counts
        offset += len;
    }

    QCOMPARE(chunkSpy.count(), wholeSpy.count());
    QCOMPARE(wholeSpy.count(), 2);
    for (int i = 0; i < wholeSpy.count(); ++i) {
        QCOMPARE(chunkSpy.at(i).at(0).toByteArray(), wholeSpy.at(i).at(0).toByteArray());
        QCOMPARE(chunkSpy.at(i).at(1).toLongLong(), wholeSpy.at(i).at(1).toLongLong());
    }
}

void TestVad::seededFloorSkipsCalibration()
{
    VadParams params;
    params.seedFloorDb = -55.0;
    Vad vad;
    vad.setParams(params);
    QVERIFY(vad.calibrated());
    QSignalSpy segments(&vad, &Vad::segmentReady);
    // Speech starts at once: with calibration it would be missed.
    vad.pushPcm(makeSignal({{1000, 0.1}, {1200, 0.0}}));
    QCOMPARE(segments.count(), 1);
    QCOMPARE(segmentsFrom(segments).first().startMs, qint64(0));
}

void TestVad::abandonDropsOpenSegment()
{
    Vad vad;
    QSignalSpy segments(&vad, &Vad::segmentReady);
    vad.pushPcm(makeSignal({{500, 0.0}, {800, 0.1}}));
    QVERIFY(vad.inSpeech());
    vad.abandonSegment();
    QVERIFY(!vad.inSpeech());
    vad.pushPcm(makeSignal({{1200, 0.0}}, 9));
    QCOMPARE(segments.count(), 0);
}

QTEST_GUILESS_MAIN(TestVad)
#include "tst_vad.moc"
