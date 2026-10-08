#pragma once

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
};

class Metrics
{
public:
    void recordTurn(const TurnMetrics &turn);
    const TurnMetrics &lastTurn() const { return m_last; }
    QVector<qint64> recentFirstTokenMs() const;

    double rollingP50FirstToken() const;
    double rollingP95FirstToken() const;

private:
    TurnMetrics m_last;
    QVector<qint64> m_firstTokenSamples;
};
