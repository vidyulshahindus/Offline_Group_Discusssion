#include "HindiFeminineFixer.h"

#include <QTest>

class TestHindiFeminineFixer : public QObject
{
    Q_OBJECT

private slots:
    void feminineFirstPerson();
    void leavesThirdPerson();
};

void TestHindiFeminineFixer::feminineFirstPerson()
{
    QCOMPARE(HindiFeminineFixer::apply(QStringLiteral("मैं सोचता हूँ")),
             QStringLiteral("मैं सोचती हूँ"));
    QCOMPARE(HindiFeminineFixer::apply(QStringLiteral("मैं कर सकता हूं")),
             QStringLiteral("मैं कर सकती हूं"));
    QCOMPARE(HindiFeminineFixer::apply(QStringLiteral("मैं आपको बताऊँगा")),
             QStringLiteral("मैं आपको बताऊँगी"));
}

void TestHindiFeminineFixer::leavesThirdPerson()
{
    QCOMPARE(HindiFeminineFixer::apply(QStringLiteral("वह जाता है")),
             QStringLiteral("वह जाता है"));
    QCOMPARE(HindiFeminineFixer::apply(QStringLiteral("हम सोचते हैं")),
             QStringLiteral("हम सोचते हैं"));
}

QTEST_MAIN(TestHindiFeminineFixer)
#include "tst_hindifixer.moc"
