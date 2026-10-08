#include "Metrics.h"

#include <QtMath>

#include <algorithm>

void Metrics::recordTurn(const TurnMetrics &turn)
{
    m_last = turn;
    if (turn.firstTokenMs >= 0) {
        m_firstTokenSamples.append(turn.firstTokenMs);
        if (m_firstTokenSamples.size() > 50) {
            m_firstTokenSamples.remove(0, m_firstTokenSamples.size() - 50);
        }
    }
}

QVector<qint64> Metrics::recentFirstTokenMs() const
{
    return m_firstTokenSamples;
}

static double percentile(QVector<qint64> samples, double p)
{
    if (samples.isEmpty()) {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    const int index = qBound(0, static_cast<int>(qCeil(p * samples.size())) - 1, samples.size() - 1);
    return static_cast<double>(samples.at(index));
}

double Metrics::rollingP50FirstToken() const
{
    return percentile(m_firstTokenSamples, 0.5);
}

double Metrics::rollingP95FirstToken() const
{
    return percentile(m_firstTokenSamples, 0.95);
}
