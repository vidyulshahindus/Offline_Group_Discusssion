#include "LanguageService.h"

#include <QTest>

class TestLanguageService : public QObject
{
    Q_OBJECT

private slots:
    void detectsEachScript_data();
    void detectsEachScript();
    void mixedTextWithoutMajorityIsUnknown();
    void hinglishTrue_data();
    void hinglishTrue();
    void hinglishFalse_data();
    void hinglishFalse();
    void hinglishRepliesInHindiOrEnglish();
    void urduScriptAsHindi();
    void urduScriptKeptWhenDisabled();
    void noVoiceFallsBackToEnglish();
    void englishStaysEnglish();
    void devanagariIsHindi();
    void whisperNamesMapToCodes();
    void resourceLexiconLoads();
};

void TestLanguageService::detectsEachScript_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("script");
    QTest::newRow("latin") << QStringLiteral("What is photosynthesis?") << int(TextScript::Latin);
    QTest::newRow("devanagari") << QStringLiteral("प्रकाश संश्लेषण क्या है?") << int(TextScript::Devanagari);
    QTest::newRow("telugu") << QStringLiteral("కిరణజన్య సంయోగక్రియ అంటే ఏమిటి") << int(TextScript::Telugu);
    QTest::newRow("malayalam") << QStringLiteral("പ്രകാശസംശ്ലേഷണം എന്താണ്") << int(TextScript::Malayalam);
    QTest::newRow("tamil") << QStringLiteral("ஒளிச்சேர்க்கை என்றால் என்ன") << int(TextScript::Tamil);
    QTest::newRow("kannada") << QStringLiteral("ದ್ಯುತಿಸಂಶ್ಲೇಷಣೆ ಎಂದರೇನು") << int(TextScript::Kannada);
    QTest::newRow("bengali") << QStringLiteral("সালোকসংশ্লেষণ কী") << int(TextScript::Bengali);
    QTest::newRow("gujarati") << QStringLiteral("પ્રકાશસંશ્લેષણ શું છે") << int(TextScript::Gujarati);
    QTest::newRow("gurmukhi") << QStringLiteral("ਪ੍ਰਕਾਸ਼ ਸੰਸਲੇਸ਼ਣ ਕੀ ਹੈ") << int(TextScript::Gurmukhi);
    QTest::newRow("arabic") << QStringLiteral("فوٹوسنتھیسس کیا ہے") << int(TextScript::Arabic);
    QTest::newRow("empty") << QString() << int(TextScript::Unknown);
    QTest::newRow("digits only") << QStringLiteral("12345 ?!") << int(TextScript::Unknown);
}

void TestLanguageService::detectsEachScript()
{
    QFETCH(QString, text);
    QFETCH(int, script);
    QCOMPARE(int(LanguageService::detectScript(text)), script);
}

void TestLanguageService::mixedTextWithoutMajorityIsUnknown()
{
    // Roughly half Latin, half Devanagari letters: no script has > 50%.
    QCOMPARE(LanguageService::detectScript(QStringLiteral("abcd कखगघ")), TextScript::Unknown);
}

void TestLanguageService::hinglishTrue_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("kya hai") << QStringLiteral("photosynthesis kya hai");
    QTest::newRow("samajh") << QStringLiteral("mujhe yeh samajh nahi aaya");
    QTest::newRow("kaise") << QStringLiteral("Ye process kaise hota hai, batao");
    QTest::newRow("mixed case") << QStringLiteral("Aap mujhe Bahut acchi tarah samjhao");
}

void TestLanguageService::hinglishTrue()
{
    QFETCH(QString, text);
    LanguageService service;
    QVERIFY2(service.isHinglish(text), qPrintable(text));
}

void TestLanguageService::hinglishFalse_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("english") << QStringLiteral("What is the function of the cell membrane?");
    QTest::newRow("two tokens") << QStringLiteral("kya hai");
    QTest::newRow("one hit") << QStringLiteral("I think aap is a political party");
    QTest::newRow("empty") << QString();
}

void TestLanguageService::hinglishFalse()
{
    QFETCH(QString, text);
    LanguageService service;
    QVERIFY2(!service.isHinglish(text), qPrintable(text));
}

void TestLanguageService::hinglishRepliesInHindiOrEnglish()
{
    LanguageService hindi;
    const LanguageDecision d = hindi.decide(QStringLiteral("photosynthesis kya hai"));
    QVERIFY(d.hinglish);
    QCOMPARE(d.replyCode, QStringLiteral("hi"));
    QVERIFY(!d.voiceFallback);

    LanguageOptions options;
    options.hinglishReply = QStringLiteral("english");
    LanguageService english(options);
    QCOMPARE(english.decide(QStringLiteral("photosynthesis kya hai")).replyCode, QStringLiteral("en"));
}

void TestLanguageService::urduScriptAsHindi()
{
    LanguageService service;  // urdu_script_as_hindi defaults to true
    const LanguageDecision d = service.decide(QStringLiteral("فوٹوسنتھیسس کیا ہے"));
    QCOMPARE(d.script, TextScript::Arabic);
    QCOMPARE(d.detectedCode, QStringLiteral("ur"));
    QCOMPARE(d.replyCode, QStringLiteral("hi"));
    QVERIFY(!d.voiceFallback);
}

void TestLanguageService::urduScriptKeptWhenDisabled()
{
    LanguageOptions options;
    options.urduScriptAsHindi = false;
    LanguageService service(options);
    const LanguageDecision d = service.decide(QStringLiteral("فوٹوسنتھیسس کیا ہے"));
    // No Urdu voice installed: reply falls back to English, with a note.
    QCOMPARE(d.replyCode, QStringLiteral("en"));
    QVERIFY(d.voiceFallback);
    QVERIFY(d.note.contains(QStringLiteral("Urdu")));
}

void TestLanguageService::noVoiceFallsBackToEnglish()
{
    LanguageService service;
    const LanguageDecision d = service.decide(QStringLiteral("కిరణజన్య సంయోగక్రియ అంటే ఏమిటి"));
    QCOMPARE(d.detectedCode, QStringLiteral("te"));
    QCOMPARE(d.replyCode, QStringLiteral("en"));
    QVERIFY(d.voiceFallback);
    QCOMPARE(d.note, QStringLiteral("No voice for Telugu installed; replying in English"));

    LanguageOptions withTelugu;
    withTelugu.voicedLanguages << QStringLiteral("te");
    LanguageService teluguVoice(withTelugu);
    QCOMPARE(teluguVoice.decide(QStringLiteral("కిరణజన్య సంయోగక్రియ")).replyCode, QStringLiteral("te"));
}

void TestLanguageService::englishStaysEnglish()
{
    LanguageService service;
    const LanguageDecision d = service.decide(QStringLiteral("Why do cells need a membrane?"));
    QCOMPARE(d.replyCode, QStringLiteral("en"));
    QVERIFY(!d.hinglish);
    QVERIFY(d.note.isEmpty());
}

void TestLanguageService::devanagariIsHindi()
{
    LanguageService service;
    QCOMPARE(service.decide(QStringLiteral("कोशिका झिल्ली का क्या काम है?")).replyCode, QStringLiteral("hi"));
    QCOMPARE(LanguageService::toSessionLanguage(QStringLiteral("hi")), SessionLanguage::Hindi);
    QCOMPARE(LanguageService::toSessionLanguage(QStringLiteral("en")), SessionLanguage::English);
    QCOMPARE(LanguageService::toSessionLanguage(QStringLiteral("te")), SessionLanguage::English);
}

void TestLanguageService::whisperNamesMapToCodes()
{
    QCOMPARE(LanguageService::whisperNameToCode(QStringLiteral("english")), QStringLiteral("en"));
    QCOMPARE(LanguageService::whisperNameToCode(QStringLiteral("Hindi")), QStringLiteral("hi"));
    QCOMPARE(LanguageService::whisperNameToCode(QStringLiteral("urdu")), QStringLiteral("ur"));
    QCOMPARE(LanguageService::whisperNameToCode(QStringLiteral("hi")), QStringLiteral("hi"));
    QCOMPARE(LanguageService::displayName(QStringLiteral("te")), QStringLiteral("Telugu"));
}

void TestLanguageService::resourceLexiconLoads()
{
    LanguageOptions options;
    QStringList hallucinations;
    QVERIFY(LanguageService::loadHinglishResource(options, &hallucinations));
    QVERIFY(options.hinglishTokens.contains(QStringLiteral("kya")));
    QVERIFY(hallucinations.contains(QStringLiteral("Thank you.")));
    LanguageService service(options);
    QVERIFY(service.isHinglish(QStringLiteral("photosynthesis kya hai")));
}

QTEST_GUILESS_MAIN(TestLanguageService)
#include "tst_languageservice.moc"
