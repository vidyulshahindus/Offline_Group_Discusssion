#include "UnitPack.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>

class TestUnitPack : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void samplePacksValidate();
    void missingTitleFailsClearly();
    void roundTripIdentical();
};

static QString samplePath(const QString &fileName)
{
    return QStringLiteral(":/data/units/") + fileName;
}

void TestUnitPack::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("EagleGDTest"));
}

void TestUnitPack::samplePacksValidate()
{
    const QStringList files = {
        QStringLiteral("dp-bio-g11-cells-form-function.json"),
        QStringLiteral("myp-g9-industrial-revolution.json"),
        QStringLiteral("myp-g7-delhi-sultanate.json"),
    };
    for (const QString &file : files) {
        QString err;
        const UnitPack pack = UnitPack::fromFile(samplePath(file), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(!pack.id().isEmpty());
        const UnitPackValidation v = pack.validate();
        QVERIFY2(v.isValid(), qPrintable(v.errors.join(QStringLiteral("; "))));
    }
}

void TestUnitPack::missingTitleFailsClearly()
{
    QString err;
    UnitPack pack = UnitPack::fromFile(samplePath(QStringLiteral("myp-g7-delhi-sultanate.json")), &err);
    QVERIFY(err.isEmpty());
    QJsonObject doc = pack.document();
    doc.remove(QStringLiteral("title"));
    pack = UnitPack(doc);
    const UnitPackValidation v = pack.validate();
    QVERIFY(!v.isValid());
    QVERIFY(v.errors.join(QStringLiteral(" ")).contains(QStringLiteral("title")));
}

void TestUnitPack::roundTripIdentical()
{
    QString err;
    const UnitPack original =
        UnitPack::fromFile(samplePath(QStringLiteral("myp-g7-delhi-sultanate.json")), &err);
    QVERIFY(err.isEmpty());
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("roundtrip.json"));
    QVERIFY(original.saveToFile(path, &err));
    const UnitPack loaded = UnitPack::fromFile(path, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(loaded.toJsonBytes(true), original.toJsonBytes(true));
}

QTEST_MAIN(TestUnitPack)
#include "tst_unitpack.moc"
