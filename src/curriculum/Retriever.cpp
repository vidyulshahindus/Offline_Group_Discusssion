#include "Retriever.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <QtMath>

namespace {

const QSet<QString> &englishStopwords()
{
    static const QSet<QString> stops = {
        QStringLiteral("a"),  QStringLiteral("an"),   QStringLiteral("and"), QStringLiteral("are"),
        QStringLiteral("as"), QStringLiteral("at"),   QStringLiteral("be"),  QStringLiteral("by"),
        QStringLiteral("for"), QStringLiteral("from"), QStringLiteral("in"), QStringLiteral("is"),
        QStringLiteral("it"), QStringLiteral("of"),   QStringLiteral("on"),  QStringLiteral("or"),
        QStringLiteral("that"), QStringLiteral("the"), QStringLiteral("to"), QStringLiteral("was"),
        QStringLiteral("with"),
    };
    return stops;
}

} // namespace

void Retriever::buildFromUnit(const UnitPack &pack, int topicIndex)
{
    m_docs.clear();
    m_df.clear();
    m_avgDocLen = 0.0;
    if (pack.id().isEmpty() || topicIndex < 0) {
        return;
    }
    const QJsonObject clm = pack.document().value(QStringLiteral("clm")).toObject();
    const QJsonArray topics = clm.value(QStringLiteral("topics")).toArray();
    if (topicIndex >= topics.size()) {
        return;
    }
    const QJsonObject topic = topics.at(topicIndex).toObject();
    auto addDoc = [&](const QString &text) {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty()) {
            return;
        }
        Doc doc;
        doc.text = trimmed;
        doc.tokens = tokenize(trimmed);
        doc.length = doc.tokens.size();
        if (doc.length == 0) {
            return;
        }
        m_docs.append(doc);
    };
    addDoc(topic.value(QStringLiteral("summary")).toString());
    for (const QJsonValue &v : topic.value(QStringLiteral("knowledge")).toArray()) {
        addDoc(v.toString());
    }
    for (const QJsonValue &v : topic.value(QStringLiteral("key_terms")).toArray()) {
        addDoc(v.toString());
    }
    for (const QJsonValue &v : topic.value(QStringLiteral("misconceptions")).toArray()) {
        const QJsonObject obj = v.toObject();
        addDoc(obj.value(QStringLiteral("text")).toString());
    }
    int totalLen = 0;
    for (const Doc &doc : m_docs) {
        totalLen += doc.length;
        QSet<QString> seen;
        for (const QString &tok : doc.tokens) {
            if (seen.contains(tok)) {
                continue;
            }
            seen.insert(tok);
            m_df[tok] += 1;
        }
    }
    if (!m_docs.isEmpty()) {
        m_avgDocLen = static_cast<double>(totalLen) / m_docs.size();
    }
}

QStringList Retriever::tokenize(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral("[\\p{L}\\p{N}]+"),
        QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::CaseInsensitiveOption);
    QStringList out;
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QString tok = it.next().captured(0).toLower();
        if (tok.size() < 2 || englishStopwords().contains(tok)) {
            continue;
        }
        out.append(tok);
    }
    return out;
}

bool Retriever::scriptsMismatch(const QString &a, const QString &b)
{
    auto dominantScript = [](const QString &text) -> QChar::Script {
        int counts[QChar::ScriptCount] = {};
        for (const QChar ch : text) {
            if (ch.isLetter()) {
                ++counts[ch.script()];
            }
        }
        int best = 0;
        int bestScript = QChar::Script_Latin;
        for (int i = 0; i < QChar::ScriptCount; ++i) {
            if (counts[i] > best) {
                best = counts[i];
                bestScript = i;
            }
        }
        return static_cast<QChar::Script>(bestScript);
    };
    if (a.isEmpty() || b.isEmpty()) {
        return false;
    }
    return dominantScript(a) != dominantScript(b);
}

QVector<RetrievalHit> Retriever::query(const QString &text, int topK) const
{
    QVector<RetrievalHit> hits;
    if (m_docs.isEmpty()) {
        return hits;
    }
    const QStringList qTokens = tokenize(text);
    if (qTokens.isEmpty()) {
        return hits;
    }
    const int n = m_docs.size();
    const double k1 = 1.2;
    const double b = 0.75;
    for (const Doc &doc : m_docs) {
        if (scriptsMismatch(text, doc.text)) {
            continue;
        }
        double score = 0.0;
        QHash<QString, int> tf;
        for (const QString &tok : doc.tokens) {
            ++tf[tok];
        }
        for (const QString &term : qTokens) {
            const int f = tf.value(term, 0);
            if (f == 0) {
                continue;
            }
            const int df = m_df.value(term, 0);
            const double idf = qLn((n - df + 0.5) / (df + 0.5) + 1.0);
            const double denom = f + k1 * (1.0 - b + b * doc.length / qMax(1.0, m_avgDocLen));
            score += idf * (f * (k1 + 1.0)) / denom;
        }
        if (score > 0.0) {
            hits.append({doc.text, score});
        }
    }
    std::sort(hits.begin(), hits.end(),
              [](const RetrievalHit &a, const RetrievalHit &b) { return a.score > b.score; });
    if (hits.size() > topK) {
        hits.resize(topK);
    }
    return hits;
}

QString Retriever::notesForTurn(const QString &queryText, int maxWords, double minScore) const
{
    const QVector<RetrievalHit> hits = query(queryText, 2);
    QStringList parts;
    int words = 0;
    for (const RetrievalHit &hit : hits) {
        if (hit.score < minScore) {
            continue;
        }
        const QStringList w = hit.text.split(QRegularExpression(QStringLiteral("\\s+")),
                                             Qt::SkipEmptyParts);
        QString clipped;
        for (const QString &token : w) {
            if (words >= maxWords) {
                break;
            }
            if (!clipped.isEmpty()) {
                clipped += QLatin1Char(' ');
            }
            clipped += token;
            ++words;
        }
        if (!clipped.isEmpty()) {
            parts.append(clipped);
        }
        if (words >= maxWords) {
            break;
        }
    }
    return parts.join(QStringLiteral(" "));
}
