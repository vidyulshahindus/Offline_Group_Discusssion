#include "chatbubble.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QSizePolicy>

namespace {

void stripTaggedBlock(QString &text, const QString &openTag, const QString &closeTag)
{
    int start = 0;
    while ((start = text.indexOf(openTag, start, Qt::CaseInsensitive)) != -1) {
        const int end = text.indexOf(closeTag, start, Qt::CaseInsensitive);
        if (end == -1) {
            text = text.left(start).trimmed();
            return;
        }
        text.remove(start, end + closeTag.size() - start);
    }
}

QString displayTextForAssistant(const QString &raw)
{
    const QString thinkOpen = QStringLiteral("<") + QStringLiteral("think>");
    const QString thinkClose = QStringLiteral("</") + QStringLiteral("think>");
    const QString toolOpen = QStringLiteral("<") + QStringLiteral("tool_code>");
    const QString toolClose = QStringLiteral("</") + QStringLiteral("tool_code>");

    QString cleaned = raw;
    stripTaggedBlock(cleaned, thinkOpen, thinkClose);
    stripTaggedBlock(cleaned, toolOpen, toolClose);

    QStringList kept;
    const QStringList lines = cleaned.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("print(web_search"), Qt::CaseInsensitive)
            || trimmed.startsWith(QStringLiteral("web_search("), Qt::CaseInsensitive)) {
            continue;
        }
        kept.append(line);
    }
    return kept.join(QLatin1Char('\n')).trimmed();
}

QString toBubbleHtml(const QString &plain)
{
    QString html = plain.toHtmlEscaped();
    static const QRegularExpression bold(QStringLiteral("\\*\\*(.+?)\\*\\*"));
    html.replace(bold, QStringLiteral("<b>\\1</b>"));
    html.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
    return html;
}

} // namespace

QString ChatBubble::spokenText(const QString &raw)
{
    QString text = displayTextForAssistant(raw);
    text.replace(QStringLiteral("**"), QString());
    text.replace(QStringLiteral("__"), QString());
    return text.trimmed();
}

ChatBubble::ChatBubble(ChatRole role, const QString &text, QWidget *parent)
    : QFrame(parent)
    , m_role(role)
    , m_rawText(text)
{
    setFrameShape(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(16, 6, 16, 6);

    m_label = new QLabel(this);
    m_label->setWordWrap(true);
    m_label->setTextFormat(Qt::RichText);
    m_label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextBrowserInteraction);
    m_label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    m_label->setMaximumWidth(720);

    if (role == ChatRole::User) {
        outer->addStretch(1);
        outer->addWidget(m_label, 0, Qt::AlignRight);
    } else {
        outer->addWidget(m_label, 0, Qt::AlignLeft);
        outer->addStretch(1);
    }

    applyRoleStyle();
    setText(text);
}

ChatRole ChatBubble::role() const
{
    return m_role;
}

void ChatBubble::setText(const QString &text)
{
    m_rawText = text;
    if (m_role == ChatRole::Assistant) {
        const QString visible = displayTextForAssistant(m_rawText);
        if (visible.isEmpty() && !m_rawText.isEmpty()) {
            m_label->setText(QStringLiteral("Thinking…"));
        } else if (visible.isEmpty()) {
            m_label->setText(QStringLiteral("…"));
        } else {
            m_label->setText(toBubbleHtml(visible));
        }
    } else {
        m_label->setText(toBubbleHtml(m_rawText));
    }
}

QString ChatBubble::text() const
{
    return m_rawText;
}

void ChatBubble::appendText(const QString &chunk)
{
    m_rawText.append(chunk);
    setText(m_rawText);
}

void ChatBubble::applyRoleStyle()
{
    if (m_role == ChatRole::User) {
        setStyleSheet(QStringLiteral(
            "ChatBubble { background: transparent; }"
            "QLabel {"
            "  background-color: #2f6fed;"
            "  color: #ffffff;"
            "  padding: 12px 14px;"
            "  border-radius: 16px;"
            "  font-size: 14px;"
            "  line-height: 1.45;"
            "}"));
    } else {
        setStyleSheet(QStringLiteral(
            "ChatBubble { background: transparent; }"
            "QLabel {"
            "  background-color: #2f2f2f;"
            "  color: #ececec;"
            "  padding: 12px 14px;"
            "  border-radius: 16px;"
            "  font-size: 14px;"
            "  line-height: 1.45;"
            "}"));
    }
}
