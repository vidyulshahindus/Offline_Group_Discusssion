#pragma once

#include <QByteArray>

namespace WavUtil {

QByteArray pcm16kMonoToWav(const QByteArray &pcm16le);

} // namespace WavUtil
