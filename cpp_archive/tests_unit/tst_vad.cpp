#include "Vad.h"

#include <QTest>
#include <QtMath>

class TestVad : public QObject
{
    Q_OBJECT

private slots:
    void detectsToneBurst();
    void preRollIncluded();
};

namespace {

QByteArray toneMs(int ms, double hz = 440.0, double amp = 0.35)
{
    const int samples = 16000 * ms / 1000;
    QByteArray pcm(samples * 2, Qt::Uninitialized);
    auto *dst = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < samples; ++i) {
        dst[i] = qint16(qRound(std::sin(2.0 * M_PI * hz * i / 16000.0) * amp * 32767.0));
    }
    return pcm;
}

QByteArray silenceMs(int ms)
{
    return QByteArray(16000 * ms / 1000 * 2, '\0');
}

} // namespace

void TestVad::detectsToneBurst()
{
    Vad::Config cfg;
    cfg.minSegmentMs = 400;
    cfg.endHangoverMs = 300;
    Vad vad(cfg);

    QVector<Vad::Segment> segs;
    segs += vad.pushPcm16k(silenceMs(500), 0);
    segs += vad.pushPcm16k(toneMs(900), 500);
    segs += vad.pushPcm16k(silenceMs(900), 1400);
    segs += vad.flush(2300);

    int nonEmpty = 0;
    for (const Vad::Segment &s : segs) {
        if (!s.pcm16k.isEmpty()) {
            ++nonEmpty;
        }
    }
    QVERIFY(nonEmpty >= 1);
}

void TestVad::preRollIncluded()
{
    Vad::Config cfg;
    cfg.minSegmentMs = 300;
    cfg.preRollMs = 200;
    cfg.endHangoverMs = 300;
    Vad vad(cfg);
    QVector<Vad::Segment> segs;
    segs += vad.pushPcm16k(silenceMs(300), 0);
    segs += vad.pushPcm16k(toneMs(700), 300);
    segs += vad.pushPcm16k(silenceMs(800), 1000);
    segs += vad.flush(1800);
    QVERIFY(!segs.isEmpty());
    const Vad::Segment seg = segs.first();
    QVERIFY(seg.pcm16k.size() >= 300 * 16000 / 1000 * 2);
}

QTEST_MAIN(TestVad)
#include "tst_vad.moc"
