#include "TranscriptAssembler.h"

#include <QTest>

class TestTranscriptAssembler : public QObject
{
    Q_OBJECT

private slots:
    void cleansAnnotationsAndSpaces();
    void dropsHallucinations();
    void joinsSegmentsInOrder();
    void tailStartsAtWordBoundary();
    void biasPromptRespectsLimits();
    void biasPromptWithoutTerms();
};

void TestTranscriptAssembler::cleansAnnotationsAndSpaces()
{
    QCOMPARE(TranscriptAssembler::clean(QStringLiteral("  [Music]  The cell\n membrane (upbeat music) ")),
             QStringLiteral("The cell membrane"));
    QCOMPARE(TranscriptAssembler::clean(QStringLiteral(" - Phospholipids form a bilayer.")),
             QStringLiteral("Phospholipids form a bilayer."));
    QCOMPARE(TranscriptAssembler::clean(QStringLiteral("♪ la la ♪")), QString());
    QCOMPARE(TranscriptAssembler::clean(QStringLiteral(" कोशिका  झिल्ली ")), QStringLiteral("कोशिका झिल्ली"));
}

void TestTranscriptAssembler::dropsHallucinations()
{
    TranscriptAssembler t;
    t.setHallucinations({QStringLiteral("Thank you."), QStringLiteral("Thanks for watching!"),
                         QStringLiteral("you"), QStringLiteral("[BLANK_AUDIO]")});
    QVERIFY(!t.appendSegment(QStringLiteral(" Thank you. ")));
    QVERIFY(!t.appendSegment(QStringLiteral("thanks for watching")));
    QVERIFY(!t.appendSegment(QStringLiteral(" you")));
    QVERIFY(!t.appendSegment(QStringLiteral("[BLANK_AUDIO]")));
    QVERIFY(!t.appendSegment(QStringLiteral(" ... ")));
    QVERIFY(t.text().isEmpty());
    // Real speech containing those words is kept.
    QVERIFY(t.appendSegment(QStringLiteral("Thank you for explaining osmosis.")));
    QCOMPARE(t.segmentCount(), 1);
}

void TestTranscriptAssembler::joinsSegmentsInOrder()
{
    TranscriptAssembler t;
    QVERIFY(t.appendSegment(QStringLiteral(" Why does the Delhi Sultanate")));
    QVERIFY(t.appendSegment(QStringLiteral("need legitimacy? ")));
    QCOMPARE(t.text(), QStringLiteral("Why does the Delhi Sultanate need legitimacy?"));
    t.reset();
    QVERIFY(t.text().isEmpty());
}

void TestTranscriptAssembler::tailStartsAtWordBoundary()
{
    TranscriptAssembler t;
    t.appendSegment(QStringLiteral("Alauddin Khilji reformed the market system of Delhi"));
    const QString tail = t.tail(20);
    QVERIFY(tail.size() <= 20);
    QVERIFY(!tail.startsWith(QLatin1Char(' ')));
    QVERIFY(QStringLiteral("Alauddin Khilji reformed the market system of Delhi").endsWith(tail));
    QCOMPARE(tail, QStringLiteral("system of Delhi"));
}

void TestTranscriptAssembler::biasPromptRespectsLimits()
{
    QStringList terms;
    for (int i = 0; i < 40; ++i) {
        terms << QStringLiteral("term%1").arg(i);
    }
    const QString tail = QString(200, QLatin1Char('x')) + QStringLiteral(" previous words here");
    const QString prompt = TranscriptAssembler::biasPrompt(terms, tail);
    const int split = int(prompt.indexOf(QStringLiteral(". ")));
    QVERIFY(split > 0);
    QVERIFY(prompt.left(split).size() <= 150);
    QVERIFY(prompt.mid(split + 2).size() <= 120);
    QVERIFY(prompt.startsWith(QStringLiteral("term0, term1")));
    QVERIFY(prompt.endsWith(QStringLiteral("previous words here")));
}

void TestTranscriptAssembler::biasPromptWithoutTerms()
{
    QCOMPARE(TranscriptAssembler::biasPrompt({}, QStringLiteral("cell membrane")),
             QStringLiteral("cell membrane"));
    QCOMPARE(TranscriptAssembler::biasPrompt({QStringLiteral("phospholipid")}, QString()),
             QStringLiteral("phospholipid."));
    QCOMPARE(TranscriptAssembler::biasPrompt({}, QString()), QString());
}

QTEST_GUILESS_MAIN(TestTranscriptAssembler)
#include "tst_transcriptassembler.moc"
