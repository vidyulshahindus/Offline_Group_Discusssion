#include "PcmConverter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>
#include <QtMath>
#include <cmath>
#include <cstring>

namespace PcmConverter {

namespace {

constexpr int kLanczosA = 8;
constexpr qreal kPi = 3.14159265358979323846;

float sinc(float x)
{
    if (qAbs(x) < 1.0e-6f) {
        return 1.0f;
    }
    const float pix = float(kPi) * x;
    return std::sin(pix) / pix;
}

float lanczos(float x, int a)
{
    if (x <= -float(a) || x >= float(a)) {
        return 0.0f;
    }
    return sinc(x) * sinc(x / float(a));
}

void appendLe16(QByteArray &out, quint16 v)
{
    char buf[2];
    qToLittleEndian(v, buf);
    out.append(buf, 2);
}

void appendLe32(QByteArray &out, quint32 v)
{
    char buf[4];
    qToLittleEndian(v, buf);
    out.append(buf, 4);
}

QByteArray packInterleaved(const QVector<float> &interleaved, QAudioFormat::SampleFormat fmt)
{
    const int n = interleaved.size();
    if (fmt == QAudioFormat::Float) {
        QByteArray out(n * int(sizeof(float)), Qt::Uninitialized);
        memcpy(out.data(), interleaved.constData(), size_t(out.size()));
        return out;
    }
    if (fmt == QAudioFormat::Int16) {
        QByteArray out(n * int(sizeof(qint16)), Qt::Uninitialized);
        auto *dst = reinterpret_cast<qint16 *>(out.data());
        for (int i = 0; i < n; ++i) {
            const float s = qBound(-1.0f, interleaved[i], 1.0f);
            dst[i] = static_cast<qint16>(qRound(s * 32767.0f));
        }
        return out;
    }
    if (fmt == QAudioFormat::Int32) {
        QByteArray out(n * int(sizeof(qint32)), Qt::Uninitialized);
        auto *dst = reinterpret_cast<qint32 *>(out.data());
        for (int i = 0; i < n; ++i) {
            const float s = qBound(-1.0f, interleaved[i], 1.0f);
            dst[i] = static_cast<qint32>(qRound(double(s) * 2147483647.0));
        }
        return out;
    }
    if (fmt == QAudioFormat::UInt8) {
        QByteArray out(n, Qt::Uninitialized);
        auto *dst = reinterpret_cast<quint8 *>(out.data());
        for (int i = 0; i < n; ++i) {
            const float s = qBound(-1.0f, interleaved[i], 1.0f);
            dst[i] = static_cast<quint8>(qRound((s + 1.0f) * 127.5f));
        }
        return out;
    }
    QByteArray out(n * int(sizeof(float)), Qt::Uninitialized);
    memcpy(out.data(), interleaved.constData(), size_t(out.size()));
    return out;
}

} // namespace

QString sampleFormatName(QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8:
        return QStringLiteral("UInt8");
    case QAudioFormat::Int16:
        return QStringLiteral("Int16");
    case QAudioFormat::Int32:
        return QStringLiteral("Int32");
    case QAudioFormat::Float:
        return QStringLiteral("Float");
    case QAudioFormat::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

QAudioFormat sourceFormat22050MonoInt16()
{
    QAudioFormat fmt;
    fmt.setSampleRate(22050);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);
    return fmt;
}

QAudioFormat mixFormat48000StereoFloat()
{
    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Float);
    return fmt;
}

qint64 frameCountForMonoInt16(const QByteArray &pcm)
{
    return pcm.size() / static_cast<qint64>(sizeof(qint16));
}

qint64 expectedUSecs(qint64 sourceFrames, int sourceRate)
{
    if (sourceRate <= 0 || sourceFrames <= 0) {
        return 0;
    }
    return sourceFrames * 1000000 / sourceRate;
}

QVector<float> int16MonoToFloat(const QByteArray &pcm)
{
    const int n = int(frameCountForMonoInt16(pcm));
    QVector<float> out(n);
    const auto *src = reinterpret_cast<const qint16 *>(pcm.constData());
    for (int i = 0; i < n; ++i) {
        out[i] = src[i] / 32768.0f;
    }
    return out;
}

QVector<float> resampleMonoFloatLanczos(const QVector<float> &input, int srcRate, int dstRate)
{
    if (input.isEmpty() || srcRate <= 0 || dstRate <= 0) {
        return {};
    }
    if (srcRate == dstRate) {
        return input;
    }
    const int srcN = input.size();
    const int dstN = qMax(1, int(qint64(srcN) * dstRate / srcRate));
    QVector<float> out(dstN);
    const int a = kLanczosA;
    for (int i = 0; i < dstN; ++i) {
        const double pos = double(i) * srcRate / dstRate;
        const int i0 = int(std::floor(pos)) - a + 1;
        const int i1 = int(std::floor(pos)) + a;
        double sum = 0;
        double wsum = 0;
        for (int n = i0; n <= i1; ++n) {
            const int idx = qBound(0, n, srcN - 1);
            const float w = lanczos(float(pos - n), a);
            sum += double(input[idx]) * double(w);
            wsum += double(w);
        }
        out[i] = wsum > 1.0e-12 ? float(sum / wsum) : 0.0f;
    }
    return out;
}

float softLimit(float sample, float knee, float ceiling)
{
    const float cap = qMax(knee + 0.01f, ceiling);
    const float a = std::fabs(sample);
    if (a <= knee) {
        return sample;
    }
    const float head = qMax(1.0e-6f, cap - knee);
    const float shaped = knee + head * std::tanh((a - knee) / head);
    const float limited = qMin(shaped, cap * 0.999f);
    return std::copysign(limited, sample);
}

int applySoftLimiter(QVector<float> &samples, float knee, float ceiling)
{
    int engaged = 0;
    for (float &s : samples) {
        if (std::fabs(s) > knee) {
            ++engaged;
            s = softLimit(s, knee, ceiling);
        }
    }
    return engaged;
}

namespace {

qreal dbfs(qreal linear)
{
    if (linear <= 1.0e-8) {
        return -120;
    }
    return 20.0 * std::log10(linear);
}

} // namespace

QByteArray convertMonoInt16(const QByteArray &srcMonoInt16, const QAudioFormat &srcFormat,
                            const QAudioFormat &dstFormat, float gain, ConversionStats *stats)
{
    if (srcMonoInt16.isEmpty() || !dstFormat.isValid()) {
        return {};
    }

    QVector<float> mono = int16MonoToFloat(srcMonoInt16);
    // Resample at full scale first so Lanczos overshoot is visible, then apply
    // tts.volume, then a ceiling limiter. Never hand 22050 Hz to the OS mixer.
    mono = resampleMonoFloatLanczos(mono, srcFormat.sampleRate(), dstFormat.sampleRate());
    const float g = gain > 0.f ? gain : 0.7f;
    for (float &s : mono) {
        s *= g;
    }

    qreal peakPre = 0;
    for (float s : mono) {
        peakPre = qMax(peakPre, qreal(qAbs(s)));
    }
    const float ceiling = qMin(0.75f, g * 1.05f);
    const float knee = qMin(0.65f, ceiling * 0.9f);
    const int limiterEngaged = applySoftLimiter(mono, knee, ceiling);

    qreal peak = 0;
    qreal sumSq = 0;
    int atOrAbove1 = 0;
    for (float &s : mono) {
        const qreal a = qAbs(s);
        peak = qMax(peak, a);
        sumSq += double(s) * double(s);
        if (a >= 1.0) {
            ++atOrAbove1;
            s = std::copysign(ceiling * 0.999f, s);
        }
    }
    const qreal rms = mono.isEmpty() ? 0 : std::sqrt(sumSq / mono.size());

    const int ch = qMax(1, dstFormat.channelCount());
    QVector<float> interleaved(mono.size() * ch);
    for (int f = 0; f < mono.size(); ++f) {
        for (int c = 0; c < ch; ++c) {
            interleaved[f * ch + c] = mono[f];
        }
    }

    QByteArray packed = packInterleaved(interleaved, dstFormat.sampleFormat());
    const int bpf = qMax(1, dstFormat.bytesPerFrame());
    const int aligned = packed.size() - (packed.size() % bpf);
    if (aligned != packed.size()) {
        packed.truncate(aligned);
    }

    if (stats) {
        stats->clippedSamples = atOrAbove1;
        stats->limiterEngaged = limiterEngaged;
        stats->peak = peak;
        stats->peakPre = peakPre;
        stats->rms = rms;
        stats->peakDb = dbfs(peak);
        stats->rmsDb = dbfs(rms);
        stats->outFrames = bpf > 0 ? packed.size() / bpf : 0;
        stats->outBytes = packed.size();
    }
    return packed;
}

bool writeWav(const QString &path, const QByteArray &pcm, const QAudioFormat &format)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    const int channels = qMax(1, format.channelCount());
    const int rate = qMax(1, format.sampleRate());
    const int bits = format.sampleFormat() == QAudioFormat::Float
                         ? 32
                         : format.bytesPerSample() * 8;
    const quint16 audioFormat = format.sampleFormat() == QAudioFormat::Float
                                    ? quint16(3)
                                    : quint16(1);
    const quint16 blockAlign = quint16(channels * (bits / 8));
    const quint32 byteRate = quint32(rate * blockAlign);
    const quint32 dataSize = quint32(pcm.size());

    QByteArray header;
    header.append("RIFF", 4);
    appendLe32(header, 36 + dataSize);
    header.append("WAVE", 4);
    header.append("fmt ", 4);
    appendLe32(header, 16);
    appendLe16(header, audioFormat);
    appendLe16(header, quint16(channels));
    appendLe32(header, quint32(rate));
    appendLe32(header, byteRate);
    appendLe16(header, blockAlign);
    appendLe16(header, quint16(bits));
    header.append("data", 4);
    appendLe32(header, dataSize);

    if (file.write(header) != header.size() || file.write(pcm) != pcm.size()) {
        return false;
    }
    return true;
}

} // namespace PcmConverter
