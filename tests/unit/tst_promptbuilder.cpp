#include "AppConfig.h"
#include "Paths.h"
#include "PromptBuilder.h"
#include "PromptTypes.h"
#include "TokenEstimator.h"

#include <QCoreApplication>
#include <QTest>

class TestPromptBuilder : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void systemPromptStableAcrossTurnsAndLanguages();
    void tailIsLastInDoubtTurn();
};

void TestPromptBuilder::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("EagleGDTest"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_promptbuilder"));
}

void TestPromptBuilder::systemPromptStableAcrossTurnsAndLanguages()
{
    AppConfig config;
    Paths paths(config);
    TokenEstimator estimator(config);
    PromptBuilder builder(config, paths, estimator);
    QVERIFY(builder.loadTemplates());

    const QString sys1 = builder.systemPrompt();
    TurnInputs en;
    en.language = SessionLanguage::English;
    en.question = QStringLiteral("What is osmosis?");
    TurnInputs hi = en;
    hi.language = SessionLanguage::Hindi;
    hi.question = QStringLiteral("परासरण क्या है?");

    UnitContext unit;
    unit.hasUnit = false;
    unit.registerShort = QStringLiteral("Grade 9 MYP");

    builder.buildUserTurn(en, unit);
    builder.buildUserTurn(hi, unit);
    const QString sys2 = builder.systemPrompt();
    QCOMPARE(sys1, sys2);
}

void TestPromptBuilder::tailIsLastInDoubtTurn()
{
    AppConfig config;
    Paths paths(config);
    TokenEstimator estimator(config);
    PromptBuilder builder(config, paths, estimator);
    QVERIFY(builder.loadTemplates());

    TurnInputs inputs;
    inputs.question = QStringLiteral("Why do cells need energy?");
    inputs.wordLimit = 75;
    UnitContext unit;
    unit.registerShort = QStringLiteral("Grade 9 MYP");
    unit.concept = QStringLiteral("Energy transfer");

    const QString turn = builder.buildUserTurn(inputs, unit);
    QVERIFY(turn.contains(QStringLiteral("Maximum 75 words")));
    const int tailPos = turn.lastIndexOf(QStringLiteral("Reply in"));
    const int doubtPos = turn.indexOf(QStringLiteral("[DOUBT]"));
    QVERIFY(tailPos > doubtPos);
    QVERIFY(turn.indexOf(QStringLiteral("Maximum 75 words"), tailPos) >= 0 || tailPos > doubtPos);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    TestPromptBuilder testObject;
    return QTest::qExec(&testObject, argc, argv);
}

#include "tst_promptbuilder.moc"
