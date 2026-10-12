#include "SentenceStreamer.h"

#include "TextPostProcessor.h"

#include <QRegularExpression>

namespace {

const QStringList kTerminalPunct = {
    QStringLiteral("."), QStringLiteral("!"), QStringLiteral("?"), QStringLiteral("।"),
    QStringLiteral("॥"), QStringLiteral("۔"), QStringLiteral("。"), QStringLiteral("！"),
    QStringLiteral("？"),
};

bool endsWithTerminal(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    for (const QString &p : kTerminalPunct) {
        if (trimmed.endsWith(p)) {
            return true;
        }
    }
    return false;
}

} // namespace

SentenceStreamer::SentenceStreamer(QObject *parent)
    : QObject(parent)
{
}

void SentenceStreamer::reset(int turnId, EagleMode mode, SessionLanguage language, int wordLimit,
                             double overflowRatio)
{
    m_turnId = turnId;
    m_mode = mode;
    m_language = language;
    m_wordLimit = wordLimit;
    m_overflowRatio = overflowRatio;
    m_hardWordCap = static_cast<int>(wordLimit * (1.0 + overflowRatio));
    m_buffer.clear();
    m_holdTag.clear();
    m_sentenceIndex = 0;
    m_emittedWords = 0;
    m_anySentenceEmitted = false;
    m_budgetStopped = false;
    m_spokenSentences.clear();
    m_firstTokenClockMs = -1;
    m_clockMs = 0;
    m_finished = false;
}

QString SentenceStreamer::spokenText() const
{
    return m_spokenSentences.join(QStringLiteral(" "));
}

void SentenceStreamer::setClockMs(qint64 msSinceT0)
{
    m_clockMs = msSinceT0;
}

void SentenceStreamer::pushToken(const QString &chunk)
{
    if (m_finished || m_budgetStopped || chunk.isEmpty()) {
        return;
    }
    if (m_firstTokenClockMs < 0) {
        m_firstTokenClockMs = m_clockMs;
    }
    m_buffer.append(cleanIncremental(chunk));
    tryEarlyFlush();
    tryEmitSentences();
    tryWatchdogFlush();
}

void SentenceStreamer::finishStream(bool hitTokenLimit)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    tryEmitSentences();
    const QString tail = m_buffer.trimmed();
    m_buffer.clear();
    if (tail.isEmpty() || m_budgetStopped) {
        return;
    }
    if (hitTokenLimit && !endsWithTerminal(tail)) {
        return;
    }
    const int words = countWords(tail);
    if (words >= 2 && m_emittedWords < m_wordLimit) {
        emitSentence(tail);
        if (m_emittedWords >= m_wordLimit) {
            stopAfterBudget();
        }
    }
}

QString SentenceStreamer::cleanIncremental(const QString &chunk)
{
    QString text = chunk;
    static const QRegularExpression eaglePrefix(QStringLiteral("^\\s*Eagle\\s*[:：]\\s*"),
                                                QRegularExpression::CaseInsensitiveOption);
    if (m_buffer.isEmpty() && m_holdTag.isEmpty()) {
        text.replace(eaglePrefix, QString());
    }
    QString combined = m_holdTag + text;
    m_holdTag.clear();
    const QString thinkOpen = QStringLiteral("<think>");
    const QString thinkClose = QStringLiteral("</think>");
    const QString toolOpen = QStringLiteral("<tool_code>");
    const QString toolClose = QStringLiteral("</tool_code>");

    auto stripSpan = [&](const QString &open, const QString &close) {
        int start = 0;
        while ((start = combined.indexOf(open, start, Qt::CaseInsensitive)) != -1) {
            const int end = combined.indexOf(close, start, Qt::CaseInsensitive);
            if (end == -1) {
                m_holdTag = combined.mid(start);
                combined = combined.left(start);
                return;
            }
            combined.remove(start, end + close.size() - start);
            start = 0;
        }
    };
    stripSpan(thinkOpen, thinkClose);
    stripSpan(toolOpen, toolClose);
    return combined;
}

void SentenceStreamer::tryEarlyFlush()
{
    if (m_anySentenceEmitted || m_wordLimit <= 0 || m_budgetStopped) {
        return;
    }
    if (m_firstTokenClockMs < 0 || m_clockMs - m_firstTokenClockMs < 1200) {
        return;
    }
    if (countWords(m_buffer) < 10) {
        return;
    }
    static const QRegularExpression clause(
        QStringLiteral("^(.{12,}?)([,;:—–])\\s+"), QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = clause.match(m_buffer);
    if (!match.hasMatch() || match.captured(1).split(QRegularExpression(QStringLiteral("\\s+")),
                                                      Qt::SkipEmptyParts)
                                    .size()
                                < 6) {
        return;
    }
    const QString chunk = (match.captured(1) + match.captured(2)).trimmed();
    m_buffer = m_buffer.mid(match.capturedEnd());
    const auto processed =
        TextPostProcessor::processSentence(chunk, m_mode, m_language);
    if (!processed.dropped && !processed.text.isEmpty()) {
        m_anySentenceEmitted = true;
        m_emittedWords += countWords(processed.text);
        m_spokenSentences.append(processed.text);
        emit sentenceReady(m_turnId, m_sentenceIndex++, processed.text);
        if (m_emittedWords >= m_wordLimit) {
            stopAfterBudget();
        }
    }
}

void SentenceStreamer::tryWatchdogFlush()
{
    if (m_anySentenceEmitted || m_clockMs < 8500 || m_budgetStopped) {
        return;
    }
    if (countWords(m_buffer) < 6) {
        return;
    }
    const QString chunk = m_buffer.trimmed();
    m_buffer.clear();
    m_anySentenceEmitted = true;
    emit watchdogFlush(m_turnId, chunk);
    const auto processed = TextPostProcessor::processSentence(chunk, m_mode, m_language);
    if (!processed.dropped && !processed.text.isEmpty()) {
        m_emittedWords += countWords(processed.text);
        m_spokenSentences.append(processed.text);
        emit sentenceReady(m_turnId, m_sentenceIndex++, processed.text);
        if (m_emittedWords >= m_wordLimit) {
            stopAfterBudget();
        }
    }
}

bool SentenceStreamer::isAbbreviationDot(const QString &buffer, int dotIndex) const
{
    static const QStringList abbrevs = {
        QStringLiteral("Dr"), QStringLiteral("Mr"), QStringLiteral("Mrs"), QStringLiteral("Ms"),
        QStringLiteral("St"),  QStringLiteral("vs"), QStringLiteral("etc"), QStringLiteral("e.g"),
        QStringLiteral("i.e"), QStringLiteral("No"), QStringLiteral("Fig"), QStringLiteral("approx"),
    };
    const int start = buffer.lastIndexOf(QRegularExpression(QStringLiteral("\\s+")), dotIndex);
    const QString word = buffer.mid(start + 1, dotIndex - start - 1).trimmed();
    for (const QString &abbr : abbrevs) {
        if (word.compare(abbr, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    if (dotIndex >= 2 && buffer.at(dotIndex - 1).isLetter() && buffer.at(dotIndex - 2) == QLatin1Char(' ')
        && dotIndex + 1 < buffer.size() && buffer.at(dotIndex + 1).isSpace()) {
        return true;
    }
    return false;
}

void SentenceStreamer::emitSentence(const QString &sentence)
{
    const auto processed = TextPostProcessor::processSentence(sentence, m_mode, m_language);
    if (processed.dropped || processed.text.isEmpty()) {
        return;
    }
    m_anySentenceEmitted = true;
    m_emittedWords += countWords(processed.text);
    m_spokenSentences.append(processed.text);
    emit sentenceReady(m_turnId, m_sentenceIndex++, processed.text);
}

void SentenceStreamer::stopAfterBudget()
{
    if (m_budgetStopped) {
        return;
    }
    m_budgetStopped = true;
    m_buffer.clear();
    m_finished = true;
    emit wordBudgetReached(m_turnId);
}

QString SentenceStreamer::trimRunOnToWordCap(const QString &sentence, int maxWords) const
{
    if (maxWords <= 0) {
        return {};
    }
    const QStringList words =
        sentence.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (words.size() <= maxWords) {
        return sentence.trimmed();
    }
    QString subset = words.mid(0, maxWords).join(QLatin1Char(' '));
    static const QRegularExpression clauseBreak(QStringLiteral("[,;:\u2013\u2014\u2012\u2014]"));
    int bestBreak = -1;
    QRegularExpressionMatchIterator it = clauseBreak.globalMatch(subset);
    while (it.hasNext()) {
        bestBreak = it.next().capturedStart();
    }
    QString trimmed;
    if (bestBreak >= 0) {
        trimmed = subset.left(bestBreak + 1).trimmed();
    } else {
        trimmed = subset.trimmed();
    }
    if (trimmed.isEmpty()) {
        return {};
    }
    if (!endsWithTerminal(trimmed)) {
        trimmed.append(m_language == SessionLanguage::Hindi ? QStringLiteral("।")
                                                            : QStringLiteral("."));
    }
    return trimmed;
}

void SentenceStreamer::tryEmitSentences()
{
    if (m_budgetStopped) {
        m_buffer.clear();
        return;
    }
    int scan = 0;
    while (scan < m_buffer.size()) {
        if (m_emittedWords >= m_wordLimit) {
            m_buffer.clear();
            return;
        }
        int boundary = -1;
        QString punct;
        for (const QString &p : kTerminalPunct) {
            const int idx = m_buffer.indexOf(p, scan);
            if (idx >= 0 && (boundary < 0 || idx < boundary)) {
                boundary = idx;
                punct = p;
            }
        }
        if (boundary < 0) {
            break;
        }
        if (punct == QStringLiteral(".") && isAbbreviationDot(m_buffer, boundary)) {
            scan = boundary + 1;
            continue;
        }
        int after = boundary + punct.size();
        if (after < m_buffer.size() && !m_buffer.at(after).isSpace() && m_buffer.at(after) != QLatin1Char('\n')) {
            scan = boundary + 1;
            continue;
        }
        QString sentence = m_buffer.left(after).trimmed();
        m_buffer = m_buffer.mid(after).trimmed();
        scan = 0;
        if (sentence.isEmpty()) {
            continue;
        }
        if (countWords(sentence) < 3 && !m_buffer.isEmpty()) {
            m_buffer = sentence + QLatin1Char(' ') + m_buffer;
            break;
        }

        const int roomToCap = m_hardWordCap - m_emittedWords;
        const int sentenceWords = countWords(sentence);
        if (sentenceWords > roomToCap) {
            sentence = trimRunOnToWordCap(sentence, roomToCap);
            if (sentence.isEmpty()) {
                stopAfterBudget();
                return;
            }
        }

        emitSentence(sentence);
        if (m_emittedWords >= m_wordLimit) {
            stopAfterBudget();
            return;
        }
    }
}

int SentenceStreamer::countWords(const QString &text) const
{
    return text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
}
