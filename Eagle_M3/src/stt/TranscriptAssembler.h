#pragma once

#include <QString>
#include <QStringList>

// Joins per-segment whisper results into the live transcript, drops known
// whisper hallucinations, and builds the bias prompt (spec §10.1). Pure logic.
class TranscriptAssembler
{
public:
    void setHallucinations(const QStringList &phrases);
    void reset();

    // Whitespace-normalised text with [..], (..) and ♪..♪ annotations removed.
    static QString clean(const QString &raw);
    bool isHallucination(const QString &cleaned) const;

    // Cleans and appends; returns false if the segment was dropped.
    bool appendSegment(const QString &raw);
    QString text() const { return m_text; }
    int segmentCount() const { return m_segments; }
    // Last <= maxChars of the transcript, starting at a word boundary.
    QString tail(int maxChars) const;

    static QString biasPrompt(const QStringList &terms, const QString &previousTail,
                              int termsMaxChars = 150, int tailMaxChars = 120);

private:
    static QString normalisedKey(const QString &text);

    QStringList m_hallucinationKeys;
    QString m_text;
    int m_segments = 0;
};
