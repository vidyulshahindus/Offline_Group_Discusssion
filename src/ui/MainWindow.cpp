#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTextCursor>
#include <QTextEdit>

#include "MainWindow.h"

#include "AppConfig.h"
#include "Paths.h"
#include "ServiceSupervisor.h"
#include "ChatPanel.h"
#include "ollamaclient.h"
#include "piperspeaker.h"
#include "ReadinessPanel.h"
#include "speechinput.h"

#include <QAction>
#include <QAudioDevice>
#include <QJsonObject>
#include <QMenuBar>
#include <QStyle>
#include <QTextCursor>

MainWindow::MainWindow(const AppConfig &config, const Paths &paths, ServiceSupervisor &supervisor,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_config(config)
    , m_supervisor(supervisor)
{
    m_client = new OllamaClient(this);
    m_speech = new SpeechInput(this);
    m_piper = new PiperSpeaker(this);
    wirePaths(paths);

    m_chat = new ChatPanel(this);
    setCentralWidget(m_chat);
    setWindowTitle(QStringLiteral("Eagle GD"));
    resize(960, 720);
    setMinimumSize(640, 480);

    populateModels();

    auto *readinessAction = new QAction(QStringLiteral("Readiness…"), this);
    readinessAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    connect(readinessAction, &QAction::triggered, this, &MainWindow::showReadinessPanel);
    menuBar()->addAction(readinessAction);

    connect(m_chat, &ChatPanel::sendRequested, this, &MainWindow::onSendClicked);
    connect(m_chat, &ChatPanel::newChatRequested, this, &MainWindow::onNewChatClicked);
    connect(m_chat, &ChatPanel::stopRequested, this, &MainWindow::onStopClicked);
    connect(m_chat, &ChatPanel::micIndexChanged, this, &MainWindow::onMicSelectionChanged);
    connect(m_chat, &ChatPanel::speakerIndexChanged, this, &MainWindow::onSpeakerSelectionChanged);
    connect(m_chat, &ChatPanel::refreshDevicesRequested, this, &MainWindow::onRefreshMicsClicked);
    connect(m_chat, &ChatPanel::recordToggled, this, &MainWindow::onRecordClicked);

    connect(m_client, &OllamaClient::responseChunk, this, &MainWindow::onResponseChunk);
    connect(m_client, &OllamaClient::responseFinished, this, &MainWindow::onResponseFinished);
    connect(m_client, &OllamaClient::errorOccurred, this, &MainWindow::onClientError);
    connect(m_speech, &SpeechInput::recordingChanged, this, &MainWindow::onRecordingChanged);
    connect(m_speech, &SpeechInput::partialTranscription, this, &MainWindow::onPartialTranscription);
    connect(m_speech, &SpeechInput::transcriptionReady, this, &MainWindow::onTranscriptionReady);
    connect(m_speech, &SpeechInput::errorOccurred, this, &MainWindow::onSpeechError);
    connect(m_speech, &SpeechInput::statusChanged, this, &MainWindow::onSpeechStatus);
    connect(m_piper, &PiperSpeaker::speakingChanged, this, &MainWindow::onPiperSpeakingChanged);
    connect(m_piper, &PiperSpeaker::statusChanged, this, &MainWindow::onPiperStatus);
    connect(m_piper, &PiperSpeaker::errorOccurred, this, &MainWindow::onPiperError);

    refreshMicrophones();
    refreshSpeakers();

    m_chat->addMessageBubble(ChatRole::Assistant,
                             QStringLiteral("Hi! I am Eagle. Choose a model above and ask anything. "
                                            "Make sure Ollama is running on this machine."));
}

MainWindow::~MainWindow() = default;

void MainWindow::wirePaths(const Paths &paths)
{
    m_speech->setWhisperPaths(paths.whisperCli(), paths.whisperModel(m_config.sttModel()));
    m_piper->setPiperPaths(paths.piperExecutable(), paths.piperVoiceOnnx(m_config.ttsVoiceEn()));
}

void MainWindow::populateModels()
{
    m_chat->modelCombo()->clear();
    m_chat->modelCombo()->addItem(m_config.llmModel());
    const QString fallback = m_config.llmFallbackModel();
    if (!fallback.isEmpty() && fallback != m_config.llmModel()) {
        m_chat->modelCombo()->addItem(fallback);
    }
    m_chat->modelCombo()->setCurrentIndex(0);
}

void MainWindow::showReadinessPanel()
{
    auto *panel = new ReadinessPanel(m_supervisor, this);
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->open();
}

void MainWindow::onSendClicked()
{
    if (m_client->isBusy()) {
        return;
    }

    const QString text = m_chat->input()->toPlainText().trimmed();
    if (text.isEmpty()) {
        return;
    }

    m_chat->input()->clear();

    m_chat->addMessageBubble(ChatRole::User, text);

    QJsonObject userMessage;
    userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMessage.insert(QStringLiteral("content"), text);
    m_chat->messages().append(userMessage);

    m_chat->setActiveAssistantBubble(m_chat->addMessageBubble(ChatRole::Assistant, QString()));
    m_chat->clearStreamBuffer();

    setGenerating(true);
    m_client->sendChat(m_chat->modelCombo()->currentText(), m_chat->conversationForApi());
}

void MainWindow::onNewChatClicked()
{
    if (m_client->isBusy()) {
        m_client->abortRequest();
    }
    m_piper->stop();
    m_chat->clearConversation();
    m_chat->addMessageBubble(ChatRole::Assistant,
                             QStringLiteral("New chat started. What would you like to talk about?"));
    m_chat->statusLabel()->setText(QStringLiteral("Ready"));
}

void MainWindow::onStopClicked()
{
    m_client->abortRequest();
    m_piper->stop();
    setGenerating(false);
    m_chat->statusLabel()->setText(QStringLiteral("Stopped"));
}

void MainWindow::onResponseChunk(const QString &text)
{
    m_chat->appendAssistantStream(text);
}

void MainWindow::onResponseFinished()
{
    const QString spoken = ChatBubble::spokenText(m_chat->streamBuffer());
    m_chat->commitAssistantToHistory(m_chat->streamBuffer());

    m_chat->setActiveAssistantBubble(nullptr);
    m_chat->clearStreamBuffer();
    setGenerating(false);
    m_chat->statusLabel()->setText(QStringLiteral("Ready"));

    if (m_chat->listenButton()->isChecked() && !m_speech->isRecording() && !spoken.isEmpty()) {
        m_piper->speak(spoken);
    }
    m_chat->scrollToBottom();
}

void MainWindow::onClientError(const QString &message)
{
    if (m_chat->activeAssistantBubble()) {
        m_chat->activeAssistantBubble()->appendText(QStringLiteral("\n\n[Error: %1]").arg(message));
    } else {
        m_chat->addMessageBubble(ChatRole::Assistant,
                                 QStringLiteral("Could not reach Ollama. Is it running?\n\n%1").arg(message));
    }

    m_chat->setActiveAssistantBubble(nullptr);
    m_chat->clearStreamBuffer();
    setGenerating(false);
    m_chat->statusLabel()->setText(QStringLiteral("Error"));
}

void MainWindow::setGenerating(bool generating)
{
    m_chat->sendButton()->setDisabled(generating);
    m_chat->modelCombo()->setDisabled(generating);
    m_chat->wordLimitCombo()->setDisabled(generating);
    m_chat->recordButton()->setDisabled(generating || m_speech->isBusy());
    m_chat->micCombo()->setDisabled(generating || m_speech->isRecording());
    m_chat->speakerCombo()->setDisabled(generating || m_piper->isBusy());
    m_chat->refreshMicsButton()->setDisabled(generating || m_speech->isRecording());
    m_chat->stopButton()->setText(QStringLiteral("Stop generating"));
    m_chat->stopButton()->setVisible(generating || m_piper->isBusy());
    if (!m_speech->isBusy()) {
        m_chat->statusLabel()->setText(generating ? QStringLiteral("Generating…") : QStringLiteral("Ready"));
    }
}

void MainWindow::refreshMicrophones()
{
    const QByteArray previousId = m_chat->micCombo()->currentData().toByteArray();
    m_chat->micCombo()->blockSignals(true);
    m_chat->micCombo()->clear();

    const QList<QAudioDevice> devices = m_speech->microphones();
    for (const QAudioDevice &device : devices) {
        m_chat->micCombo()->addItem(device.description(), device.id());
    }

    int index = m_chat->micCombo()->findData(previousId);
    if (index < 0) {
        index = 0;
    }
    if (m_chat->micCombo()->count() == 0) {
        m_chat->micCombo()->addItem(QStringLiteral("No microphone found"));
        m_chat->recordButton()->setEnabled(false);
    } else {
        m_chat->micCombo()->setCurrentIndex(index);
        m_chat->recordButton()->setEnabled(!m_client->isBusy());
        onMicSelectionChanged(index);
    }
    m_chat->micCombo()->blockSignals(false);
}

void MainWindow::refreshSpeakers()
{
    const QByteArray previousId = m_chat->speakerCombo()->currentData().toByteArray();
    m_chat->speakerCombo()->blockSignals(true);
    m_chat->speakerCombo()->clear();

    const QList<QAudioDevice> devices = m_piper->speakers();
    for (const QAudioDevice &device : devices) {
        m_chat->speakerCombo()->addItem(device.description(), device.id());
    }

    int index = m_chat->speakerCombo()->findData(previousId);
    if (index < 0) {
        index = 0;
    }
    if (m_chat->speakerCombo()->count() == 0) {
        m_chat->speakerCombo()->addItem(QStringLiteral("No speaker found"));
    } else {
        m_chat->speakerCombo()->setCurrentIndex(index);
        onSpeakerSelectionChanged(index);
    }
    m_chat->speakerCombo()->blockSignals(false);
}

void MainWindow::onMicSelectionChanged(int index)
{
    if (index < 0) {
        return;
    }
    const QByteArray id = m_chat->micCombo()->itemData(index).toByteArray();
    const QList<QAudioDevice> devices = m_speech->microphones();
    for (const QAudioDevice &device : devices) {
        if (device.id() == id) {
            m_speech->setMicrophone(device);
            return;
        }
    }
}

void MainWindow::onSpeakerSelectionChanged(int index)
{
    if (index < 0) {
        return;
    }
    const QByteArray id = m_chat->speakerCombo()->itemData(index).toByteArray();
    const QList<QAudioDevice> devices = m_piper->speakers();
    for (const QAudioDevice &device : devices) {
        if (device.id() == id) {
            m_piper->setSpeaker(device);
            return;
        }
    }
}

void MainWindow::onRefreshMicsClicked()
{
    refreshMicrophones();
    refreshSpeakers();
    m_chat->statusLabel()->setText(QStringLiteral("Microphones and speakers refreshed"));
}

void MainWindow::onRecordClicked()
{
    if (m_speech->isRecording()) {
        m_speech->stopRecording();
        return;
    }
    if (m_client->isBusy() || m_speech->isBusy()) {
        return;
    }
    onMicSelectionChanged(m_chat->micCombo()->currentIndex());
    m_piper->stop();
    m_speechPrefix = m_chat->input()->toPlainText().trimmed();
    m_speech->startRecording();
}

void MainWindow::showSpeechText(const QString &text)
{
    const QString combined = m_speechPrefix.isEmpty() ? text : m_speechPrefix + QLatin1Char(' ') + text;
    m_chat->input()->setPlainText(combined);
    m_chat->input()->moveCursor(QTextCursor::End);
}

void MainWindow::onPartialTranscription(const QString &text)
{
    showSpeechText(text);
}

void MainWindow::onTranscriptionReady(const QString &text)
{
    showSpeechText(text);
    m_chat->input()->setFocus();
    m_chat->statusLabel()->setText(QStringLiteral("Ready"));
    m_chat->recordButton()->setEnabled(!m_client->isBusy());
    m_chat->sendButton()->setEnabled(!m_client->isBusy());
}

void MainWindow::onRecordingChanged(bool recording)
{
    m_chat->recordButton()->setText(recording ? QStringLiteral("Stop") : QStringLiteral("Mic"));
    m_chat->recordButton()->setProperty("recording", recording);
    m_chat->recordButton()->style()->unpolish(m_chat->recordButton());
    m_chat->recordButton()->style()->polish(m_chat->recordButton());
    m_chat->micCombo()->setDisabled(recording || m_client->isBusy());
    m_chat->refreshMicsButton()->setDisabled(recording || m_client->isBusy());
    m_chat->sendButton()->setDisabled(recording || m_client->isBusy());
}

void MainWindow::onSpeechError(const QString &message)
{
    m_chat->statusLabel()->setText(QStringLiteral("Speech error"));
    m_chat->addMessageBubble(ChatRole::Assistant, message);
    m_chat->recordButton()->setEnabled(!m_client->isBusy());
    m_chat->sendButton()->setEnabled(!m_client->isBusy() && !m_speech->isRecording());
}

void MainWindow::onPiperSpeakingChanged(bool speaking)
{
    m_chat->speakerCombo()->setDisabled(speaking || m_client->isBusy());
    if (speaking) {
        m_chat->stopButton()->setText(QStringLiteral("Stop speaking"));
        m_chat->stopButton()->setVisible(true);
        return;
    }
    if (!m_client->isBusy()) {
        m_chat->stopButton()->setVisible(false);
        m_chat->stopButton()->setText(QStringLiteral("Stop generating"));
    }
}

void MainWindow::onPiperStatus(const QString &status)
{
    if (!m_client->isBusy() && !m_speech->isBusy()) {
        m_chat->statusLabel()->setText(status);
    }
}

void MainWindow::onPiperError(const QString &message)
{
    m_chat->statusLabel()->setText(QStringLiteral("Speech error"));
    m_chat->addMessageBubble(ChatRole::Assistant, message);
}

void MainWindow::onSpeechStatus(const QString &status)
{
    m_chat->statusLabel()->setText(status);
    const bool locked = status == QStringLiteral("Transcribing…") || m_speech->isRecording() || m_client->isBusy();
    m_chat->recordButton()->setDisabled(m_client->isBusy() || status == QStringLiteral("Transcribing…"));
    m_chat->sendButton()->setDisabled(locked);
}
