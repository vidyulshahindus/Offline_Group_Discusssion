#include "PromptTypes.h"
#include "SentenceStreamer.h"

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

class TestSentenceStreamer : public QObject
{
    Q_OBJECT

private slots:
    void emitsOnHindiDanda();
    void holdsThinkingUntilClose();
    void speaks37Then42Words();
    void speaks70Then30NotThird();
    void speaks40And35Exactly75();
    void trimsRunOnAtClauseBreak();
    void hindiRunOnEndsWithDanda();
    void dropsUnfinishedFragmentOnTokenLimit();
};

namespace {

QString wordBlock(int count)
{
    QStringList words;
    for (int i = 0; i < count; ++i) {
        words.append(QStringLiteral("w%1").arg(i + 1));
    }
    return words.join(QLatin1Char(' '));
}

QString sentenceOf(int wordCount)
{
    return wordBlock(wordCount) + QStringLiteral(".");
}

QString runOnWithCommas(int wordCount, int commaEvery = 10)
{
    QStringList words;
    for (int i = 0; i < wordCount; ++i) {
        QString w = QStringLiteral("w%1").arg(i + 1);
        if (commaEvery > 0 && (i + 1) % commaEvery == 0 && i + 1 < wordCount) {
            w += QLatin1Char(',');
        }
        words.append(w);
    }
    return words.join(QLatin1Char(' ')) + QStringLiteral(".");
}

int countWords(const QString &text)
{
    return text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
}

} // namespace

void TestSentenceStreamer::emitsOnHindiDanda()
{
    SentenceStreamer streamer;
    QSignalSpy spy(&streamer, &SentenceStreamer::sentenceReady);
    streamer.reset(1, EagleMode::Doubt, SessionLanguage::Hindi, 75);
    streamer.setClockMs(5000);
    streamer.pushToken(QStringLiteral("यह एक वाक्य है।"));
    streamer.finishStream();
    QCOMPARE(spy.count(), 1);
}

void TestSentenceStreamer::holdsThinkingUntilClose()
{
    SentenceStreamer streamer;
    QSignalSpy spy(&streamer, &SentenceStreamer::sentenceReady);
    streamer.reset(2, EagleMode::Doubt, SessionLanguage::English, 75);
    streamer.pushToken(QStringLiteral("<think>secret"));
    QCOMPARE(spy.count(), 0);
    streamer.pushToken(QStringLiteral("</think> Hello."));
    streamer.finishStream();
    QVERIFY(spy.count() >= 1);
}

void TestSentenceStreamer::speaks37Then42Words()
{
    SentenceStreamer streamer;
    QSignalSpy ready(&streamer, &SentenceStreamer::sentenceReady);
    QSignalSpy budget(&streamer, &SentenceStreamer::wordBudgetReached);
    streamer.reset(1, EagleMode::Doubt, SessionLanguage::English, 75, 0.5);
    streamer.pushToken(sentenceOf(37));
    streamer.pushToken(QStringLiteral(" ") + sentenceOf(42));
    streamer.finishStream();
    QCOMPARE(ready.count(), 2);
    QCOMPARE(budget.count(), 1);
    QCOMPARE(streamer.emittedWordCount(), 79);
}

void TestSentenceStreamer::speaks70Then30NotThird()
{
    SentenceStreamer streamer;
    QSignalSpy ready(&streamer, &SentenceStreamer::sentenceReady);
    streamer.reset(1, EagleMode::Doubt, SessionLanguage::English, 75, 0.5);
    streamer.pushToken(sentenceOf(70));
    streamer.pushToken(QStringLiteral(" ") + sentenceOf(30));
    streamer.pushToken(QStringLiteral(" ") + sentenceOf(12));
    streamer.finishStream();
    QCOMPARE(ready.count(), 2);
    QCOMPARE(streamer.emittedWordCount(), 100);
}

void TestSentenceStreamer::speaks40And35Exactly75()
{
    SentenceStreamer streamer;
    QSignalSpy ready(&streamer, &SentenceStreamer::sentenceReady);
    streamer.reset(1, EagleMode::Doubt, SessionLanguage::English, 75, 0.5);
    streamer.pushToken(sentenceOf(40));
    streamer.pushToken(QStringLiteral(" ") + sentenceOf(35));
    streamer.pushToken(QStringLiteral(" ") + sentenceOf(8));
    streamer.finishStream();
    QCOMPARE(ready.count(), 2);
    QCOMPARE(streamer.emittedWordCount(), 75);
}

void TestSentenceStreamer::trimsRunOnAtClauseBreak()
{
    SentenceStreamer streamer;
    QSignalSpy ready(&streamer, &SentenceStreamer::sentenceReady);
    streamer.reset(1, EagleMode::Doubt, SessionLanguage::English, 75, 0.5);
    streamer.pushToken(sentenceOf(37) + QLatin1Char(' ') + runOnWithCommas(100));
    streamer.finishStream();
    QCOMPARE(ready.count(), 2);
    const QString second = ready.at(1).at(2).toString();
    QVERIFY(second.endsWith(QLatin1Char('.')));
    QVERIFY(streamer.emittedWordCount() <= 112);
    QVERIFY(countWords(second) <= 75);
}

void TestSentenceStreamer::hindiRunOnEndsWithDanda()
{
    SentenceStreamer streamer;
    QSignalSpy ready(&streamer, &SentenceStreamer::sentenceReady);
    streamer.reset(1, EagleMode::Doubt, SessionLanguage::Hindi, 75, 0.5);
    streamer.pushToken(sentenceOf(37) + QLatin1Char(' ') + runOnWithCommas(100));
    streamer.finishStream();
    QCOMPARE(ready.count(), 2);
    const QString second = ready.at(1).at(2).toString();
    QVERIFY(second.endsWith(QStringLiteral("।")));
}

void TestSentenceStreamer::dropsUnfinishedFragmentOnTokenLimit()
{
    SentenceStreamer streamer;
    QSignalSpy ready(&streamer, &SentenceStreamer::sentenceReady);
    streamer.reset(1, EagleMode::Doubt, SessionLanguage::English, 75, 0.5);
    streamer.pushToken(QStringLiteral("This fragment has no ending"));
    streamer.finishStream(true);
    QCOMPARE(ready.count(), 0);
}

QTEST_MAIN(TestSentenceStreamer)
#include "tst_sentencestreamer.moc"
