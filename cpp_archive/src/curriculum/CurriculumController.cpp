#include "CurriculumController.h"

#include "Paths.h"
#include "PromptBuilder.h"
#include "UnitContextBuilder.h"

#include <QSettings>

namespace {

constexpr auto kSettingsOrg = "EagleGD";
constexpr auto kSettingsApp = "EagleGD";

} // namespace

CurriculumController::CurriculumController(const Paths &paths, PromptBuilder *prompts,
                                           QObject *parent)
    : QObject(parent)
    , m_library(paths)
    , m_prompts(prompts)
{
    m_clm.loadFromResources();
    m_ib.loadFromResources();
    m_context = UnitContextBuilder::none();
}

void CurriculumController::restoreSettings()
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    m_period = settings.value(QStringLiteral("curriculum/period"), 4).toInt();
    const QString packId = settings.value(QStringLiteral("curriculum/packId")).toString();
    m_topicIndex = settings.value(QStringLiteral("curriculum/topicIndex"), 0).toInt();
    if (packId.isEmpty()) {
        applySelection(QString(), 0, false);
        return;
    }
    applySelection(packId, m_topicIndex, false);
}

void CurriculumController::saveSettings() const
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.setValue(QStringLiteral("curriculum/period"), m_period);
    settings.setValue(QStringLiteral("curriculum/packId"), m_selectedPackId);
    settings.setValue(QStringLiteral("curriculum/topicIndex"), m_topicIndex);
}

void CurriculumController::setWarming(bool warming)
{
    if (m_warming == warming) {
        return;
    }
    m_warming = warming;
    emit warmingChanged(warming);
}

void CurriculumController::selectNoUnit()
{
    applySelection(QString(), 0, true);
}

void CurriculumController::selectPack(const QString &packId, int topicIndex)
{
    applySelection(packId, topicIndex, true);
}

void CurriculumController::applySelection(const QString &packId, int topicIndex, bool requestWarmup)
{
    const bool hadUnit = !m_selectedPackId.isEmpty();
    if (packId.isEmpty()) {
        const bool changed = hadUnit;
        m_selectedPackId.clear();
        m_topicIndex = 0;
        rebuildContext(changed, requestWarmup);
        return;
    }
    const UnitPack pack = m_library.packById(packId);
    if (pack.id().isEmpty()) {
        applySelection(QString(), 0, requestWarmup);
        return;
    }
    const bool changed = packId != m_selectedPackId || topicIndex != m_topicIndex;
    m_selectedPackId = packId;
    m_topicIndex = qBound(0, topicIndex, qMax(0, pack.topicCount() - 1));
    rebuildContext(changed, requestWarmup);
}

void CurriculumController::setPeriod(int period)
{
    m_period = qBound(1, period, 15);
}

QString CurriculumController::contextLine(const QString &modeLabel, const QString &languageLabel,
                                          const QString &wordLimitLabel) const
{
    return contextStatusLine(modeLabel, languageLabel, wordLimitLabel);
}

QString CurriculumController::periodLabel() const
{
    return m_clm.periodLabel(m_period);
}

void CurriculumController::rebuildContext(bool unitOrTopicChanged, bool requestWarmup)
{
    if (m_selectedPackId.isEmpty()) {
        m_context = UnitContextBuilder::none();
    } else {
        const UnitPack pack = m_library.packById(m_selectedPackId);
        m_context = UnitContextBuilder::fromPack(pack, m_topicIndex, m_ib);
    }
    emit contextChanged(m_context, unitOrTopicChanged);
    if (unitOrTopicChanged && requestWarmup && m_prompts) {
        const QString system = m_prompts->systemPrompt(m_context);
        const QString title =
            m_context.hasUnit ? m_context.unitTitle : QStringLiteral("General");
        emit warmupRequested(system, title);
    }
}

QString CurriculumController::contextStatusLine(const QString &modeLabel,
                                                  const QString &languageLabel,
                                                  const QString &wordLimitLabel) const
{
    const QString unitPart =
        m_context.hasUnit ? m_context.unitTitle : QStringLiteral("No unit");
    const QString topicPart =
        m_context.hasUnit ? m_context.topicTitle : QStringLiteral("—");
    const QString periodPart = QStringLiteral("P%1").arg(m_period);
    const QString modePart = modeLabel.isEmpty() ? QStringLiteral("Doubt") : modeLabel;
    const QString langPart = languageLabel.isEmpty() ? QStringLiteral("English") : languageLabel;
    const QString limitPart = wordLimitLabel.isEmpty() ? QStringLiteral("75") : wordLimitLabel;
    return QStringLiteral("%1 › %2 › %3 › %4 › %5 › %6 words")
        .arg(unitPart, topicPart, periodPart, modePart, langPart, limitPart);
}
