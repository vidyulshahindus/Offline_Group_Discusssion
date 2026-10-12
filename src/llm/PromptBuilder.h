#pragma once

#include "ILlmBackend.h"
#include "PromptTypes.h"

#include <QHash>
#include <QString>
#include <QStringList>

class AppConfig;
class Paths;
class TokenEstimator;

class PromptBuilder
{
public:
    PromptBuilder(const AppConfig &config, const Paths &paths, TokenEstimator &estimator);

    QStringList validationErrors() const;
    bool loadTemplates();

    QString systemPrompt(const UnitContext &unit = UnitContext()) const;
    QString buildUserTurn(const TurnInputs &inputs, const UnitContext &unit) const;
    QVector<LlmMessage> buildMessages(const TurnInputs &inputs, const UnitContext &unit,
                                      const QVector<LlmMessage> &history) const;

    int estimateNewTurnTokens(const TurnInputs &inputs, const UnitContext &unit,
                              const QVector<LlmMessage> &history = {}) const;

private:
    QString loadTemplateFile(const QString &name) const;
    QString renderTail(const TurnInputs &inputs) const;
    QHash<QString, QString> baseVars(const TurnInputs &inputs, const UnitContext &unit) const;
    QString languageInstruction(SessionLanguage language) const;

    const AppConfig &m_config;
    const Paths &m_paths;
    TokenEstimator &m_estimator;

    QString m_systemSchool;
    QString m_unitCard;
    QString m_unitCardNone;
    QString m_doubt;
    QString m_tail;
    QString unitCacheKey(const UnitContext &unit) const;

    mutable QString m_cachedSystem;
    mutable QString m_cachedUnitKey;
    mutable UnitContext m_cachedUnitContext;
};
