#include "PromptTypes.h"
#include "SentenceStreamer.h"

#include <QSignalSpy>
#include <QTest>

class TestSentenceStreamer : public QObject
{
    Q_OBJECT

private slots:
    void emitsOnHindiDanda();
    void holdsThinkingUntilClose();
};

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

QTEST_MAIN(TestSentenceStreamer)
#include "tst_sentencestreamer.moc"
