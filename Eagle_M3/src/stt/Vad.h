#pragma once

#include <QByteArray>
#include <QObject>
#include <QVector>

#include <limits>

// Energy-based voice activity detector (spec §10.2).
// Input: 16 kHz mono PCM16 in any chunk size. Output: one segmentReady() per utterance.
// Pure logic, no audio device: unit-tested with synthetic signals (tst_vad).
struct VadParams {
    int sampleRate = 16000;
    int frameMs = 30;
    double startThresholdDb = 9.0;  // speech starts above floor + this
    double endDropDb = 3.0;         // speech ends below floor + start - this
    int startFrames = 3;            // consecutive loud frames to start
    int endHangoverMs = 700;        // quiet time that ends a segment
    int minSegmentMs = 500;         // voiced span shorter than this is discarded
    int maxSegmentMs = 12000;       // force a cut at the quietest frame in the last second
    int preRollMs = 200;            // audio kept before the detected start
    int tailKeepMs = 200;           // quiet audio kept after the last loud frame
    int calibrationMs = 300;        // first frames only measure the noise floor
    int cutSearchMs = 1000;         // window for the forced cut
    double noiseAlpha = 0.05;       // EMA of frame dB while not in speech
    double floorMinDb = -70.0;
    double floorMaxDb = -25.0;
    // If set (not NaN), skips calibration and starts from this floor (e.g. the
    // floor measured in the previous Listen session).
    double seedFloorDb = std::numeric_limits<double>::quiet_NaN();
};

class Vad : public QObject
{
    Q_OBJECT

public:
    explicit Vad(QObject *parent = nullptr);

    void setParams(const VadParams &params);
    const VadParams &params() const { return m_params; }

    // Clears all state. Keeps nothing except the parameters.
    void reset();
    void pushPcm(const QByteArray &pcm16kMono);
    // Ends an in-progress segment (on Stop listening). Emits it if its kept
    // length is >= minMs. Returns true if a segment was emitted.
    bool flush(int minMs);
    // Drops an in-progress segment without emitting (e.g. Eagle started speaking).
    void abandonSegment();

    bool inSpeech() const { return m_inSpeech; }
    bool calibrated() const { return m_calibrated; }
    double noiseFloorDb() const { return m_floorDb; }
    double lastFrameDb() const { return m_lastDb; }
    qint64 positionMs() const { return m_frameIndex * m_params.frameMs; }

    static double frameDb(const qint16 *samples, int count);

signals:
    void speechStarted(qint64 startMs);
    void speechEnded(qint64 endMs);
    void segmentReady(const QByteArray &pcm16k, qint64 startMs, qint64 endMs);
    void segmentDiscarded(qint64 startMs, qint64 endMs);

private:
    struct Frame {
        QByteArray pcm;
        double db = -100.0;
        qint64 index = 0;
    };

    void processFrame(const Frame &frame);
    void pushPreRoll(const Frame &frame);
    void finishCalibration();
    void updateFloor(double db);
    void endSegment(int keepFrames, bool checkMinimum, int minMs);
    void forceCut();
    double startThreshold() const { return m_floorDb + m_params.startThresholdDb; }
    double endThreshold() const { return startThreshold() - m_params.endDropDb; }
    int framesFor(int ms) const;

    VadParams m_params;
    int m_frameSamples = 480;
    QByteArray m_remainder;
    qint64 m_frameIndex = 0;

    bool m_calibrated = false;
    QVector<double> m_calibrationDb;
    double m_floorDb = -50.0;
    double m_lastDb = -100.0;

    QVector<Frame> m_preRoll;
    QVector<Frame> m_candidates;

    bool m_inSpeech = false;
    QVector<Frame> m_segment;
    int m_quietRun = 0;         // trailing quiet frames in m_segment
    int m_firstLoud = -1;       // index into m_segment
    int m_lastLoud = -1;        // index into m_segment
};
