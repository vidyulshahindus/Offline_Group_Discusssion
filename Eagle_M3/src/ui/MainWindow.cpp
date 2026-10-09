#include "MainWindow.h"

#include "AppConfig.h"
#include "HistoryManager.h"
#include "LlmServices.h"
#include "Metrics.h"
#include "OllamaBackend.h"
#include "Paths.h"
#include "PromptBuilder.h"
#include "PromptTypes.h"
#include "ServiceSupervisor.h"
#include "TokenEstimator.h"
#include "ChatPanel.h"
#include "ListenController.h"
#include "MetricsOverlay.h"
#include "SttConfig.h"
#include "TurnSpeechController.h"
#include "ReadinessPanel.h"
#include "chatbubble.h"
#include "piperspeaker.h"

#include <QAction>
#include <QAudioDevice>
#include <QComboBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QKeySequence>
#include <QShortcut>
#include <QLabel>
#include <QMediaDevices>
#include <QMenuBar>
#include <QPushButton>
#include <QStyle>
#include <QTextCursor>
#include <QTextEdit>

MainWindow::MainWindow(const AppConfig &config, const Paths &paths, ServiceSupervisor &supervisor,
                       LlmServices &llm, QWidget *parent)
    : QMainWindow(parent)
    , m_config(config)
    , m_paths(paths)
    , m_supervisor(supervisor)
    , m_llm(llm)
{
    m_turnSpeech = new TurnSpeechController(m_config, m_paths, this);
    connect(m_turnSpeech, &TurnSpeechController::firstAudio, this, &MainWindow::onTurnFirstAudio);
    connect(m_turnSpeech, &TurnSpeechController::wordBudgetReached, this,
            &MainWindow::onWordBudgetReached);
    connect(m_turnSpeech, &TurnSpeechController::ttsError, this, &MainWindow::onTurnSpeechError);
    connect(m_turnSpeech, &TurnSpeechController::turnIdle, this, &MainWindow::onTurnIdle);
    m_piper = new PiperSpeaker(this);
    wirePaths(paths);
    m_languages.setOptions(SttConfig::languageOptions(m_config));

    m_unitContext.hasUnit = false;
    m_unitContext.registerShort = QStringLiteral("Grade 9 MYP");
    m_unitContext.concept.clear();

    m_chat = new ChatPanel(this);
    setCentralWidget(m_chat);
    m_listen = new ListenController(m_config, m_supervisor, m_chat, this);
    setWindowTitle(QStringLiteral("Eagle GD"));
    resize(960, 720);
    setMinimumSize(640, 480);

    populateModels({m_config.llmModel(), m_config.llmFallbackModel()});

    auto *readinessAction = new QAction(QStringLiteral("Readiness…"), this);
    readinessAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    connect(readinessAction, &QAction::triggered, this, &MainWindow::showReadinessPanel);
    menuBar()->addAction(readinessAction);

    auto *metricsAction = new QAction(QStringLiteral("Metrics"), this);
    metricsAction->setShortcut(QKeySequence(Qt::Key_F12));
    connect(metricsAction, &QAction::triggered, this, &MainWindow::showMetricsOverlay);
    menuBar()->addAction(metricsAction);

    auto *stopShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(stopShortcut, &QShortcut::activated, this, &MainWindow::onStopClicked);

    // §19.2: Space = Listen / stop listening (a text box being typed in keeps its
    // spaces); presenter clicker: PageDown = Listen toggle, PageUp = primary action.
    auto *listenShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(listenShortcut, &QShortcut::activated, this, &MainWindow::onRecordClicked);
    auto *clickerListen = new QShortcut(QKeySequence(Qt::Key_PageDown), this);
    connect(clickerListen, &QShortcut::activated, this, &MainWindow::onRecordClicked);
    auto *clickerPrimary = new QShortcut(QKeySequence(Qt::Key_PageUp), this);
    connect(clickerPrimary, &QShortcut::activated, this, &MainWindow::onSendClicked);

    connect(m_chat, &ChatPanel::sendRequested, this, &MainWindow::onSendClicked);
    connect(m_chat, &ChatPanel::newChatRequested, this, &MainWindow::onNewChatClicked);
    connect(m_chat, &ChatPanel::stopRequested, this, &MainWindow::onStopClicked);
    connect(m_chat, &ChatPanel::speakerIndexChanged, this, &MainWindow::onSpeakerSelectionChanged);
    connect(m_chat, &ChatPanel::refreshDevicesRequested, this, &MainWindow::onRefreshMicsClicked);
    connect(m_chat, &ChatPanel::recordToggled, this, &MainWindow::onRecordClicked);
    connect(m_chat, &ChatPanel::listenToggleRequested, this, &MainWindow::onRecordClicked);
    connect(m_chat->speakRepliesButton(), &QPushButton::toggled, m_turnSpeech,
            &TurnSpeechController::setSpeechEnabled);

    selectInitialLanguage();
    connect(m_chat->languageCombo(), &QComboBox::currentIndexChanged, this,
            &MainWindow::onLanguageChanged);

    connect(m_listen, &ListenController::sendRequested, this, &MainWindow::onSendClicked);
    connect(m_listen, &ListenController::statusMessage, this, [this](const QString &text) {
        m_chat->statusLabel()->setText(text);
    });
    connect(m_listen, &ListenController::errorMessage, this, &MainWindow::onListenError);
    connect(m_listen, &ListenController::busyChanged, this, [this](bool) { refreshTurnChrome(); });
    // Never transcribe Eagle's own voice (audio.mute_mic_while_speaking).
    connect(m_turnSpeech, &TurnSpeechController::outputActiveChanged, m_listen,
            &ListenController::setOutputActive);
    connect(&m_supervisor, &ServiceSupervisor::whisperStateChanged, this,
            [this](bool ready, const QString &) {
                if (ready && !m_listen->isBusy() && !isEagleBusy()) {
                    m_chat->statusLabel()->setText(QStringLiteral("Speech recogniser ready"));
                }
            });
    auto *mediaDevices = new QMediaDevices(this);
    connect(mediaDevices, &QMediaDevices::audioOutputsChanged, this, &MainWindow::refreshSpeakers);

    if (m_llm.backend) {
        connect(m_llm.backend.get(), &OllamaBackend::token, this, &MainWindow::onLlmToken);
        connect(m_llm.backend.get(), &OllamaBackend::finished, this, &MainWindow::onLlmFinished);
        connect(m_llm.backend.get(), &OllamaBackend::failed, this, &MainWindow::onLlmFailed);
        connect(m_llm.backend.get(), &OllamaBackend::probed, this, &MainWindow::onModelsProbed);
    }

    connect(m_piper, &PiperSpeaker::speakingChanged, this, &MainWindow::onPiperSpeakingChanged);
    connect(m_piper, &PiperSpeaker::statusChanged, this, &MainWindow::onPiperStatus);
    connect(m_piper, &PiperSpeaker::errorOccurred, this, &MainWindow::onPiperError);

    refreshSpeakers();

    m_chat->addMessageBubble(ChatRole::Assistant,
                             QStringLiteral("Hi! I am Eagle. Type a question, or press Listen (Space) "
                                            "and speak. Make sure Ollama is running on this machine."));
}

MainWindow::~MainWindow() = default;

QString MainWindow::languageCode() const
{
    const QString code = m_chat->languageCombo()->currentData().toString();
    return code.isEmpty() ? QStringLiteral("en") : code;
}

void MainWindow::selectInitialLanguage()
{
    QComboBox *combo = m_chat->languageCombo();
    int index = combo->findData(m_config.sttLanguageMode().trimmed().toLower());
    if (index < 0) {
        index = combo->findData(QStringLiteral("en"));
    }
    combo->blockSignals(true);
    combo->setCurrentIndex(qMax(0, index));
    combo->blockSignals(false);
    m_listen->setLanguage(languageCode());
    m_supervisor.setSttLanguage(languageCode());
}

void MainWindow::onLanguageChanged(int index)
{
    Q_UNUSED(index);
    const QString code = languageCode();
    m_listen->setLanguage(code);
    m_supervisor.setSttLanguage(code);
    m_chat->statusLabel()->setText(code == QLatin1String("auto")
                                       ? QStringLiteral("Language: Auto (detected per question)")
                                       : QStringLiteral("Language: %1").arg(LanguageService::displayName(code)));
}

SessionLanguage MainWindow::languageForTurn(const QString &question, QString *note) const
{
    const QString code = languageCode();
    if (code != QLatin1String("auto")) {
        return LanguageService::toSessionLanguage(code);
    }
    const LanguageDecision decision = m_languages.decide(question);
    if (note) {
        *note = decision.note;
    }
    return LanguageService::toSessionLanguage(decision.replyCode);
}

bool MainWindow::isEagleBusy() const
{
    return m_pendingTurnOpen || (m_turnSpeech && (m_turnSpeech->isTurnActive() || m_turnSpeech->isSpeechBusy()))
           || (m_piper && m_piper->isBusy());
}

EagleMode MainWindow::currentMode() const
{
    return EagleMode::Doubt;
}

void MainWindow::wirePaths(const Paths &paths)
{
    m_piper->setPiperPaths(paths.piperExecutable(), paths.piperVoiceOnnx(m_config.ttsVoiceEn()));
}

void MainWindow::populateModels(const QStringList &models)
{
    m_chat->modelCombo()->clear();
    QStringList seen;
    for (const QString &model : models) {
        if (model.isEmpty() || seen.contains(model)) {
            continue;
        }
        seen.append(model);
        m_chat->modelCombo()->addItem(model);
    }
    if (m_chat->modelCombo()->count() == 0) {
        m_chat->modelCombo()->addItem(m_config.llmModel());
    }
    m_chat->modelCombo()->setCurrentIndex(0);
}

void MainWindow::onModelsProbed(bool ok, const QString &version, const QStringList &models)
{
    if (!ok) {
        return;
    }
    QStringList merged = models;
    merged.prepend(m_config.llmModel());
    merged.append(m_config.llmFallbackModel());
    populateModels(merged);
    if (!version.isEmpty()) {
        m_chat->statusLabel()->setText(QStringLiteral("Ollama %1").arg(version));
    }
}

void MainWindow::showReadinessPanel()
{
    auto *panel = new ReadinessPanel(m_supervisor, m_turnSpeech, this);
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->open();
}

void MainWindow::showMetricsOverlay()
{
    if (!m_metricsOverlay) {
        m_metricsOverlay = new MetricsOverlay(*m_llm.metrics, this);
    }
    m_metricsOverlay->refresh();
    m_metricsOverlay->show();
    m_metricsOverlay->raise();
    m_metricsOverlay->activateWindow();
}

void MainWindow::onSendClicked()
{
    if (!m_llm.backend) {
        return;
    }
    if (m_listen->isBusy()) {
        // Enter / Send while listening: stop listening, then send the final transcript.
        m_listen->stopListening(true);
        return;
    }
    if (m_llm.backend->isBusy()) {
        m_chat->statusLabel()->setText(
            QStringLiteral("Eagle is still loading the model — try again in a moment."));
        return;
    }

    const QString text = m_chat->input()->toPlainText().trimmed();
    if (text.isEmpty()) {
        return;
    }

    m_chat->input()->clear();
    m_chat->addMessageBubble(ChatRole::User, text);

    QString languageNote;
    m_turnLanguage = languageForTurn(text, &languageNote);

    TurnInputs inputs;
    inputs.mode = currentMode();
    inputs.language = m_turnLanguage;
    inputs.wordLimit = m_chat->currentWordLimit();
    inputs.question = text;

    m_lastUserTurnMessage = m_llm.prompts->buildUserTurn(inputs, m_unitContext);
    const QVector<LlmMessage> messages =
        m_llm.prompts->buildMessages(inputs, m_unitContext, m_llm.history->messages());

    const int turnId = ++m_turnCounter;
    m_turnSpeech->beginTurn(turnId, currentMode(), m_turnLanguage, inputs.wordLimit);
    m_turnSpeech->markPromptBuilt();
    m_pendingTurnOpen = true;

    LlmRequest req;
    req.model = m_chat->modelCombo()->currentText();
    req.messages = messages;
    req.turnId = turnId;
    req.purpose = QStringLiteral("turn");
    req.temperature = m_config.llmTemperatureFor(QStringLiteral("doubt"));
    req.topP = m_config.llmTopP();
    req.topK = m_config.llmTopK();
    req.repeatPenalty = m_config.llmRepeatPenalty();
    req.numPredict = m_llm.estimator->numPredictForWordLimit(inputs.wordLimit, inputs.language);

    m_chat->setActiveAssistantBubble(m_chat->addMessageBubble(ChatRole::Assistant, QString()));
    m_chat->clearStreamBuffer();
    m_chat->statusLabel()->setText(languageNote.isEmpty()
                                       ? QStringLiteral("Generating…")
                                       : QStringLiteral("Generating… · %1").arg(languageNote));
    refreshTurnChrome();
    m_turnTimer.start();
    m_turnSpeech->markRequestSent();
    m_llm.backend->start(req);
}

void MainWindow::onNewChatClicked()
{
    if (m_llm.backend && m_llm.backend->isBusy()) {
        m_llm.backend->abort();
    }
    m_listen->cancel();
    m_turnSpeech->stop();
    m_piper->stop();
    m_pendingTurnOpen = false;
    m_llm.history->reset();
    m_chat->clearConversation();
    m_chat->addMessageBubble(ChatRole::Assistant,
                             QStringLiteral("New chat started. What would you like to ask?"));
    m_chat->statusLabel()->setText(QStringLiteral("Ready"));
}

void MainWindow::onStopClicked()
{
    if (m_llm.backend) {
        m_llm.backend->abort();
    }
    m_listen->cancel();
    m_turnSpeech->stop();
    m_piper->stop();
    m_pendingTurnOpen = false;
    refreshTurnChrome();
    m_chat->statusLabel()->setText(QStringLiteral("Stopped"));
}

void MainWindow::onLlmToken(int turnId, const QString &text)
{
    Q_UNUSED(turnId);
    m_chat->appendAssistantStream(text);
    m_turnSpeech->onLlmToken(text);
}

void MainWindow::onLlmFinished(int turnId, const QString &fullText, const LlmMetrics &metrics)
{
    m_turnSpeech->onLlmFinished();
    const QString spoken = ChatBubble::spokenText(fullText);
    const QVector<LlmMessage> historyBeforeAppend = m_llm.history->messages();
    m_llm.history->appendTurn(m_lastUserTurnMessage, spoken, m_turnLanguage);
    m_pendingLlmMetrics = metrics;
    finalizeTurnMetrics(turnId, metrics, historyBeforeAppend);

    m_chat->setActiveAssistantBubble(nullptr);
    m_chat->clearStreamBuffer();
    refreshTurnChrome();
    if (m_turnSpeech->isTurnActive() || m_turnSpeech->isSpeechBusy()) {
        m_chat->statusLabel()->setText(QStringLiteral("Speaking…"));
    } else {
        m_chat->statusLabel()->setText(QStringLiteral("Ready"));
    }
    m_chat->scrollToBottom();
}

void MainWindow::onTurnIdle(int turnId)
{
    Q_UNUSED(turnId);
    refreshTurnChrome();
    if (!m_listen->isBusy()) {
        m_chat->statusLabel()->setText(QStringLiteral("Ready"));
    }
}

void MainWindow::onTurnFirstAudio(int turnId, qint64 t1Ms)
{
    Q_UNUSED(turnId);
    if (t1Ms > 10000) {
        m_chat->statusLabel()->setStyleSheet(QStringLiteral("color: #c9a227;"));
    }
    if (m_metricsOverlay && m_metricsOverlay->isVisible()) {
        m_metricsOverlay->refresh();
    }
}

void MainWindow::onWordBudgetReached(int turnId)
{
    if (m_llm.backend && m_llm.backend->isBusy()) {
        m_llm.backend->abort();
    }
    Q_UNUSED(turnId);
}

void MainWindow::onTurnSpeechError(const QString &message)
{
    m_chat->statusLabel()->setText(
        QStringLiteral("Speech: %1").arg(message.left(120)));
    refreshTurnChrome();
}

void MainWindow::finalizeTurnMetrics(int turnId, const LlmMetrics &metrics,
                                     const QVector<LlmMessage> &historyBeforeAppend)
{
    int estimatedNew =
        m_llm.estimator->estimateTokens(m_lastUserTurnMessage, m_turnLanguage);
    if (!historyBeforeAppend.isEmpty()
        && historyBeforeAppend.last().role == QStringLiteral("assistant")) {
        estimatedNew += m_llm.estimator->estimateTokens(historyBeforeAppend.last().content,
                                                        m_turnLanguage);
    }

    const qint64 promptNew = metrics.promptEvalNewCount();
    TurnMetrics tm;
    tm.turnId = turnId;
    tm.model = m_chat->modelCombo()->currentText();
    tm.firstTokenMs = metrics.msToFirstToken;
    tm.llmDoneMs = m_turnTimer.elapsed();
    tm.llm = metrics;
    tm.estimatedNewPromptTokens = estimatedNew;
    tm.cacheLikelyHit =
        promptNew > 0 && estimatedNew > 0 && promptNew <= static_cast<qint64>(estimatedNew) * 2;
    tm.timestamps = m_turnSpeech->timestamps();
    if (tm.timestamps.firstTokenMs < 0) {
        tm.timestamps.firstTokenMs = metrics.msToFirstToken;
    }
    tm.t0ToT1Ms = tm.timestamps.firstAudioMs;
    tm.ttsFirstSynthMs = m_turnSpeech->turnFirstSynthMs();
    tm.ttsWarmSynthMs = m_turnSpeech->turnWarmSynthMs();
    tm.ttsWorkerColdSynthMs = m_turnSpeech->workerColdSynthMs();
    tm.ttsWorkerWarmSynthMs = m_turnSpeech->workerWarmSynthMs();
    m_llm.metrics->recordTurn(tm);
    m_pendingTurnOpen = false;

    if (m_metricsOverlay && m_metricsOverlay->isVisible()) {
        m_metricsOverlay->refresh();
    }
}

void MainWindow::onLlmFailed(int turnId, const QString &error, bool recoverable)
{
    Q_UNUSED(turnId);
    Q_UNUSED(recoverable);
    m_pendingTurnOpen = false;
    m_turnSpeech->stop();
    onClientError(error);
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
    refreshTurnChrome();
    m_chat->statusLabel()->setText(QStringLiteral("Error"));
}

void MainWindow::refreshTurnChrome()
{
    const bool llmBusy = m_llm.backend && m_llm.backend->isBusy();
    const bool turnActive = m_turnSpeech && m_turnSpeech->isTurnActive();
    const bool speechBusy = m_turnSpeech && m_turnSpeech->isSpeechBusy();
    const bool piperLegacyBusy = m_piper->isBusy();
    const bool locked = llmBusy || turnActive;

    const bool listenBusy = m_listen && m_listen->isBusy();
    // Listen stays enabled during a turn: pressing it is a barge-in (§10.3 step 6).
    m_chat->sendButton()->setDisabled(locked);
    m_chat->modelCombo()->setDisabled(locked);
    m_chat->modeCombo()->setDisabled(locked);
    m_chat->languageCombo()->setDisabled(locked);
    m_chat->wordLimitCombo()->setDisabled(locked);
    m_chat->speakerCombo()->setDisabled(locked || piperLegacyBusy);
    m_chat->refreshMicsButton()->setDisabled(locked || listenBusy);
    m_chat->stopButton()->setText(QStringLiteral("Stop generating"));
    m_chat->stopButton()->setVisible(llmBusy || turnActive || speechBusy || piperLegacyBusy);
    if (!listenBusy && !turnActive && !speechBusy) {
        if (llmBusy) {
            m_chat->statusLabel()->setText(QStringLiteral("Generating…"));
        }
    } else if (turnActive && !llmBusy && !listenBusy) {
        m_chat->statusLabel()->setText(QStringLiteral("Speaking…"));
    }
}

void MainWindow::setGenerating(bool generating)
{
    Q_UNUSED(generating);
    refreshTurnChrome();
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
            if (m_turnSpeech) {
                m_turnSpeech->setOutputDevice(device);
            }
            return;
        }
    }
}

void MainWindow::onRefreshMicsClicked()
{
    m_listen->refreshMicrophones();
    refreshSpeakers();
    m_chat->statusLabel()->setText(QStringLiteral("Microphones and speakers refreshed"));
}

void MainWindow::onRecordClicked()
{
    if (m_listen->isListening()) {
        m_listen->stopListening(false);
        return;
    }
    if (m_listen->isBusy()) {
        return;  // still transcribing the last segment
    }
    if (isEagleBusy()) {
        // Barge-in: stop generation and speech first, then listen.
        onStopClicked();
    }
    m_piper->stop();
    m_listen->startListening();
    refreshTurnChrome();
}

void MainWindow::onListenError(const QString &message)
{
    m_chat->statusLabel()->setText(QStringLiteral("Speech input problem"));
    m_chat->addMessageBubble(ChatRole::Assistant, message);
    refreshTurnChrome();
}

void MainWindow::onPiperSpeakingChanged(bool speaking)
{
    if (speaking) {
        m_chat->stopButton()->setText(QStringLiteral("Stop speaking"));
    } else {
        m_chat->stopButton()->setText(QStringLiteral("Stop generating"));
    }
    refreshTurnChrome();
}

void MainWindow::onPiperStatus(const QString &status)
{
    const bool llmBusy = m_llm.backend && m_llm.backend->isBusy();
    if (!llmBusy && !m_listen->isBusy()) {
        m_chat->statusLabel()->setText(status);
    }
}

void MainWindow::onPiperError(const QString &message)
{
    m_chat->statusLabel()->setText(QStringLiteral("Speech error"));
    m_chat->addMessageBubble(ChatRole::Assistant, message);
}
