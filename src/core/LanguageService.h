#pragma once

#include "PromptTypes.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

class AppConfig;
class Paths;

enum class DetectedScript {
    Latin,
    Devanagari,
    Telugu,
    Malayalam,
    Tamil,
    Kannada,
    Bengali,
    Gujarati,
    Gurmukhi,
    Arabic,
    Other,
};

class LanguageService
{
public:
    bool load(const AppConfig &config, const Paths &paths);

    DetectedScript detectScript(const QString &text) const;
    QString scriptName(DetectedScript script) const;
    bool isHinglish(const QString &text) const;
    QString filterWhisperHallucination(const QString &text) const;

    SessionLanguage replyLanguageFor(const QString &transcript, SessionLanguage lockedFallback,
                                     QString *noteOut = nullptr) const;
    QString promptLanguageString(const QString &code) const;
    QString voiceIdFor(const QString &code) const;
    QStringList whisperHallucinations() const { return m_hallucinations; }

private:
    QString m_devanagariLang = QStringLiteral("hi");
    bool m_urduScriptAsHindi = true;
    QString m_hinglishReply = QStringLiteral("hindi");
    double m_hinglishRatio = 0.25;
    int m_hinglishMinHits = 3;
    QSet<QString> m_hinglishTokens;
    QStringList m_hallucinations;
    QHash<QString, QString> m_promptLanguage;
    QHash<QString, QString> m_voices;
};
