#pragma once

#include "TurnTimestamps.h"
#include "llm/ILlmBackend.h"

#include <QVector>

struct TurnMetrics {
    qint64 turnId = -1;
    QString model;
    qint64 requestSentMs = 0;
    qint64 firstTokenMs = -1;
    qint64 llmDoneMs = -1;
    LlmMetrics llm;
    int estimatedNewPromptTokens = 0;
    bool cacheLikelyHit = false;
    TurnTimestamps timestamps;
    qint64 t0ToT1Ms = -1;
    qint64 ttsFirstSynthMs = -1;
    qint64 ttsWarmSynthMs = -1;
    qint64 ttsWorkerColdSynthMs = -1;
    qint64 ttsWorkerWarmSynthMs = -1;
};

class Metrics
{
public:
    void recordTurn(const TurnMetrics &turn);
    const TurnMetrics &lastTurn() const { return m_last; }
    QVector<qint64> recentFirstTokenMs() const;

    double rollingP50FirstToken() const;
    double rollingP95FirstToken() const;
    double rollingP50T0ToT1() const;
    double rollingP95T0ToT1() const;

private:
    TurnMetrics m_last;
    QVector<qint64> m_firstTokenSamples;
    QVector<qint64> m_t0ToT1Samples;
};
