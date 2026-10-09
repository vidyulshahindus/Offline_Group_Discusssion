#pragma once

#include "PromptTypes.h"

#include <QSet>
#include <QString>
#include <QStringList>

// Script detection, the Hinglish heuristic and reply-language choice (spec §10.5).
// Pure logic: no config or Qt Multimedia, unit-tested in tst_languageservice.
enum class TextScript {
    Unknown,
    Latin,
    Devanagari,
    Bengali,
    Gurmukhi,
    Gujarati,
    Tamil,
    Telugu,
    Kannada,
    Malayalam,
    Arabic,
    Other,
};

struct LanguageOptions {
    bool urduScriptAsHindi = true;
    QString devanagariLang = QStringLiteral("hi");       // hi, mr or ne
    QString hinglishReply = QStringLiteral("hindi");     // "hindi" or "english"
    QString noVoiceFallback = QStringLiteral("en");
    QStringList voicedLanguages = {QStringLiteral("en"), QStringLiteral("hi")};
    QStringList hinglishTokens;   // empty -> built-in list
    double hinglishRatio = 0.25;
    int hinglishMinHits = 2;
    int hinglishMinTokens = 3;
};

struct LanguageDecision {
    TextScript script = TextScript::Unknown;
    QString detectedCode;   // language the text is in ("te", "hi", "en", "ur"...)
    QString replyCode;      // language Eagle replies in (always one with a voice)
    bool hinglish = false;
    bool voiceFallback = false;
    QString note;           // short message for the teacher, may be empty
};

class LanguageService
{
public:
    explicit LanguageService(const LanguageOptions &options = {});

    void setOptions(const LanguageOptions &options);
    const LanguageOptions &options() const { return m_options; }

    // Fills hinglish_tokens / thresholds from resources/data/lang/hinglish.json
    // (Qt resource ":/data/lang/hinglish.json"). Returns false if not found.
    static bool loadHinglishResource(LanguageOptions &options, QStringList *hallucinations = nullptr);
    static QStringList builtinHinglishTokens();

    // Dominant script: > 50% of the script letters, otherwise Unknown.
    static TextScript detectScript(const QString &text, double *share = nullptr);
    static QString scriptName(TextScript script);
    bool isHinglish(const QString &text) const;

    // Auto mode: which language is this text in, and which language should Eagle reply in?
    LanguageDecision decide(const QString &text) const;

    // Whisper verbose_json gives full names ("english", "hindi"); map to ISO codes.
    static QString whisperNameToCode(const QString &nameOrCode);
    static QString displayName(const QString &code);
    static SessionLanguage toSessionLanguage(const QString &code);
    static QString codeFor(SessionLanguage language);

private:
    LanguageOptions m_options;
    QSet<QString> m_tokens;
};
