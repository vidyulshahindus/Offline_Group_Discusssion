#pragma once

#include <QByteArray>

namespace WavUtil {

// Wraps raw mono PCM16 little-endian samples in a 44-byte RIFF/WAVE header.
QByteArray wrapPcm16Mono(const QByteArray &pcm, int sampleRate = 16000);

} // namespace WavUtil
