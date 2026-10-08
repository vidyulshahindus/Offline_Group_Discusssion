#pragma once

#include "PromptTypes.h"

#include <QString>

class AppConfig;

class TokenEstimator
{
public:
    explicit TokenEstimator(const AppConfig &config);

    int estimateTokens(const QString &text, SessionLanguage language) const;
    double tokensPerWord(SessionLanguage language) const;
    int numPredictForWordLimit(int wordLimit, SessionLanguage language) const;
    void calibrate(SessionLanguage language, qint64 promptEvalCount, int estimatedNewTokens);

private:
    double charsPerToken(SessionLanguage language) const;

    const AppConfig &m_config;
};
