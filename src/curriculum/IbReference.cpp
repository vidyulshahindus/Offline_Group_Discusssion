#include "IbReference.h"

#include "JsonResource.h"

#include <QJsonArray>
#include <QJsonObject>

bool IbReference::loadFromResources()
{
    const QJsonDocument cmdDoc =
        JsonResource::loadDocument(QStringLiteral(":/data/ib/command_terms.json"));
    if (!cmdDoc.isObject()) {
        return false;
    }
    m_terms.clear();
    const QJsonArray terms = cmdDoc.object().value(QStringLiteral("terms")).toArray();
    for (const QJsonValue &value : terms) {
        const QJsonObject obj = value.toObject();
        CommandTermInfo info;
        info.term = obj.value(QStringLiteral("term")).toString();
        info.tier = obj.value(QStringLiteral("tier")).toInt();
        info.definition = obj.value(QStringLiteral("definition")).toString();
        info.shape = obj.value(QStringLiteral("shape")).toString();
        info.spoken = obj.value(QStringLiteral("spoken")).toBool(true);
        m_terms.insert(info.term.toLower(), info);
    }

    const QJsonDocument gcDoc =
        JsonResource::loadDocument(QStringLiteral(":/data/ib/global_contexts.json"));
    m_globalContexts.clear();
    if (gcDoc.isObject()) {
        const QJsonArray contexts = gcDoc.object().value(QStringLiteral("global_contexts")).toArray();
        for (const QJsonValue &value : contexts) {
            const QJsonObject obj = value.toObject();
            const QString name = obj.value(QStringLiteral("name")).toString();
            m_globalContexts.insert(name, obj.value(QStringLiteral("focus")).toString());
        }
    }

    const QJsonDocument mypDoc =
        JsonResource::loadDocument(QStringLiteral(":/data/ib/myp_key_concepts.json"));
    m_mypKeyConcepts.clear();
    if (mypDoc.isObject()) {
        const QJsonObject defs =
            mypDoc.object().value(QStringLiteral("is_definitions")).toObject();
        for (auto it = defs.begin(); it != defs.end(); ++it) {
            m_mypKeyConcepts.insert(it.key(), it.value().toString());
        }
    }
    return !m_terms.isEmpty();
}

CommandTermInfo IbReference::commandTerm(const QString &term) const
{
    return m_terms.value(term.trimmed().toLower());
}

QString IbReference::globalContextSummary(const QString &name) const
{
    return m_globalContexts.value(name);
}

QString IbReference::mypKeyConceptBlurb(const QString &name) const
{
    return m_mypKeyConcepts.value(name);
}
