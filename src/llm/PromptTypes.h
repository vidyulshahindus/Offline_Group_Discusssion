#pragma once

#include <QString>

enum class EagleMode {
    Doubt,
    Practice,
};

enum class SessionLanguage {
    English,
    Hindi,
};

struct UnitContext {
    bool hasUnit = false;
    QString programme = QStringLiteral("MYP");
    QString subject = QStringLiteral("General");
    QString grade = QStringLiteral("9");
    QString registerText;
    QString registerShort = QStringLiteral("Grade 9 MYP");
    QString unitTitle;
    QString topicTitle;
    QString concept = QStringLiteral("Learning and growth");
    QString conceptMeaning;
    QString relevance;
    QString wisdomQuestion;
    QString ibLinks;
    QString keyFacts;
};

struct TurnInputs {
    EagleMode mode = EagleMode::Doubt;
    SessionLanguage language = SessionLanguage::English;
    int wordLimit = 75;
    QString question;
    QString notes;
    QString misconception;
};
