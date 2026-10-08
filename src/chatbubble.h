#pragma once

#include <QFrame>
#include <QString>

class QLabel;

enum class ChatRole {
    User,
    Assistant
};

class ChatBubble : public QFrame
{
    Q_OBJECT

public:
    explicit ChatBubble(ChatRole role, const QString &text, QWidget *parent = nullptr);

    ChatRole role() const;
    static QString spokenText(const QString &raw);
    void setText(const QString &text);
    QString text() const;

    void appendText(const QString &chunk);

private:
    void applyRoleStyle();

    ChatRole m_role;
    QLabel *m_label = nullptr;
    QString m_rawText;
};
