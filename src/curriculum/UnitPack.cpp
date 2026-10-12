#include "UnitPack.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

UnitPack::UnitPack(const QJsonObject &doc)
    : m_doc(doc)
{
}

UnitPack UnitPack::fromJson(const QByteArray &bytes, QString *errorOut)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut) {
            *errorOut = QStringLiteral("JSON parse error: %1").arg(err.errorString());
        }
        return {};
    }
    return UnitPack(doc.object());
}

UnitPack UnitPack::fromFile(const QString &path, QString *errorOut)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Could not read %1").arg(path);
        }
        return {};
    }
    UnitPack pack = fromJson(file.readAll(), errorOut);
    pack.setSourcePath(path);
    return pack;
}

bool UnitPack::saveToFile(const QString &path, QString *errorOut) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Could not write %1").arg(path);
        }
        return false;
    }
    file.write(toJsonBytes(false));
    return true;
}

QByteArray UnitPack::toJsonBytes(bool compact) const
{
    return QJsonDocument(m_doc).toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented);
}

QString UnitPack::schema() const
{
    return m_doc.value(QStringLiteral("schema")).toString();
}

QString UnitPack::id() const
{
    return m_doc.value(QStringLiteral("id")).toString();
}

QString UnitPack::title() const
{
    return m_doc.value(QStringLiteral("title")).toString();
}

QString UnitPack::programme() const
{
    return m_doc.value(QStringLiteral("programme")).toString();
}

int UnitPack::grade() const
{
    return m_doc.value(QStringLiteral("grade")).toInt();
}

QString UnitPack::subject() const
{
    return m_doc.value(QStringLiteral("subject")).toString();
}

QString UnitPack::approvalStatus() const
{
    return m_doc.value(QStringLiteral("authoring"))
        .toObject()
        .value(QStringLiteral("status"))
        .toString();
}

int UnitPack::topicCount() const
{
    return m_doc.value(QStringLiteral("clm"))
        .toObject()
        .value(QStringLiteral("topics"))
        .toArray()
        .size();
}

QString UnitPack::topicId(int index) const
{
    const QJsonArray topics =
        m_doc.value(QStringLiteral("clm")).toObject().value(QStringLiteral("topics")).toArray();
    if (index < 0 || index >= topics.size()) {
        return {};
    }
    return topics.at(index).toObject().value(QStringLiteral("id")).toString();
}

QString UnitPack::topicTitle(int index) const
{
    const QJsonArray topics =
        m_doc.value(QStringLiteral("clm")).toObject().value(QStringLiteral("topics")).toArray();
    if (index < 0 || index >= topics.size()) {
        return {};
    }
    return topics.at(index).toObject().value(QStringLiteral("title")).toString();
}
