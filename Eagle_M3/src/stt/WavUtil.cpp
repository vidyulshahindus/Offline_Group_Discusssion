#include "WavUtil.h"

#include <QtEndian>

namespace {

void appendLe16(QByteArray &out, quint16 v)
{
    char b[2];
    qToLittleEndian(v, b);
    out.append(b, 2);
}

void appendLe32(QByteArray &out, quint32 v)
{
    char b[4];
    qToLittleEndian(v, b);
    out.append(b, 4);
}

} // namespace

QByteArray WavUtil::wrapPcm16Mono(const QByteArray &pcm, int sampleRate)
{
    QByteArray wav;
    wav.reserve(44 + pcm.size());
    wav.append("RIFF", 4);
    appendLe32(wav, quint32(36 + pcm.size()));
    wav.append("WAVE", 4);
    wav.append("fmt ", 4);
    appendLe32(wav, 16);                       // PCM fmt chunk size
    appendLe16(wav, 1);                        // PCM
    appendLe16(wav, 1);                        // mono
    appendLe32(wav, quint32(sampleRate));
    appendLe32(wav, quint32(sampleRate * 2));  // byte rate
    appendLe16(wav, 2);                        // block align
    appendLe16(wav, 16);                       // bits per sample
    wav.append("data", 4);
    appendLe32(wav, quint32(pcm.size()));
    wav.append(pcm);
    return wav;
}
