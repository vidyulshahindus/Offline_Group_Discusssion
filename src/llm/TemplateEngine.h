#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

class TemplateEngine
{
public:
    static QString render(const QString &templateText, const QHash<QString, QString> &vars);
    static QStringList placeholdersIn(const QString &templateText);
};
