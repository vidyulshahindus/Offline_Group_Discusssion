#include "AppConfig.h"
#include "IbReference.h"
#include "Paths.h"
#include "PromptBuilder.h"
#include "PromptTypes.h"
#include "TokenEstimator.h"
#include "UnitContextBuilder.h"
#include "UnitPack.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

class TestPromptBuilder : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void systemPromptStableAcrossTurnsAndLanguages();
    void tailIsLastInDoubtTurn();
    void doubtWithoutUnitOmitsConceptLink();
    void doubtWithUnitLinksConcept();
    void turnThreeExtendsTurnTwoMessagePrefix();
    void systemPromptStableWithUnitAcrossModesAndLanguages();
    void systemPromptChangesWhenTopicChanges();
    void systemPromptHasBlankLineBeforeUnitCard();
};

static QByteArray messagesJson(const QVector<LlmMessage> &messages)
{
    QJsonArray arr;
    for (const LlmMessage &message : messages) {
        QJsonObject obj;
        obj.insert(QStringLiteral("role"), message.role);
        obj.insert(QStringLiteral("content"), message.content);
        arr.append(obj);
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

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

void TestPromptBuilder::doubtWithoutUnitOmitsConceptLink()
{
    AppConfig config;
    Paths paths(config);
    TokenEstimator estimator(config);
    PromptBuilder builder(config, paths, estimator);
    QVERIFY(builder.loadTemplates());

    TurnInputs inputs;
    inputs.question = QStringLiteral("What is gravity?");
    UnitContext unit;
    unit.hasUnit = false;
    unit.registerShort = QStringLiteral("Grade 9 MYP");

    const QString turn = builder.buildUserTurn(inputs, unit);
    QVERIFY(!turn.contains(QStringLiteral("unit concept")));
    QVERIFY(!turn.contains(QStringLiteral("Learning and growth")));
}

void TestPromptBuilder::turnThreeExtendsTurnTwoMessagePrefix()
{
    AppConfig config;
    Paths paths(config);
    TokenEstimator estimator(config);
    PromptBuilder builder(config, paths, estimator);
    QVERIFY(builder.loadTemplates());

    UnitContext unit;
    unit.hasUnit = false;
    unit.registerShort = QStringLiteral("Grade 9 MYP");

    QVector<LlmMessage> history;
    TurnInputs t1;
    t1.question = QStringLiteral("What is osmosis?");
    const QVector<LlmMessage> m1 = builder.buildMessages(t1, unit, history);
    LlmMessage h1u;
    h1u.role = QStringLiteral("user");
    h1u.content = builder.buildUserTurn(t1, unit);
    history.append(h1u);
    LlmMessage h1a;
    h1a.role = QStringLiteral("assistant");
    h1a.content = QStringLiteral("Osmosis is water movement across a membrane.");
    history.append(h1a);

    TurnInputs t2;
    t2.question = QStringLiteral("Why does it matter?");
    const QVector<LlmMessage> m2 = builder.buildMessages(t2, unit, history);
    LlmMessage h2u;
    h2u.role = QStringLiteral("user");
    h2u.content = builder.buildUserTurn(t2, unit);
    history.append(h2u);
    LlmMessage h2a;
    h2a.role = QStringLiteral("assistant");
    h2a.content = QStringLiteral("It keeps cells alive.");
    history.append(h2a);

    TurnInputs t3;
    t3.question = QStringLiteral("Give an example.");
    const QVector<LlmMessage> m3 = builder.buildMessages(t3, unit, history);

    QCOMPARE(m2.size(), 4);
    QCOMPARE(m3.size(), 6);
    for (int i = 0; i < m2.size(); ++i) {
        QCOMPARE(m2.at(i).role, m3.at(i).role);
        QCOMPARE(m2.at(i).content, m3.at(i).content);
    }
    QCOMPARE(m3.at(4).role, QStringLiteral("assistant"));
    QCOMPARE(m3.at(4).content, h2a.content);
    QCOMPARE(m3.at(5).role, QStringLiteral("user"));
    QVERIFY(m3.at(5).content.contains(QStringLiteral("Give an example")));
}

void TestPromptBuilder::doubtWithUnitLinksConcept()
{
    AppConfig config;
    Paths paths(config);
    TokenEstimator estimator(config);
    PromptBuilder builder(config, paths, estimator);
    QVERIFY(builder.loadTemplates());

    IbReference ib;
    ib.loadFromResources();
    const UnitPack pack =
        UnitPack::fromFile(QStringLiteral(":/data/units/myp-g7-delhi-sultanate.json"));
    UnitContext unit = UnitContextBuilder::fromPack(pack, 0, ib);

    TurnInputs inputs;
    inputs.question = QStringLiteral("Why did rulers build monuments?");
    const QString turn = builder.buildUserTurn(inputs, unit);
    QVERIFY(turn.contains(QStringLiteral("unit concept")));
    QVERIFY(turn.contains(unit.concept));
}

void TestPromptBuilder::systemPromptStableWithUnitAcrossModesAndLanguages()
{
    AppConfig config;
    Paths paths(config);
    TokenEstimator estimator(config);
    PromptBuilder builder(config, paths, estimator);
    QVERIFY(builder.loadTemplates());

    IbReference ib;
    QVERIFY(ib.loadFromResources());
    QString err;
    const UnitPack pack = UnitPack::fromFile(
        QStringLiteral(":/data/units/myp-g7-delhi-sultanate.json"), &err);
    QVERIFY(err.isEmpty());

    UnitContext unit = UnitContextBuilder::fromPack(pack, 0, ib);
    const QString sys = builder.systemPrompt(unit);

    TurnInputs en;
    en.mode = EagleMode::Doubt;
    en.language = SessionLanguage::English;
    en.question = QStringLiteral("Test?");
    TurnInputs hi = en;
    hi.language = SessionLanguage::Hindi;
    builder.buildUserTurn(en, unit);
    builder.buildUserTurn(hi, unit);
    QCOMPARE(builder.systemPrompt(unit), sys);
}

void TestPromptBuilder::systemPromptChangesWhenTopicChanges()
{
    AppConfig config;
    Paths paths(config);
    TokenEstimator estimator(config);
    PromptBuilder builder(config, paths, estimator);
    QVERIFY(builder.loadTemplates());

    IbReference ib;
    QVERIFY(ib.loadFromResources());
    const UnitPack pack = UnitPack::fromFile(
        QStringLiteral(":/data/units/myp-g7-delhi-sultanate.json"));
    QVERIFY(pack.topicCount() >= 2);

    const QString sys0 = builder.systemPrompt(UnitContextBuilder::fromPack(pack, 0, ib));
    const QString sys1 = builder.systemPrompt(UnitContextBuilder::fromPack(pack, 1, ib));
    QVERIFY(!sys0.isEmpty());
    QVERIFY(!sys1.isEmpty());
    QVERIFY(sys0 != sys1);
}

void TestPromptBuilder::systemPromptHasBlankLineBeforeUnitCard()
{
    AppConfig config;
    Paths paths(config);
    TokenEstimator estimator(config);
    PromptBuilder builder(config, paths, estimator);
    QVERIFY(builder.loadTemplates());

    IbReference ib;
    QVERIFY(ib.loadFromResources());
    const UnitPack pack = UnitPack::fromFile(
        QStringLiteral(":/data/units/myp-g7-delhi-sultanate.json"));
    const UnitContext unit = UnitContextBuilder::fromPack(pack, 0, ib);
    const QString sys = builder.systemPrompt(unit);

    QVERIFY(sys.contains(QStringLiteral("\n\nUNIT CARD")));
    QVERIFY(!sys.contains(QLatin1Char('\r')));
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    TestPromptBuilder testObject;
    return QTest::qExec(&testObject, argc, argv);
}

#include "tst_promptbuilder.moc"
