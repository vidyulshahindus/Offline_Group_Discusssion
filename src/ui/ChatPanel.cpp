#include "ChatPanel.h"

#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

ChatPanel::ChatPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("headerBar"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 14, 20, 14);
    headerLayout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("Eagle GD"), header);
    title->setObjectName(QStringLiteral("appTitle"));

    m_modelCombo = new QComboBox(header);
    m_modelCombo->setObjectName(QStringLiteral("modelCombo"));
    m_modelCombo->setToolTip(QStringLiteral("Model used for the next reply"));

    m_modeCombo = new QComboBox(header);
    m_modeCombo->setObjectName(QStringLiteral("modeCombo"));
    m_modeCombo->addItem(QStringLiteral("Doubt"), QStringLiteral("doubt"));
    m_modeCombo->setToolTip(QStringLiteral("CLM mode for the next turn"));

    m_languageCombo = new QComboBox(header);
    m_languageCombo->setObjectName(QStringLiteral("languageCombo"));
    m_languageCombo->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_languageCombo->addItem(QStringLiteral("Hindi"), QStringLiteral("hi"));
    m_languageCombo->setToolTip(QStringLiteral("Reply language"));

    auto *wordLimitLabel = new QLabel(QStringLiteral("Word limit:"), header);
    wordLimitLabel->setObjectName(QStringLiteral("wordLimitLabel"));

    m_wordLimitCombo = new QComboBox(header);
    m_wordLimitCombo->setObjectName(QStringLiteral("wordLimitCombo"));
    m_wordLimitCombo->addItems({QStringLiteral("50"), QStringLiteral("75"), QStringLiteral("100"),
                                QStringLiteral("125"), QStringLiteral("150"), QStringLiteral("200")});
    m_wordLimitCombo->setCurrentText(QStringLiteral("75"));
    m_wordLimitCombo->setToolTip(QStringLiteral("Maximum words the assistant should use in each reply"));

    m_listenButton = new QPushButton(QStringLiteral("Listen"), header);
    m_listenButton->setObjectName(QStringLiteral("listenButton"));
    m_listenButton->setCheckable(true);
    m_listenButton->setChecked(true);
    m_listenButton->setToolTip(QStringLiteral("Speak each assistant reply with Piper"));

    auto *newChatButton = new QPushButton(QStringLiteral("New chat"), header);
    newChatButton->setObjectName(QStringLiteral("newChatButton"));
    connect(newChatButton, &QPushButton::clicked, this, &ChatPanel::newChatRequested);

    m_statusLabel = new QLabel(QStringLiteral("Ready"), header);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));

    headerLayout->addWidget(title);
    headerLayout->addSpacing(8);
    headerLayout->addWidget(m_modelCombo);
    headerLayout->addWidget(m_modeCombo);
    headerLayout->addWidget(m_languageCombo);
    headerLayout->addWidget(wordLimitLabel);
    headerLayout->addWidget(m_wordLimitCombo);
    headerLayout->addStretch(1);
    headerLayout->addWidget(m_statusLabel);
    headerLayout->addWidget(m_listenButton);
    headerLayout->addWidget(newChatButton);

    m_chatScroll = new QScrollArea(this);
    m_chatScroll->setObjectName(QStringLiteral("chatScroll"));
    m_chatScroll->setWidgetResizable(true);
    m_chatScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_messageContainer = new QWidget(m_chatScroll);
    m_messageContainer->setObjectName(QStringLiteral("messageContainer"));
    m_messageLayout = new QVBoxLayout(m_messageContainer);
    m_messageLayout->setContentsMargins(0, 16, 0, 16);
    m_messageLayout->setSpacing(4);
    m_messageLayout->addStretch(1);
    m_chatScroll->setWidget(m_messageContainer);

    auto *inputBar = new QFrame(this);
    inputBar->setObjectName(QStringLiteral("inputBar"));
    auto *inputOuter = new QVBoxLayout(inputBar);
    inputOuter->setContentsMargins(20, 12, 20, 20);
    inputOuter->setSpacing(10);

    auto *inputRow = new QHBoxLayout();
    inputRow->setSpacing(10);

    m_micCombo = new QComboBox(inputBar);
    m_micCombo->setObjectName(QStringLiteral("micCombo"));
    m_micCombo->setToolTip(QStringLiteral("Microphone used for speech to text"));
    connect(m_micCombo, &QComboBox::currentIndexChanged, this, &ChatPanel::micIndexChanged);

    m_refreshMicsButton = new QPushButton(QStringLiteral("↻"), inputBar);
    m_refreshMicsButton->setObjectName(QStringLiteral("refreshMicsButton"));
    m_speakerCombo = new QComboBox(inputBar);
    m_speakerCombo->setObjectName(QStringLiteral("speakerCombo"));
    m_speakerCombo->setToolTip(QStringLiteral("Speaker used for Piper playback"));
    connect(m_speakerCombo, &QComboBox::currentIndexChanged, this, &ChatPanel::speakerIndexChanged);

    m_refreshMicsButton->setToolTip(QStringLiteral("Refresh microphones and speakers"));
    connect(m_refreshMicsButton, &QPushButton::clicked, this, &ChatPanel::refreshDevicesRequested);

    m_recordButton = new QPushButton(QStringLiteral("Mic"), inputBar);
    m_recordButton->setObjectName(QStringLiteral("recordButton"));
    m_recordButton->setToolTip(QStringLiteral("Start or stop voice input"));
    connect(m_recordButton, &QPushButton::clicked, this, &ChatPanel::recordToggled);

    m_input = new QTextEdit(inputBar);
    m_input->setObjectName(QStringLiteral("messageInput"));
    m_input->setPlaceholderText(QStringLiteral("Ask Eagle…  (Enter to send, Shift+Enter for new line, Mic for voice)"));
    m_input->setFixedHeight(52);
    m_input->setTabChangesFocus(false);
    m_input->installEventFilter(this);
    installEventFilter(this);

    m_sendButton = new QPushButton(QStringLiteral("↑"), inputBar);
    m_sendButton->setObjectName(QStringLiteral("sendButton"));
    m_sendButton->setToolTip(QStringLiteral("Send message"));
    connect(m_sendButton, &QPushButton::clicked, this, &ChatPanel::sendRequested);

    inputRow->addWidget(m_micCombo, 0, Qt::AlignBottom);
    inputRow->addWidget(m_speakerCombo, 0, Qt::AlignBottom);
    inputRow->addWidget(m_refreshMicsButton, 0, Qt::AlignBottom);
    inputRow->addWidget(m_recordButton, 0, Qt::AlignBottom);
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_sendButton, 0, Qt::AlignBottom);

    m_stopButton = new QPushButton(QStringLiteral("Stop generating"), inputBar);
    m_stopButton->setObjectName(QStringLiteral("stopButton"));
    m_stopButton->setVisible(false);
    connect(m_stopButton, &QPushButton::clicked, this, &ChatPanel::stopRequested);

    inputOuter->addLayout(inputRow);
    inputOuter->addWidget(m_stopButton, 0, Qt::AlignHCenter);

    rootLayout->addWidget(header);
    rootLayout->addWidget(m_chatScroll, 1);
    rootLayout->addWidget(inputBar);
}

bool ChatPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Space && watched != m_input) {
            emit listenToggleRequested();
            return true;
        }
    }
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (!(keyEvent->modifiers() & Qt::ShiftModifier)) {
                emit sendRequested();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChatPanel::scrollToBottom()
{
    if (!m_chatScroll || !m_chatScroll->verticalScrollBar()) {
        return;
    }
    QScrollBar *bar = m_chatScroll->verticalScrollBar();
    bar->setValue(bar->maximum());
}

ChatBubble *ChatPanel::addMessageBubble(ChatRole role, const QString &text)
{
    auto *bubble = new ChatBubble(role, text, m_messageContainer);
    const int insertIndex = qMax(0, m_messageLayout->count() - 1);
    m_messageLayout->insertWidget(insertIndex, bubble);
    scrollToBottom();
    return bubble;
}

void ChatPanel::clearConversation()
{
    m_activeAssistantBubble = nullptr;
    m_streamBuffer.clear();

    while (m_messageLayout->count() > 1) {
        QLayoutItem *item = m_messageLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
}

int ChatPanel::currentWordLimit() const
{
    bool ok = false;
    const int limit = m_wordLimitCombo->currentText().toInt(&ok);
    return ok ? limit : 75;
}

void ChatPanel::appendAssistantStream(const QString &text)
{
    m_streamBuffer.append(text);
    if (m_activeAssistantBubble) {
        m_activeAssistantBubble->appendText(text);
        scrollToBottom();
    }
}

void ChatPanel::setActiveAssistantBubble(ChatBubble *bubble)
{
    m_activeAssistantBubble = bubble;
}
