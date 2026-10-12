#pragma once

#include <QHash>
#include <QString>

struct CommandTermInfo {
    QString term;
    int tier = 0;
    QString definition;
    QString shape;
    bool spoken = true;
};

class IbReference
{
public:
    bool loadFromResources();

    CommandTermInfo commandTerm(const QString &term) const;
    QString globalContextSummary(const QString &name) const;
    QString mypKeyConceptBlurb(const QString &name) const;

private:
    QHash<QString, CommandTermInfo> m_terms;
    QHash<QString, QString> m_globalContexts;
    QHash<QString, QString> m_mypKeyConcepts;
};
