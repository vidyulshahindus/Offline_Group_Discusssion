#include "UnitPack.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace {

int countWords(const QString &text)
{
    return text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
}

bool conceptWordCountOk(const QString &concept)
{
    const int n = countWords(concept.trimmed());
    return n >= 2 && n <= 4;
}

QString requireString(const QJsonObject &obj, const QString &key, QStringList *errors)
{
    if (!obj.contains(key) || !obj.value(key).isString()) {
        errors->append(QStringLiteral("Missing or invalid string field '%1'.").arg(key));
        return {};
    }
    const QString v = obj.value(key).toString().trimmed();
    if (v.isEmpty()) {
        errors->append(QStringLiteral("Required field '%1' is empty.").arg(key));
    }
    return v;
}

} // namespace

UnitPackValidation UnitPack::validate() const
{
    UnitPackValidation result;
    if (m_doc.isEmpty()) {
        result.errors.append(QStringLiteral("Unit pack document is empty."));
        return result;
    }

    const QString schema = requireString(m_doc, QStringLiteral("schema"), &result.errors);
    if (!schema.isEmpty() && schema != QStringLiteral("eagle.unitpack.v1")) {
        result.errors.append(QStringLiteral("Unsupported schema '%1' (expected eagle.unitpack.v1).")
                                .arg(schema));
    }
    requireString(m_doc, QStringLiteral("id"), &result.errors);
    requireString(m_doc, QStringLiteral("title"), &result.errors);
    requireString(m_doc, QStringLiteral("programme"), &result.errors);
    requireString(m_doc, QStringLiteral("subject"), &result.errors);
    requireString(m_doc, QStringLiteral("subject_group"), &result.errors);

    if (!m_doc.contains(QStringLiteral("grade")) || !m_doc.value(QStringLiteral("grade")).isDouble()) {
        result.errors.append(QStringLiteral("Missing or invalid integer field 'grade'."));
    } else {
        const int grade = m_doc.value(QStringLiteral("grade")).toInt();
        if (grade < 6 || grade > 12) {
            result.errors.append(QStringLiteral("Field 'grade' must be between 6 and 12."));
        }
    }

    const QJsonObject clm = m_doc.value(QStringLiteral("clm")).toObject();
    if (clm.isEmpty()) {
        result.errors.append(QStringLiteral("Missing object 'clm'."));
        return result;
    }

    const QString concept = requireString(clm, QStringLiteral("concept"), &result.errors);
    if (!concept.isEmpty() && !conceptWordCountOk(concept)) {
        result.errors.append(QStringLiteral("clm.concept must be 2–4 words."));
    }
    requireString(clm, QStringLiteral("concept_meaning"), &result.errors);
    requireString(clm, QStringLiteral("relevance"), &result.errors);
    requireString(clm, QStringLiteral("application"), &result.errors);
    requireString(clm, QStringLiteral("wisdom_question"), &result.errors);

    const QJsonArray transfers = clm.value(QStringLiteral("transfers")).toArray();
    if (transfers.size() < 3) {
        result.errors.append(QStringLiteral("clm.transfers must contain at least 3 items."));
    }

    const QJsonArray topics = clm.value(QStringLiteral("topics")).toArray();
    if (topics.isEmpty()) {
        result.errors.append(QStringLiteral("clm.topics must contain at least one topic."));
    }

    const QJsonArray goals = clm.value(QStringLiteral("reflection"))
                                 .toObject()
                                 .value(QStringLiteral("learning_goals"))
                                 .toArray();
    if (goals.isEmpty()) {
        result.errors.append(QStringLiteral("clm.reflection.learning_goals is required."));
    }

    const QJsonObject authoring = m_doc.value(QStringLiteral("authoring")).toObject();
    const QString status = requireString(authoring, QStringLiteral("status"), &result.errors);
    if (!status.isEmpty()
        && status != QStringLiteral("draft") && status != QStringLiteral("reviewed")
        && status != QStringLiteral("approved")) {
        result.errors.append(
            QStringLiteral("authoring.status must be draft, reviewed, or approved."));
    }

    for (int i = 0; i < topics.size(); ++i) {
        const QJsonObject topic = topics.at(i).toObject();
        requireString(topic, QStringLiteral("id"), &result.errors);
        requireString(topic, QStringLiteral("title"), &result.errors);
        requireString(topic, QStringLiteral("summary"), &result.errors);
        requireString(topic, QStringLiteral("key_facts"), &result.errors);
        const QString keyFacts = topic.value(QStringLiteral("key_facts")).toString();
        if (!keyFacts.isEmpty() && countWords(keyFacts) > 120) {
            result.errors.append(QStringLiteral("Topic '%1' key_facts exceeds 120 words.")
                                    .arg(topic.value(QStringLiteral("id")).toString()));
        }
        const QJsonArray terms = topic.value(QStringLiteral("key_terms")).toArray();
        if (terms.isEmpty()) {
            result.errors.append(QStringLiteral("Topic '%1' must include key_terms.")
                                    .arg(topic.value(QStringLiteral("id")).toString()));
        }
        const QJsonArray ctqs = topic.value(QStringLiteral("ctqs")).toArray();
        if (status == QStringLiteral("approved")) {
            if (topics.size() != 3) {
                result.errors.append(QStringLiteral("Approved packs must have exactly 3 topics."));
            }
            if (ctqs.size() != 3) {
                result.errors.append(
                    QStringLiteral("Approved topic '%1' must have exactly 3 CTQs.")
                        .arg(topic.value(QStringLiteral("id")).toString()));
            }
        }
    }

    return result;
}
