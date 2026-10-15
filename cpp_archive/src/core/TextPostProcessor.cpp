#include "TextPostProcessor.h"

#include "HindiFeminineFixer.h"

#include <QRegularExpression>

namespace {

QString stripTaggedBlocks(QString text)
{
    static const QRegularExpression thinkRe(
        QStringLiteral("<\\s*redacted_thinking\\s*>.*?</\\s*redacted_thinking\\s*>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression toolRe(
        QStringLiteral("<\\s*tool_code\\s*>.*?</\\s*tool_code\\s*>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    text.remove(thinkRe);
    text.remove(toolRe);
    return text;
}

QString stripUrlsAndMarkdown(QString text)
{
    static const QRegularExpression urlRe(QStringLiteral("https?://\\S+"));
    text.remove(urlRe);
    static const QRegularExpression mdLink(QStringLiteral("\\[([^\\]]+)\\]\\([^)]+\\)"));
    text.replace(mdLink, QStringLiteral("\\1"));
    text.replace(QStringLiteral("**"), QString());
    text.replace(QStringLiteral("__"), QString());
    static const QRegularExpression backticks(QStringLiteral("`+"));
    text.remove(backticks);
    static const QRegularExpression heading(QStringLiteral("^#+\\s*"), QRegularExpression::MultilineOption);
    text.remove(heading);
    return text;
}

QString mapSymbols(const QString &text, SessionLanguage language)
{
    QString out = text;
    const bool hindi = language == SessionLanguage::Hindi;
    out.replace(QLatin1Char('&'), hindi ? QStringLiteral("और") : QStringLiteral("and"));
    out.replace(QLatin1Char('%'), hindi ? QStringLiteral("प्रतिशत") : QStringLiteral("percent"));
    return out;
}

QString removeRoleLabel(QString text)
{
    static const QRegularExpression roleRe(
        QStringLiteral("^(?:Eagle|Student\\s*[A-Z0-9]+|Teacher|Participant\\s*\\d+)\\s*[:：\\-–]\\s*"),
        QRegularExpression::CaseInsensitiveOption);
    return text.replace(roleRe, QString());
}

bool isBannedPhrase(const QString &sentence)
{
    const QString lower = sentence.toLower();
    static const QStringList banned = {
        QStringLiteral("as an ai language model"),
        QStringLiteral("as of my last update"),
        QStringLiteral("my knowledge cutoff"),
        QStringLiteral("i'm just a robot"),
        QStringLiteral("what else would you like to discuss"),
        QStringLiteral("how can i help you today"),
    };
    for (const QString &phrase : banned) {
        if (lower.contains(phrase)) {
            return true;
        }
    }
    return false;
}

QString normalizeWhitespace(QString text)
{
    text = text.simplified();
    return text.trimmed();
}

} // namespace

QString TextPostProcessor::stripForDisplay(const QString &raw)
{
    QString text = stripTaggedBlocks(raw);
    QStringList kept;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("web_search("), Qt::CaseInsensitive)
            || trimmed.startsWith(QStringLiteral("print(web_search"), Qt::CaseInsensitive)) {
            continue;
        }
        kept.append(line);
    }
    return normalizeWhitespace(stripUrlsAndMarkdown(kept.join(QLatin1Char('\n'))));
}

QString TextPostProcessor::stripForSpeech(const QString &raw)
{
    return stripForDisplay(raw);
}

TextPostProcessor::Result TextPostProcessor::processSentence(const QString &sentence,
                                                             EagleMode mode,
                                                             SessionLanguage language)
{
    Result result;
    QString text = stripForDisplay(sentence);
    text = removeRoleLabel(text);
    text = mapSymbols(text, language);
    static const QRegularExpression listMarker(
        QStringLiteral("^(?:[-*•]|\\d+[.)])\\s+"), QRegularExpression::MultilineOption);
    text.remove(listMarker);
    text = normalizeWhitespace(text);
    if (text.isEmpty()) {
        result.dropped = true;
        return result;
    }
    if (isBannedPhrase(text)) {
        result.dropped = true;
        return result;
    }
    if (language == SessionLanguage::Hindi) {
        text = HindiFeminineFixer::apply(text);
    }
    if (!text.isEmpty() && !text.endsWith(QLatin1Char('.')) && !text.endsWith(QLatin1Char('?'))
        && !text.endsWith(QChar(0x0964))) {
        text.append(language == SessionLanguage::Hindi ? QChar(0x0964) : QLatin1Char('.'));
    }
    result.text = text;
    return result;
}
