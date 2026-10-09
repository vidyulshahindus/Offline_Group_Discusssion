#include "HindiFeminineFixer.h"

#include <QRegularExpression>

namespace {

bool shouldApply(const QString &text)
{
    return text.contains(QStringLiteral("हूँ")) || text.contains(QStringLiteral("हूं"))
           || text.contains(QStringLiteral("ूँगा")) || text.contains(QStringLiteral("ूंगा"))
           || text.contains(QStringLiteral("ऊँगा")) || text.contains(QStringLiteral("ऊंगा"));
}

QString replaceAll(QString text, const QRegularExpression &re, const QString &replacement)
{
    return text.replace(re, replacement);
}

} // namespace

QString HindiFeminineFixer::apply(const QString &sentence)
{
    if (!shouldApply(sentence)) {
        return sentence;
    }
    QString out = sentence;
    out = replaceAll(out, QRegularExpression(QStringLiteral("(\\S+)ता हूँ")),
                     QStringLiteral("\\1ती हूँ"));
    out = replaceAll(out, QRegularExpression(QStringLiteral("(\\S+)ता हूं")),
                     QStringLiteral("\\1ती हूं"));
    out = replaceAll(out, QRegularExpression(QStringLiteral("रहा हूँ")),
                     QStringLiteral("रही हूँ"));
    out = replaceAll(out, QRegularExpression(QStringLiteral("रहा हूं")),
                     QStringLiteral("रही हूं"));
    out = replaceAll(out, QRegularExpression(QStringLiteral("गया हूँ")),
                     QStringLiteral("गई हूँ"));
    out = replaceAll(out, QRegularExpression(QStringLiteral("गया हूं")),
                     QStringLiteral("गई हूं"));
    out = replaceAll(out, QRegularExpression(QStringLiteral("(\\S+)ूँगा$")),
                     QStringLiteral("\\1ूँगी"));
    out = replaceAll(out, QRegularExpression(QStringLiteral("(\\S+)ूंगा$")),
                     QStringLiteral("\\1ूंगी"));
    out = replaceAll(out, QRegularExpression(QStringLiteral("(\\S+)ऊँगा$")),
                     QStringLiteral("\\1ऊँगी"));
    out = replaceAll(out, QRegularExpression(QStringLiteral("(\\S+)ऊंगा$")),
                     QStringLiteral("\\1ऊंगी"));
    return out;
}
