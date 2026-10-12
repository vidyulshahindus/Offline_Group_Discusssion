#include "UnitContextBuilder.h"

#include "IbReference.h"

#include <QJsonArray>
#include <QJsonObject>

namespace {

QString registerShort(const QString &programme, int grade)
{
    if (programme == QStringLiteral("DP")) {
        return QStringLiteral("Grade %1 DP").arg(grade);
    }
    if (programme == QStringLiteral("MYP")) {
        return QStringLiteral("Grade %1 MYP").arg(grade);
    }
    return QStringLiteral("Grade %1").arg(grade);
}

QString buildIbLinks(const QJsonObject &ibRoot, const IbReference &ib)
{
    QStringList parts;
    const QJsonObject myp = ibRoot.value(QStringLiteral("myp")).toObject();
    if (!myp.isEmpty()) {
        const QString keyConcept = myp.value(QStringLiteral("key_concept")).toString();
        if (!keyConcept.isEmpty()) {
            const QString blurb = ib.mypKeyConceptBlurb(keyConcept);
            parts.append(QStringLiteral("MYP key concept: %1%2")
                             .arg(keyConcept,
                                  blurb.isEmpty() ? QString() : QStringLiteral(" — ") + blurb));
        }
        const QString gc = myp.value(QStringLiteral("global_context")).toString();
        if (!gc.isEmpty()) {
            const QString focus = ib.globalContextSummary(gc);
            parts.append(QStringLiteral("Global context: %1%2")
                             .arg(gc, focus.isEmpty() ? QString() : QStringLiteral(" — ") + focus));
        }
        const QJsonArray related = myp.value(QStringLiteral("related_concepts")).toArray();
        if (!related.isEmpty()) {
            QStringList names;
            for (const QJsonValue &v : related) {
                names.append(v.toString());
            }
            parts.append(QStringLiteral("Related concepts: %1").arg(names.join(QStringLiteral(", "))));
        }
    }
    const QJsonObject dp = ibRoot.value(QStringLiteral("dp")).toObject();
    if (!dp.isEmpty()) {
        const QString theme = dp.value(QStringLiteral("theme_or_organiser")).toString();
        if (!theme.isEmpty()) {
            parts.append(QStringLiteral("DP theme: %1").arg(theme));
        }
    }
    const QJsonArray lp = ibRoot.value(QStringLiteral("learner_profile")).toArray();
    if (!lp.isEmpty()) {
        QStringList attrs;
        for (const QJsonValue &v : lp) {
            attrs.append(v.toString());
        }
        parts.append(QStringLiteral("Learner profile: %1").arg(attrs.join(QStringLiteral(", "))));
    }
    return parts.join(QStringLiteral("; "));
}

} // namespace

UnitContext UnitContextBuilder::none()
{
    UnitContext unit;
    unit.hasUnit = false;
    unit.registerShort = QStringLiteral("Grade 9 MYP");
    unit.concept.clear();
    return unit;
}

UnitContext UnitContextBuilder::fromPack(const UnitPack &pack, int topicIndex,
                                           const IbReference &ib)
{
    UnitContext unit;
    unit.hasUnit = true;
    unit.packId = pack.id();
    unit.programme = pack.programme();
    unit.grade = QString::number(pack.grade());
    unit.subject = pack.subject();
    unit.registerShort = registerShort(pack.programme(), pack.grade());
    unit.unitTitle = pack.title();

    const QJsonObject clm = pack.document().value(QStringLiteral("clm")).toObject();
    unit.concept = clm.value(QStringLiteral("concept")).toString();
    unit.conceptMeaning = clm.value(QStringLiteral("concept_meaning")).toString();
    unit.relevance = clm.value(QStringLiteral("relevance")).toString();
    unit.wisdomQuestion = clm.value(QStringLiteral("wisdom_question")).toString();

    const QJsonArray topics = clm.value(QStringLiteral("topics")).toArray();
    if (topicIndex < 0 || topicIndex >= topics.size()) {
        topicIndex = 0;
    }
    const QJsonObject topic = topics.at(topicIndex).toObject();
    unit.topicId = topic.value(QStringLiteral("id")).toString();
    unit.topicTitle = topic.value(QStringLiteral("title")).toString();
    unit.keyFacts = topic.value(QStringLiteral("key_facts")).toString();

    unit.ibLinks = buildIbLinks(pack.document().value(QStringLiteral("ib")).toObject(), ib);
    return unit;
}
