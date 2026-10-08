#include "PromptBuilder.h"

#include "AppConfig.h"
#include "Paths.h"
#include "TemplateEngine.h"
#include "TokenEstimator.h"

#include <QDir>
#include <QFile>

namespace {

QString registerForGrade(const QString &programme, const QString &grade)
{
    const int g = grade.toInt();
    if (programme == QStringLiteral("DP") || g >= 11) {
        return QStringLiteral("precise subject vocabulary and an assessment-aware tone");
    }
    if (g <= 8) {
        return QStringLiteral("simple words, short sentences and everyday examples");
    }
    return QStringLiteral("clear academic language that stays accessible");
}

} // namespace

PromptBuilder::PromptBuilder(const AppConfig &config, const Paths &paths, TokenEstimator &estimator)
    : m_config(config)
    , m_paths(paths)
    , m_estimator(estimator)
{
}

bool PromptBuilder::loadTemplates()
{
    m_systemSchool = loadTemplateFile(QStringLiteral("system_school.txt"));
    m_unitCard = loadTemplateFile(QStringLiteral("unit_card.txt"));
    m_unitCardNone = loadTemplateFile(QStringLiteral("unit_card_none.txt"));
    m_doubt = loadTemplateFile(QStringLiteral("doubt.txt"));
    m_tail = loadTemplateFile(QStringLiteral("tail.txt"));
    m_cachedSystem.clear();
    return !m_systemSchool.isEmpty() && !m_doubt.isEmpty() && !m_tail.isEmpty();
}

QString PromptBuilder::loadTemplateFile(const QString &name) const
{
    const QString dataOverride = QDir(m_paths.dataDir()).filePath(QStringLiteral("prompts/") + name);
    if (QFile::exists(dataOverride)) {
        QFile file(dataOverride);
        if (file.open(QIODevice::ReadOnly)) {
            return QString::fromUtf8(file.readAll());
        }
    }
    QFile resource(QStringLiteral(":/prompts/") + name);
    if (resource.open(QIODevice::ReadOnly)) {
        return QString::fromUtf8(resource.readAll());
    }
    return {};
}

QStringList PromptBuilder::validationErrors() const
{
    QStringList errors;
    const QStringList requiredKeys = {
        QStringLiteral("avoid_meta"), QStringLiteral("language_instruction"), QStringLiteral("word_limit"),
        QStringLiteral("first_sentence_hint"), QStringLiteral("register_short"), QStringLiteral("concept"),
        QStringLiteral("question"), QStringLiteral("tail"),
    };

    TurnInputs sample;
    sample.wordLimit = 75;
    sample.question = QStringLiteral("sample");
    UnitContext unit;
    unit.registerShort = QStringLiteral("Grade 9 MYP");
    auto vars = baseVars(sample, unit);
    vars.insert(QStringLiteral("tail"), renderTail(sample));

    const auto checkTemplate = [&](const QString &name, const QString &text) {
        for (const QString &key : TemplateEngine::placeholdersIn(text)) {
            if (key == QStringLiteral("tail")) {
                continue;
            }
            if (!vars.contains(key) && !requiredKeys.contains(key)) {
                errors.append(QStringLiteral("%1: missing placeholder %2").arg(name, key));
            }
        }
    };

    checkTemplate(QStringLiteral("system_school.txt"), m_systemSchool);
    checkTemplate(QStringLiteral("unit_card_none.txt"), m_unitCardNone);
    checkTemplate(QStringLiteral("doubt.txt"), m_doubt);
    checkTemplate(QStringLiteral("tail.txt"), m_tail);
    return errors;
}

QString PromptBuilder::languageInstruction(SessionLanguage language) const
{
    switch (language) {
    case SessionLanguage::Hindi:
        return QStringLiteral("Hindi in Devanagari script. Mixing a few common English words is fine");
    default:
        return QStringLiteral("English");
    }
}

QHash<QString, QString> PromptBuilder::baseVars(const TurnInputs &inputs,
                                                const UnitContext &unit) const
{
    QHash<QString, QString> vars;
    const QString programme = unit.programme.isEmpty() ? QStringLiteral("MYP") : unit.programme;
    const QString grade = unit.grade.isEmpty() ? QStringLiteral("9") : unit.grade;
    vars.insert(QStringLiteral("avoid_meta"),
                m_config.schoolAvoidMetaSchoolWords() ? QStringLiteral("1") : QString());
    vars.insert(QStringLiteral("programme"), programme);
    vars.insert(QStringLiteral("subject"), unit.subject);
    vars.insert(QStringLiteral("grade"), grade);
    vars.insert(QStringLiteral("register"),
                unit.registerText.isEmpty() ? registerForGrade(programme, grade) : unit.registerText);
    vars.insert(QStringLiteral("register_short"), unit.registerShort);
    vars.insert(QStringLiteral("unit_title"), unit.unitTitle);
    vars.insert(QStringLiteral("topic_title"), unit.topicTitle);
    vars.insert(QStringLiteral("concept"), unit.concept);
    vars.insert(QStringLiteral("concept_meaning"), unit.conceptMeaning);
    vars.insert(QStringLiteral("relevance"), unit.relevance);
    vars.insert(QStringLiteral("wisdom_question"), unit.wisdomQuestion);
    vars.insert(QStringLiteral("ib_links"), unit.ibLinks);
    vars.insert(QStringLiteral("key_facts"), unit.keyFacts);
    vars.insert(QStringLiteral("language_instruction"), languageInstruction(inputs.language));
    vars.insert(QStringLiteral("word_limit"), QString::number(inputs.wordLimit));
    vars.insert(QStringLiteral("first_sentence_hint"),
                m_config.llmFirstSentenceHint() ? QStringLiteral("1") : QString());
    vars.insert(QStringLiteral("question"), inputs.question);
    vars.insert(QStringLiteral("notes"), inputs.notes);
    vars.insert(QStringLiteral("misconception"), inputs.misconception);
    return vars;
}

QString PromptBuilder::renderTail(const TurnInputs &inputs) const
{
    UnitContext unit;
    unit.registerShort = QStringLiteral("Grade 9 MYP");
    auto vars = baseVars(inputs, unit);
    return TemplateEngine::render(m_tail, vars);
}

QString PromptBuilder::systemPrompt() const
{
    UnitContext unit;
    unit.hasUnit = false;
    unit.registerShort = QStringLiteral("Grade 9 MYP");
    unit.concept = QStringLiteral("Learning and growth");

    if (m_cachedSystem.isEmpty() || m_cachedUnitContext.hasUnit != unit.hasUnit) {
        TurnInputs dummy;
        auto vars = baseVars(dummy, unit);
        const QString core = TemplateEngine::render(m_systemSchool, vars);
        const QString card = TemplateEngine::render(m_unitCardNone, vars);
        m_cachedSystem = core + card;
        m_cachedUnitContext = unit;
    }
    return m_cachedSystem;
}

QString PromptBuilder::buildUserTurn(const TurnInputs &inputs, const UnitContext &unit) const
{
    auto vars = baseVars(inputs, unit);
    vars.insert(QStringLiteral("tail"), renderTail(inputs));
    switch (inputs.mode) {
    case EagleMode::Doubt:
        return TemplateEngine::render(m_doubt, vars);
    }
    return inputs.question;
}

QVector<LlmMessage> PromptBuilder::buildMessages(const TurnInputs &inputs, const UnitContext &unit,
                                                 const QVector<LlmMessage> &history) const
{
    QVector<LlmMessage> messages;
    LlmMessage system;
    system.role = QStringLiteral("system");
    system.content = systemPrompt();
    messages.append(system);
    for (const LlmMessage &msg : history) {
        messages.append(msg);
    }
    LlmMessage user;
    user.role = QStringLiteral("user");
    user.content = buildUserTurn(inputs, unit);
    messages.append(user);
    return messages;
}

int PromptBuilder::estimateNewTurnTokens(const TurnInputs &inputs, const UnitContext &unit) const
{
    UnitContext u = unit;
    if (!u.hasUnit) {
        u.registerShort = QStringLiteral("Grade 9 MYP");
    }
    return m_estimator.estimateTokens(buildUserTurn(inputs, u), inputs.language);
}
