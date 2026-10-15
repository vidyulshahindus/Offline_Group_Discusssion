#include "HistoryManager.h"

#include "AppConfig.h"
#include "TokenEstimator.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcHistory, "eagle.history")

HistoryManager::HistoryManager(const AppConfig &config, TokenEstimator &estimator)
    : m_config(config)
    , m_estimator(estimator)
{
}

void HistoryManager::reset()
{
    m_messages.clear();
}

bool HistoryManager::isEmpty() const
{
    return m_messages.isEmpty();
}

void HistoryManager::appendTurn(const QString &userTurnMessage, const QString &spokenAssistantText,
                                SessionLanguage language)
{
    LlmMessage user;
    user.role = QStringLiteral("user");
    user.content = userTurnMessage;
    m_messages.append(user);

    LlmMessage assistant;
    assistant.role = QStringLiteral("assistant");
    assistant.content = spokenAssistantText;
    m_messages.append(assistant);

    trimIfNeeded(language);
}

void HistoryManager::appendFollowUpToLastAssistant(const QString &followUpQuestion)
{
    if (m_messages.isEmpty() || m_messages.last().role != QStringLiteral("assistant")) {
        return;
    }
    QString &content = m_messages.last().content;
    if (!content.endsWith(QChar(QLatin1Char('.'))) && !content.endsWith(QChar(QLatin1Char('?'))) &&
        !content.endsWith(QChar(0x0964))) {
        content.append(QLatin1Char('.'));
    }
    content.append(QLatin1Char(' '));
    content.append(followUpQuestion.trimmed());
}

QVector<LlmMessage> HistoryManager::trimmedHistory(SessionLanguage language) const
{
    return m_messages;
}

void HistoryManager::trimIfNeeded(SessionLanguage language)
{
    const QJsonObject llm = m_config.json().value(QStringLiteral("llm")).toObject();
    const int budget = llm.value(QStringLiteral("history_budget_tokens")).toInt(2200);
    const int trimTo = llm.value(QStringLiteral("history_trim_to_tokens")).toInt(1400);

    int total = 0;
    for (const LlmMessage &msg : m_messages) {
        total += m_estimator.estimateTokens(msg.content, language);
    }
    if (total <= budget) {
        return;
    }

    while (total > trimTo && m_messages.size() > 4) {
        m_messages.remove(0, 2);
        total = 0;
        for (const LlmMessage &msg : m_messages) {
            total += m_estimator.estimateTokens(msg.content, language);
        }
        qCInfo(lcHistory) << "Trimmed oldest history turn; tokens now about" << total;
    }
}
