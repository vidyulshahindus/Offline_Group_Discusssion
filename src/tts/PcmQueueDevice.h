#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QQueue>

class PcmQueueDevice : public QIODevice
{
    Q_OBJECT

public:
    explicit PcmQueueDevice(QObject *parent = nullptr);

    void setBytesPerFrame(int bytesPerFrame);
    int bytesPerFrame() const { return m_bytesPerFrame; }

    void setStreamOpen(bool open);
    bool streamOpen() const { return m_streamOpen; }

    void enqueue(const QByteArray &pcm);
    void clearQueue();
    bool hasData() const;
    qint64 queuedBytes() const;
    qint64 bytesPulled() const { return m_bytesPulled; }
    qint64 partialReadsDropped() const { return m_partialReadsDropped; }

    bool isSequential() const override;
    qint64 bytesAvailable() const override;
    bool atEnd() const override;

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 maxSize) override;

private:
    QQueue<QByteArray> m_chunks;
    QByteArray m_current;
    qint64 m_bytesPulled = 0;
    qint64 m_partialReadsDropped = 0;
    int m_bytesPerFrame = 1;
    bool m_streamOpen = true;
};
