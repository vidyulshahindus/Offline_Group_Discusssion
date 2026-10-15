#include "Retriever.h"
#include "UnitPack.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

class TestRetriever : public QObject
{
    Q_OBJECT

private slots:
    void ranksRelevantSnippet();
    void skipsScriptMismatch();
};

void TestRetriever::ranksRelevantSnippet()
{
    const UnitPack pack = UnitPack::fromFile(
        QStringLiteral(":/data/units/myp-g9-industrial-revolution.json"));
    Retriever r;
    r.buildFromUnit(pack, 0);
    QVERIFY(r.documentCount() > 0);
    const auto hits = r.query(QStringLiteral("industrial revolution factory workers"), 2);
    QVERIFY(!hits.isEmpty());
    QVERIFY(hits.first().score >= hits.last().score);
}

void TestRetriever::skipsScriptMismatch()
{
    Retriever r;
    UnitPack pack;
    QJsonObject doc;
    QJsonObject clm;
    QJsonArray topics;
    QJsonObject topic;
    topic.insert(QStringLiteral("summary"), QStringLiteral("English only corpus about coal."));
    topics.append(topic);
    clm.insert(QStringLiteral("topics"), topics);
    doc.insert(QStringLiteral("clm"), clm);
    doc.insert(QStringLiteral("id"), QStringLiteral("test"));
    pack = UnitPack(doc);
    r.buildFromUnit(pack, 0);
    const auto hits = r.query(QStringLiteral("कोयला और कारखाना"), 2);
    QCOMPARE(hits.size(), 0);
}

QTEST_MAIN(TestRetriever)
#include "tst_retriever.moc"
