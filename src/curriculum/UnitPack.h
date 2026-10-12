#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

struct UnitPackValidation {
    QStringList errors;
    QStringList warnings;
    bool isValid() const { return errors.isEmpty(); }
};

class UnitPack
{
public:
    UnitPack() = default;
    explicit UnitPack(const QJsonObject &doc);

    static UnitPack fromJson(const QByteArray &bytes, QString *errorOut = nullptr);
    static UnitPack fromFile(const QString &path, QString *errorOut = nullptr);

    bool saveToFile(const QString &path, QString *errorOut = nullptr) const;
    QByteArray toJsonBytes(bool compact = false) const;

    const QJsonObject &document() const { return m_doc; }
    QString sourcePath() const { return m_sourcePath; }
    void setSourcePath(const QString &path) { m_sourcePath = path; }

    QString schema() const;
    QString id() const;
    QString title() const;
    QString programme() const;
    int grade() const;
    QString subject() const;
    QString approvalStatus() const;
    bool isBuiltin() const { return m_builtin; }
    void setBuiltin(bool builtin) { m_builtin = builtin; }

    int topicCount() const;
    QString topicId(int index) const;
    QString topicTitle(int index) const;

    UnitPackValidation validate() const;

private:
    QJsonObject m_doc;
    QString m_sourcePath;
    bool m_builtin = false;
};
