#pragma once

#include "PromptTypes.h"

#include <QObject>
#include <QString>
#include <QStringList>

class SentenceStreamer : public QObject
{
    Q_OBJECT

public:
    explicit SentenceStreamer(QObject *parent = nullptr);

    void reset(int turnId, EagleMode mode, SessionLanguage language, int wordLimit,
               double overflowRatio = 0.5);
    void setClockMs(qint64 msSinceT0);
    void pushToken(const QString &chunk);
    void finishStream(bool hitTokenLimit = false);

    QString buffer() const { return m_buffer; }
    int emittedWordCount() const { return m_emittedWords; }
    QString spokenText() const;

signals:
    void sentenceReady(int turnId, int sentenceIndex, const QString &text);
    void wordBudgetReached(int turnId);
    void watchdogFlush(int turnId, const QString &text);

private:
    QString cleanIncremental(const QString &chunk);
    void tryEmitSentences();
    void tryEarlyFlush();
    void tryWatchdogFlush();
    void emitSentence(const QString &sentence);
    void stopAfterBudget();
    QString trimRunOnToWordCap(const QString &sentence, int maxWords) const;
    int countWords(const QString &text) const;
    bool isAbbreviationDot(const QString &buffer, int dotIndex) const;

    int m_turnId = -1;
    EagleMode m_mode = EagleMode::Doubt;
    SessionLanguage m_language = SessionLanguage::English;
    int m_wordLimit = 75;
    int m_hardWordCap = 112;
    double m_overflowRatio = 0.5;
    QString m_buffer;
    QString m_holdTag;
    int m_sentenceIndex = 0;
    int m_emittedWords = 0;
    bool m_anySentenceEmitted = false;
    bool m_budgetStopped = false;
    QStringList m_spokenSentences;
    qint64 m_firstTokenClockMs = -1;
    qint64 m_clockMs = 0;
    bool m_finished = false;
};
