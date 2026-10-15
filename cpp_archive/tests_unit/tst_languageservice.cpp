#include "LanguageService.h"

#include "AppConfig.h"
#include "Paths.h"

#include <QTest>

class TestLanguageService : public QObject
{
    Q_OBJECT

private slots:
    void devanagariScript();
    void hinglishHeuristic();
    void urduAsHindi();
    void filtersHallucination();
};

void TestLanguageService::devanagariScript()
{
    AppConfig config;
    Paths paths(config);
    LanguageService svc;
    QVERIFY(svc.load(config, paths));
    QCOMPARE(svc.detectScript(QStringLiteral("यह एक परीक्षण है")), DetectedScript::Devanagari);
}

void TestLanguageService::hinglishHeuristic()
{
    AppConfig config;
    Paths paths(config);
    LanguageService svc;
    QVERIFY(svc.load(config, paths));
    QVERIFY(svc.isHinglish(QStringLiteral("kya hai yeh samajh aap")));
    QVERIFY(!svc.isHinglish(QStringLiteral("hello world today")));
    SessionLanguage lang =
        svc.replyLanguageFor(QStringLiteral("mujhe kya samajh aap"), SessionLanguage::English);
    QCOMPARE(lang, SessionLanguage::Hindi);
}

void TestLanguageService::urduAsHindi()
{
    AppConfig config;
    Paths paths(config);
    LanguageService svc;
    QVERIFY(svc.load(config, paths));
    const SessionLanguage lang =
        svc.replyLanguageFor(QString::fromUtf8("یہ ایک ٹیسٹ ہے"), SessionLanguage::English);
    QCOMPARE(lang, SessionLanguage::Hindi);
}

void TestLanguageService::filtersHallucination()
{
    AppConfig config;
    Paths paths(config);
    LanguageService svc;
    QVERIFY(svc.load(config, paths));
    QVERIFY(svc.filterWhisperHallucination(QStringLiteral("Thank you.")).isEmpty());
    QCOMPARE(svc.filterWhisperHallucination(QStringLiteral("Hello class")), QStringLiteral("Hello class"));
}

QTEST_MAIN(TestLanguageService)
#include "tst_languageservice.moc"
