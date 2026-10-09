#include "TranscriptAssembler.h"

#include <QRegularExpression>

void TranscriptAssembler::setHallucinations(const QStringList &phrases)
{
    m_hallucinationKeys.clear();
    for (const QString &p : phrases) {
        const QString key = normalisedKey(p);
        if (!key.isEmpty()) {
            m_hallucinationKeys.append(key);
        }
    }
}

void TranscriptAssembler::reset()
{
    m_text.clear();
    m_segments = 0;
}

QString TranscriptAssembler::clean(const QString &raw)
{
    static const QRegularExpression annotations(
        QStringLiteral("\\[[^\\]]*\\]|\\([^)]*\\)|\\*[^*]*\\*|♪[^♪]*♪|♪"),
        QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression spaces(QStringLiteral("\\s+"),
                                           QRegularExpression::UseUnicodePropertiesOption);
    QString text = raw;
    text.replace(annotations, QStringLiteral(" "));
    text.replace(spaces, QStringLiteral(" "));
    text = text.trimmed();
    // Whisper sometimes starts a segment with a dash or an ellipsis.
    while (text.startsWith(QLatin1Char('-')) || text.startsWith(QStringLiteral("..."))) {
        text = text.mid(text.startsWith(QLatin1Char('-')) ? 1 : 3).trimmed();
    }
    return text;
}

QString TranscriptAssembler::normalisedKey(const QString &text)
{
    static const QRegularExpression punct(QStringLiteral("[\\p{P}\\p{S}\\s]+"),
                                          QRegularExpression::UseUnicodePropertiesOption);
    QString key = text.toLower();
    key.replace(punct, QStringLiteral(" "));
    return key.simplified();
}

bool TranscriptAssembler::isHallucination(const QString &cleaned) const
{
    const QString key = normalisedKey(cleaned);
    if (key.isEmpty()) {
        return true;  // only punctuation / annotations
    }
    return m_hallucinationKeys.contains(key);
}

bool TranscriptAssembler::appendSegment(const QString &raw)
{
    const QString cleaned = clean(raw);
    if (cleaned.isEmpty() || isHallucination(cleaned)) {
        return false;
    }
    if (!m_text.isEmpty()) {
        m_text.append(QLatin1Char(' '));
    }
    m_text.append(cleaned);
    ++m_segments;
    return true;
}

QString TranscriptAssembler::tail(int maxChars) const
{
    if (maxChars <= 0 || m_text.isEmpty()) {
        return {};
    }
    if (m_text.size() <= maxChars) {
        return m_text;
    }
    QString t = m_text.right(maxChars);
    const int space = int(t.indexOf(QLatin1Char(' ')));
    if (space >= 0 && space + 1 < t.size()) {
        t = t.mid(space + 1);
    }
    return t;
}

QString TranscriptAssembler::biasPrompt(const QStringList &terms, const QString &previousTail,
                                        int termsMaxChars, int tailMaxChars)
{
    QString termText;
    for (const QString &raw : terms) {
        const QString term = raw.simplified();
        if (term.isEmpty()) {
            continue;
        }
        const int extra = int(termText.isEmpty() ? term.size() : term.size() + 2);
        if (termText.size() + extra > termsMaxChars) {
            break;
        }
        if (!termText.isEmpty()) {
            termText.append(QStringLiteral(", "));
        }
        termText.append(term);
    }

    QString tail = previousTail.simplified();
    if (tail.size() > tailMaxChars) {
        tail = tail.right(tailMaxChars);
        const int space = int(tail.indexOf(QLatin1Char(' ')));
        if (space >= 0 && space + 1 < tail.size()) {
            tail = tail.mid(space + 1);
        }
    }

    if (termText.isEmpty()) {
        return tail;
    }
    if (tail.isEmpty()) {
        return termText + QLatin1Char('.');
    }
    return termText + QStringLiteral(". ") + tail;
}
