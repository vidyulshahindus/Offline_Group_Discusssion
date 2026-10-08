#pragma once

#include "ILlmBackend.h"
#include "PromptTypes.h"

#include <QVector>

class AppConfig;
class TokenEstimator;

class HistoryManager
{
public:
    HistoryManager(const AppConfig &config, TokenEstimator &estimator);

    void reset();
    bool isEmpty() const;
    const QVector<LlmMessage> &messages() const { return m_messages; }

    void appendTurn(const QString &userTurnMessage, const QString &spokenAssistantText,
                    SessionLanguage language);
    void appendFollowUpToLastAssistant(const QString &followUpQuestion);
    QVector<LlmMessage> trimmedHistory(SessionLanguage language) const;

private:
    void trimIfNeeded(SessionLanguage language);

    const AppConfig &m_config;
    TokenEstimator &m_estimator;
    QVector<LlmMessage> m_messages;
};
