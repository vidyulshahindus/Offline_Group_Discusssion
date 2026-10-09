#include "PromptTypes.h"
#include "TextPostProcessor.h"

#include <QTest>

class TestTextPostProcessor : public QObject
{
    Q_OBJECT

private slots:
    void stripsMarkdownAndUrls();
    void mapsAmpersandEnglish();
    void removesBannedPhrase();
};

void TestTextPostProcessor::stripsMarkdownAndUrls()
{
    const QString raw = QStringLiteral("See **bold** at https://example.com/x now.");
    const QString out = TextPostProcessor::stripForSpeech(raw);
    QVERIFY(!out.contains(QStringLiteral("**")));
    QVERIFY(!out.contains(QStringLiteral("https://")));
    QVERIFY(out.contains(QStringLiteral("bold")));
}

void TestTextPostProcessor::mapsAmpersandEnglish()
{
    const QString out =
        TextPostProcessor::processSentence(QStringLiteral("Salt & water"), EagleMode::Doubt,
                                           SessionLanguage::English)
            .text;
    QVERIFY(out.contains(QStringLiteral("and")));
}

void TestTextPostProcessor::removesBannedPhrase()
{
    const auto result = TextPostProcessor::processSentence(
        QStringLiteral("As an AI language model I cannot."), EagleMode::Doubt,
        SessionLanguage::English);
    QVERIFY(result.dropped);
}

QTEST_MAIN(TestTextPostProcessor)
#include "tst_textpostprocessor.moc"
