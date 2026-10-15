#include "TemplateEngine.h"

#include <QRegularExpression>

namespace {

bool isTruthy(const QString &value)
{
    if (value.isEmpty()) {
        return false;
    }
    const QString lower = value.trimmed().toLower();
    return lower != QStringLiteral("0") && lower != QStringLiteral("false") && lower != QStringLiteral("no");
}

QString expandConditionals(const QString &text, const QHash<QString, QString> &vars)
{
    QString result = text;
    static const QRegularExpression blockRe(
        QStringLiteral("\\{\\{#if\\s+([a-zA-Z0-9_]+)\\s*\\}\\}(.*?)\\{\\{/if\\}\\}"),
        QRegularExpression::DotMatchesEverythingOption);
    int offset = 0;
    while (true) {
        const QRegularExpressionMatch match = blockRe.match(result, offset);
        if (!match.hasMatch()) {
            break;
        }
        const QString key = match.captured(1);
        const QString body = match.captured(2);
        const QString replacement = isTruthy(vars.value(key)) ? body : QString();
        result.replace(match.capturedStart(), match.capturedLength(), replacement);
        offset = match.capturedStart() + replacement.size();
    }
    return result;
}

} // namespace

QString TemplateEngine::render(const QString &templateText, const QHash<QString, QString> &vars)
{
    QString result = expandConditionals(templateText, vars);
    static const QRegularExpression placeholderRe(QStringLiteral("\\{\\{([a-zA-Z0-9_]+)\\}\\}"));
    int offset = 0;
    while (true) {
        const QRegularExpressionMatch match = placeholderRe.match(result, offset);
        if (!match.hasMatch()) {
            break;
        }
        const QString key = match.captured(1);
        const QString value = vars.value(key);
        result.replace(match.capturedStart(), match.capturedLength(), value);
        offset = match.capturedStart() + value.size();
    }
    return result.trimmed();
}

QStringList TemplateEngine::placeholdersIn(const QString &templateText)
{
    QStringList keys;
    static const QRegularExpression placeholderRe(QStringLiteral("\\{\\{([a-zA-Z0-9_]+)\\}\\}"));
    static const QRegularExpression ifRe(QStringLiteral("\\{\\{#if\\s+([a-zA-Z0-9_]+)\\s*\\}\\}"));
    for (const QRegularExpressionMatch &match : placeholderRe.globalMatch(templateText)) {
        keys.append(match.captured(1));
    }
    for (const QRegularExpressionMatch &match : ifRe.globalMatch(templateText)) {
        keys.append(match.captured(1));
    }
    keys.removeDuplicates();
    return keys;
}
