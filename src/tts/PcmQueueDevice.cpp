#include "PcmQueueDevice.h"

#include <cstring>

PcmQueueDevice::PcmQueueDevice(QObject *parent)
    : QIODevice(parent)
{
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
}

void PcmQueueDevice::setBytesPerFrame(int bytesPerFrame)
{
    m_bytesPerFrame = qMax(1, bytesPerFrame);
}

bool PcmQueueDevice::isSequential() const
{
    return true;
}

qint64 PcmQueueDevice::queuedBytes() const
{
    qint64 n = m_current.size();
    for (const QByteArray &chunk : m_chunks) {
        n += chunk.size();
    }
    return n;
}

qint64 PcmQueueDevice::bytesAvailable() const
{
    const qint64 raw = queuedBytes() + QIODevice::bytesAvailable();
    const int bpf = qMax(1, m_bytesPerFrame);
    return raw - (raw % bpf);
}

void PcmQueueDevice::setStreamOpen(bool open)
{
    m_streamOpen = open;
}

bool PcmQueueDevice::atEnd() const
{
    if (m_streamOpen) {
        return false;
    }
    return queuedBytes() < m_bytesPerFrame;
}

void PcmQueueDevice::enqueue(const QByteArray &pcm)
{
    if (pcm.isEmpty()) {
        return;
    }
    QByteArray aligned = pcm;
    const int bpf = qMax(1, m_bytesPerFrame);
    const int rem = aligned.size() % bpf;
    if (rem != 0) {
        aligned.append(QByteArray(bpf - rem, 0));
    }
    m_chunks.enqueue(aligned);
    emit readyRead();
}

void PcmQueueDevice::clearQueue()
{
    m_chunks.clear();
    m_current.clear();
    m_bytesPulled = 0;
    m_partialReadsDropped = 0;
}

bool PcmQueueDevice::hasData() const
{
    return queuedBytes() >= m_bytesPerFrame;
}

qint64 PcmQueueDevice::readData(char *data, qint64 maxSize)
{
    const int bpf = qMax(1, m_bytesPerFrame);
    maxSize -= maxSize % bpf;
    if (maxSize < bpf) {
        ++m_partialReadsDropped;
        return 0;
    }

    qint64 written = 0;
    while (written + bpf <= maxSize) {
        if (m_current.size() < bpf && !m_chunks.isEmpty()) {
            if (m_current.isEmpty()) {
                m_current = m_chunks.dequeue();
            } else {
                m_current.append(m_chunks.dequeue());
            }
            continue;
        }
        if (m_current.size() < bpf) {
            break;
        }
        const qint64 room = maxSize - written;
        qint64 toCopy = qMin(room, static_cast<qint64>(m_current.size()));
        toCopy -= toCopy % bpf;
        if (toCopy < bpf) {
            break;
        }
        memcpy(data + written, m_current.constData(), static_cast<size_t>(toCopy));
        m_current.remove(0, int(toCopy));
        written += toCopy;
    }
    m_bytesPulled += written;
    return written;
}

qint64 PcmQueueDevice::writeData(const char *data, qint64 maxSize)
{
    Q_UNUSED(data);
    Q_UNUSED(maxSize);
    return -1;
}
