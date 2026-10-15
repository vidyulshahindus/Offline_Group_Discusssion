#pragma once

#include "ClmReference.h"
#include "IbReference.h"
#include "PromptTypes.h"
#include "UnitLibrary.h"

#include <QObject>
#include <QString>

class Paths;
class PromptBuilder;

class CurriculumController : public QObject
{
    Q_OBJECT

public:
    explicit CurriculumController(const Paths &paths, PromptBuilder *prompts,
                                  QObject *parent = nullptr);

    const UnitLibrary &library() const { return m_library; }
    const ClmReference &clm() const { return m_clm; }
    const IbReference &ib() const { return m_ib; }

    UnitContext currentContext() const { return m_context; }
    int period() const { return m_period; }
    QString periodLabel() const;
    bool hasUnitSelected() const { return m_selectedPackId.isEmpty() == false; }
    QString selectedPackId() const { return m_selectedPackId; }
    int selectedTopicIndex() const { return m_topicIndex; }

    bool isWarming() const { return m_warming; }
    void setWarming(bool warming);

    void restoreSettings();
    void saveSettings() const;

    void selectNoUnit();
    void selectPack(const QString &packId, int topicIndex);
    void setPeriod(int period);
    QString contextLine(const QString &modeLabel, const QString &languageLabel,
                        const QString &wordLimitLabel) const;

signals:
    void contextChanged(const UnitContext &context, bool unitOrTopicChanged);
    void warmupRequested(const QString &systemPrompt, const QString &unitTitle);
    void warmingChanged(bool warming);
    void contextLineChanged(const QString &line);

private:
    void applySelection(const QString &packId, int topicIndex, bool requestWarmup);
    void rebuildContext(bool unitOrTopicChanged, bool requestWarmup);
    QString contextStatusLine(const QString &modeLabel, const QString &languageLabel,
                              const QString &wordLimitLabel) const;

    UnitLibrary m_library;
    ClmReference m_clm;
    IbReference m_ib;
    PromptBuilder *m_prompts = nullptr;
    UnitContext m_context;
    QString m_selectedPackId;
    int m_topicIndex = 0;
    int m_period = 4;
    bool m_warming = false;
};
