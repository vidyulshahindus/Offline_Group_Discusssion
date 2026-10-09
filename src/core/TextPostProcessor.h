#pragma once

#include "PromptTypes.h"

#include <QString>

class TextPostProcessor
{
public:
    struct Result {
        QString text;
        bool dropped = false;
    };

    static QString stripForDisplay(const QString &raw);
    static QString stripForSpeech(const QString &raw);
    static Result processSentence(const QString &sentence,
                                  EagleMode mode,
                                  SessionLanguage language);
};
