#pragma once

#include <QAudioFormat>
#include <QByteArray>

class PcmTo16kMono
{
public:
    void setInputFormat(const QAudioFormat &format);
    QByteArray convert(const QByteArray &raw) const;

private:
    QAudioFormat m_format;
};
