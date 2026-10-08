#include "TokenEstimator.h"

#include "AppConfig.h"

#include <QtMath>

namespace {

QString langKey(SessionLanguage language)
{
    switch (language) {
    case SessionLanguage::Hindi:
        return QStringLiteral("hi");
    default:
        return QStringLiteral("en");
    }
}

} // namespace

TokenEstimator::TokenEstimator(const AppConfig &config)
    : m_config(config)
{
}

double TokenEstimator::charsPerToken(SessionLanguage language) const
{
    switch (language) {
    case SessionLanguage::Hindi:
        return 1.6;
    default:
        return 3.6;
    }
}

int TokenEstimator::estimateTokens(const QString &text, SessionLanguage language) const
{
    const int chars = text.size();
    if (chars <= 0) {
        return 0;
    }
    return qMax(1, static_cast<int>(qCeil(chars / charsPerToken(language))));
}

double TokenEstimator::tokensPerWord(SessionLanguage language) const
{
    const QJsonObject perWord = m_config.json().value(QStringLiteral("llm"))
                                    .toObject()
                                    .value(QStringLiteral("tokens_per_word"))
                                    .toObject();
    const QString key = langKey(language);
    if (perWord.contains(key)) {
        return perWord.value(key).toDouble(1.4);
    }
    return perWord.value(QStringLiteral("default")).toDouble(3.0);
}

int TokenEstimator::numPredictForWordLimit(int wordLimit, SessionLanguage language) const
{
    const double raw = wordLimit * tokensPerWord(language) * 1.25 + 24.0;
    return qMin(600, qMax(32, static_cast<int>(qCeil(raw))));
}

void TokenEstimator::calibrate(SessionLanguage language, qint64 promptEvalCount, int estimatedNewTokens)
{
    if (promptEvalCount <= 0 || estimatedNewTokens <= 0) {
        return;
    }
    Q_UNUSED(language);
    Q_UNUSED(promptEvalCount);
    Q_UNUSED(estimatedNewTokens);
    // EMA calibration persisted to config in M10 benchmark; hook reserved for M1.
}
