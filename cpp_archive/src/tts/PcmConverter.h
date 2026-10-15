#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QString>
#include <QVector>

namespace PcmConverter {

struct ConversionStats {
    int clippedSamples = 0;
    int limiterEngaged = 0;
    qreal peak = 0;
    qreal peakPre = 0;
    qreal rms = 0;
    qreal peakDb = -120;
    qreal rmsDb = -120;
    qint64 outFrames = 0;
    qint64 outBytes = 0;
};

QString sampleFormatName(QAudioFormat::SampleFormat format);
QAudioFormat sourceFormat22050MonoInt16();
QAudioFormat mixFormat48000StereoFloat();
qint64 frameCountForMonoInt16(const QByteArray &pcm);
qint64 expectedUSecs(qint64 sourceFrames, int sourceRate);

QVector<float> int16MonoToFloat(const QByteArray &pcm);
QVector<float> resampleMonoFloatLanczos(const QVector<float> &input, int srcRate, int dstRate);
float softLimit(float sample, float knee = 0.9f, float ceiling = 0.75f);
int applySoftLimiter(QVector<float> &samples, float knee = 0.9f, float ceiling = 0.75f);

QByteArray convertMonoInt16(const QByteArray &srcMonoInt16, const QAudioFormat &srcFormat,
                            const QAudioFormat &dstFormat, float gain = 0.7f,
                            ConversionStats *stats = nullptr);

bool writeWav(const QString &path, const QByteArray &pcm, const QAudioFormat &format);

} // namespace PcmConverter
