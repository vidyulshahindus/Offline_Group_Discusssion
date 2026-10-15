#pragma once

#include <QtGlobal>

struct TurnTimestamps {
    qint64 t0Ms = 0;
    qint64 sttDoneMs = -1;
    qint64 promptBuiltMs = -1;
    qint64 requestSentMs = -1;
    qint64 firstTokenMs = -1;
    qint64 firstSentenceMs = -1;
    qint64 firstTtsDoneMs = -1;
    qint64 firstAudioMs = -1;
    qint64 llmDoneMs = -1;
    qint64 speechDoneMs = -1;
};
