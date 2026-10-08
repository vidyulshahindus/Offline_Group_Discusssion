#pragma once

#include <QJsonArray>
#include <QWidget>

#include "chatbubble.h"

class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QTextEdit;
class QVBoxLayout;

class ChatPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ChatPanel(QWidget *parent = nullptr);

    QComboBox *modelCombo() const { return m_modelCombo; }
    QComboBox *modeCombo() const { return m_modeCombo; }
    QComboBox *languageCombo() const { return m_languageCombo; }
    QComboBox *wordLimitCombo() const { return m_wordLimitCombo; }
    QComboBox *micCombo() const { return m_micCombo; }
    QComboBox *speakerCombo() const { return m_speakerCombo; }
    QLabel *statusLabel() const { return m_statusLabel; }
    QPushButton *listenButton() const { return m_listenButton; }
    QPushButton *sendButton() const { return m_sendButton; }
    QPushButton *stopButton() const { return m_stopButton; }
    QPushButton *recordButton() const { return m_recordButton; }
    QPushButton *refreshMicsButton() const { return m_refreshMicsButton; }
    QTextEdit *input() const { return m_input; }

    ChatBubble *addMessageBubble(ChatRole role, const QString &text);
    void clearConversation();
    void scrollToBottom();
    int currentWordLimit() const;
    void appendAssistantStream(const QString &text);
    void setActiveAssistantBubble(ChatBubble *bubble);
    ChatBubble *activeAssistantBubble() const { return m_activeAssistantBubble; }
    QString streamBuffer() const { return m_streamBuffer; }
    void clearStreamBuffer() { m_streamBuffer.clear(); }
signals:
    void sendRequested();
    void newChatRequested();
    void stopRequested();
    void micIndexChanged(int index);
    void speakerIndexChanged(int index);
    void refreshDevicesRequested();
    void recordToggled();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QComboBox *m_modelCombo = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QComboBox *m_languageCombo = nullptr;
    QComboBox *m_wordLimitCombo = nullptr;
    QComboBox *m_micCombo = nullptr;
    QComboBox *m_speakerCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    QScrollArea *m_chatScroll = nullptr;
    QWidget *m_messageContainer = nullptr;
    QVBoxLayout *m_messageLayout = nullptr;
    QTextEdit *m_input = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_listenButton = nullptr;
    QPushButton *m_recordButton = nullptr;
    QPushButton *m_refreshMicsButton = nullptr;

    ChatBubble *m_activeAssistantBubble = nullptr;
    QString m_streamBuffer;
};
