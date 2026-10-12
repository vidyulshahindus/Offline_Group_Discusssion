#include "WavUtil.h"

#include <QtEndian>

namespace WavUtil {

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

QByteArray pcm16kMonoToWav(const QByteArray &pcm16le)
{
    QByteArray header;
    header.reserve(44);
    header.append("RIFF", 4);
    appendLe32(header, quint32(36 + pcm16le.size()));
    header.append("WAVE", 4);
    header.append("fmt ", 4);
    appendLe32(header, 16);
    appendLe16(header, 1);
    appendLe16(header, 1);
    appendLe32(header, 16000);
    appendLe32(header, 16000 * 2);
    appendLe16(header, 2);
    appendLe16(header, 16);
    header.append("data", 4);
    appendLe32(header, quint32(pcm16le.size()));
    header.append(pcm16le);
    return header;
}

} // namespace WavUtil
