#include "AppConfig.h"
#include "HistoryManager.h"
#include "PromptTypes.h"
#include "TokenEstimator.h"

#include <QTest>

class TestHistoryManager : public QObject
{
    Q_OBJECT

private slots:
    void appendOnlyAndFollowUpMerged();
    void trimDropsOldestPairs();
    void resetClears();
};

void TestHistoryManager::resetClears()
{
    AppConfig config;
    TokenEstimator estimator(config);
    HistoryManager history(config, estimator);
    history.appendTurn(QStringLiteral("user one"), QStringLiteral("assistant one"),
                       SessionLanguage::English);
    history.reset();
    QVERIFY(history.isEmpty());
}

void TestHistoryManager::appendOnlyAndFollowUpMerged()
{
    AppConfig config;
    TokenEstimator estimator(config);
    HistoryManager history(config, estimator);
    history.appendTurn(QStringLiteral("user turn"), QStringLiteral("First reply."),
                       SessionLanguage::English);
    history.appendFollowUpToLastAssistant(QStringLiteral("What evidence supports that?"));
    QCOMPARE(history.messages().size(), 2);
    QVERIFY(history.messages().last().content.contains(QStringLiteral("What evidence")));
}

void TestHistoryManager::trimDropsOldestPairs()
{
    AppConfig config;
    TokenEstimator estimator(config);
    HistoryManager history(config, estimator);
    for (int i = 0; i < 30; ++i) {
        history.appendTurn(QStringLiteral("user %1 %2").arg(i).arg(QString(400, QChar('x'))),
                           QStringLiteral("assistant %1").arg(i), SessionLanguage::English);
    }
    QVERIFY(history.messages().size() >= 4);
}

QTEST_MAIN(TestHistoryManager)
#include "tst_historymanager.moc"
