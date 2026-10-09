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

void SentenceStreamer::reset(int turnId, EagleMode mode, SessionLanguage language, int wordLimit)
{
    m_turnId = turnId;
    m_mode = mode;
    m_language = language;
    m_wordLimit = wordLimit;
    m_buffer.clear();
    m_holdTag.clear();
    m_sentenceIndex = 0;
    m_emittedWords = 0;
    m_anySentenceEmitted = false;
    m_firstTokenClockMs = -1;
    m_clockMs = 0;
    m_finished = false;
}

void SentenceStreamer::setClockMs(qint64 msSinceT0)
{
    m_clockMs = msSinceT0;
}

void SentenceStreamer::pushToken(const QString &chunk)
{
    if (m_finished || chunk.isEmpty()) {
        return;
    }
    if (m_firstTokenClockMs < 0) {
        m_firstTokenClockMs = m_clockMs;
    }
    m_buffer.append(cleanIncremental(chunk));
    tryEarlyFlush();
    tryEmitSentences(false);
    tryWatchdogFlush();
}

void SentenceStreamer::finishStream()
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    tryEmitSentences(true);
    if (!m_buffer.trimmed().isEmpty()) {
        const int words = countWords(m_buffer);
        if (words >= 2) {
            emit sentenceReady(m_turnId, m_sentenceIndex++, m_buffer.trimmed());
            m_emittedWords += words;
        }
        m_buffer.clear();
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
    if (m_anySentenceEmitted || m_wordLimit <= 0) {
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
        emit sentenceReady(m_turnId, m_sentenceIndex++, processed.text);
    }
}

void SentenceStreamer::tryWatchdogFlush()
{
    if (m_anySentenceEmitted || m_clockMs < 8500) {
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
        emit sentenceReady(m_turnId, m_sentenceIndex++, processed.text);
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

void SentenceStreamer::tryEmitSentences(bool endOfStream)
{
    Q_UNUSED(endOfStream);
    int scan = 0;
    while (scan < m_buffer.size()) {
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
        const int sentenceWords = countWords(sentence);
        if (m_emittedWords + sentenceWords > m_wordLimit) {
            if (m_emittedWords >= static_cast<int>(m_wordLimit * 0.6)) {
                emit wordBudgetReached(m_turnId);
                m_finished = true;
                return;
            }
            emit wordBudgetReached(m_turnId);
            m_finished = true;
            return;
        }
        const auto processed =
            TextPostProcessor::processSentence(sentence, m_mode, m_language);
        if (!processed.dropped && !processed.text.isEmpty()) {
            m_anySentenceEmitted = true;
            m_emittedWords += countWords(processed.text);
            emit sentenceReady(m_turnId, m_sentenceIndex++, processed.text);
        }
    }
}

int SentenceStreamer::countWords(const QString &text) const
{
    return text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
}
