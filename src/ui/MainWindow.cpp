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
#include "MetricsOverlay.h"
#include "Paths.h"
#include "TurnSpeechController.h"
#include "ReadinessPanel.h"
#include "chatbubble.h"
#include "piperspeaker.h"
#include "SttEngine.h"
#include "CurriculumController.h"
#include "UnitContextBuilder.h"
#include "UnitPack.h"

#include <QAction>
#include <QJsonArray>
#include <QAudioDevice>
#include <QComboBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QKeySequence>
#include <QShortcut>
#include <QLabel>
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
    connect(m_turnSpeech, &TurnSpeechController::audioPlaybackChanged, this,
            &MainWindow::syncMicPlaybackMute);
    connect(m_turnSpeech, &TurnSpeechController::testToneFinished, this,
            &MainWindow::syncMicPlaybackMute);
    m_stt = new SttEngine(m_config, m_paths, m_supervisor, this);
    m_piper = new PiperSpeaker(this);
    wirePaths(paths);

    m_chat = new ChatPanel(this);
    setCentralWidget(m_chat);
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

    connect(m_chat, &ChatPanel::sendRequested, this, &MainWindow::onSendClicked);
    connect(m_chat, &ChatPanel::newChatRequested, this, &MainWindow::onNewChatClicked);
    connect(m_chat, &ChatPanel::stopRequested, this, &MainWindow::onStopClicked);
    connect(m_chat, &ChatPanel::micIndexChanged, this, &MainWindow::onMicSelectionChanged);
    connect(m_chat, &ChatPanel::speakerIndexChanged, this, &MainWindow::onSpeakerSelectionChanged);
    connect(m_chat, &ChatPanel::refreshDevicesRequested, this, &MainWindow::onRefreshMicsClicked);
    connect(m_chat, &ChatPanel::recordToggled, this, &MainWindow::onRecordClicked);
    connect(m_chat, &ChatPanel::listenToggleRequested, this, &MainWindow::onListenToggle);
    connect(m_chat, &ChatPanel::unitSelectionChanged, this, &MainWindow::onUnitComboChanged);
    connect(m_chat, &ChatPanel::topicSelectionChanged, this, &MainWindow::onTopicComboChanged);
    connect(m_chat, &ChatPanel::periodSelectionChanged, this, &MainWindow::onPeriodComboChanged);
    connect(m_chat->languageCombo(), &QComboBox::currentIndexChanged, this,
            [this](int) { refreshContextLine(); });
    connect(m_chat->modeCombo(), &QComboBox::currentIndexChanged, this,
            [this](int) { refreshContextLine(); });
    connect(m_chat->wordLimitCombo(), &QComboBox::currentTextChanged, this,
            [this](const QString &) { refreshContextLine(); });

    if (m_llm.prompts) {
        m_curriculum = new CurriculumController(m_paths, m_llm.prompts.get(), this);
        connect(m_curriculum, &CurriculumController::contextChanged, this,
                &MainWindow::onCurriculumContextChanged);
        connect(m_curriculum, &CurriculumController::warmupRequested, this,
                &MainWindow::onCurriculumWarmupRequested);
        populateCurriculumUi();
        m_curriculum->restoreSettings();
        populateCurriculumUi();
        m_unitContext = m_curriculum->currentContext();
        refreshContextLine();
    } else {
        m_unitContext = UnitContextBuilder::none();
    }

    if (m_llm.backend) {
        connect(m_llm.backend.get(), &OllamaBackend::token, this, &MainWindow::onLlmToken);
        connect(m_llm.backend.get(), &OllamaBackend::finished, this, &MainWindow::onLlmFinished);
        connect(m_llm.backend.get(), &OllamaBackend::failed, this, &MainWindow::onLlmFailed);
        connect(m_llm.backend.get(), &OllamaBackend::probed, this, &MainWindow::onModelsProbed);
        connect(m_llm.backend.get(), &OllamaBackend::warmUpFinished, this,
                &MainWindow::onWarmUpFinished);
    }

    connect(m_stt, &SttEngine::listeningChanged, this, &MainWindow::onRecordingChanged);
    connect(m_stt, &SttEngine::inputLevel, m_chat, &ChatPanel::setMicInputLevel);
    connect(m_stt, &SttEngine::partialText, this, &MainWindow::onPartialTranscription);
    connect(m_stt, &SttEngine::finalText, this, [this](const QString &text, SessionLanguage lang) {
        onTranscriptionReady(text);
        const int idx = m_chat->languageCombo()->findData(
            lang == SessionLanguage::Hindi ? QStringLiteral("hi") : QStringLiteral("en"));
        if (idx >= 0) {
            m_chat->languageCombo()->setCurrentIndex(idx);
        }
    });
    connect(m_stt, &SttEngine::errorOccurred, this, &MainWindow::onSpeechError);
    connect(m_stt, &SttEngine::statusChanged, this, &MainWindow::onSpeechStatus);
    connect(m_piper, &PiperSpeaker::speakingChanged, this, &MainWindow::onPiperSpeakingChanged);
    connect(m_piper, &PiperSpeaker::statusChanged, this, &MainWindow::onPiperStatus);
    connect(m_piper, &PiperSpeaker::errorOccurred, this, &MainWindow::onPiperError);

    refreshMicrophones();
    refreshSpeakers();

    m_chat->addMessageBubble(ChatRole::Assistant,
                             QStringLiteral("Hi! I am Eagle. Choose Doubt mode, pick a model, and ask a question. "
                                            "Make sure Ollama is running on this machine."));
}

MainWindow::~MainWindow()
{
    if (m_curriculum) {
        m_curriculum->saveSettings();
    }
}

QString MainWindow::systemPromptForStartup() const
{
    if (!m_llm.prompts) {
        return {};
    }
    if (m_curriculum) {
        return m_llm.prompts->systemPrompt(m_curriculum->currentContext());
    }
    return m_llm.prompts->systemPrompt(m_unitContext);
}

SessionLanguage MainWindow::currentLanguage() const
{
    const QString code = m_chat->languageCombo()->currentData().toString();
    return code == QStringLiteral("hi") ? SessionLanguage::Hindi : SessionLanguage::English;
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
    if (m_curriculum && m_curriculum->isWarming()) {
        m_retrySendAfterWarm = true;
        m_chat->statusLabel()->setText(
            QStringLiteral("Waiting — Eagle is preparing this unit…"));
        return;
    }
    if (!sendTurnIfReady()) {
        return;
    }
}

bool MainWindow::sendTurnIfReady()
{
    if (!m_llm.backend) {
        return false;
    }
    if (m_llm.backend->isBusy()) {
        m_chat->statusLabel()->setText(
            QStringLiteral("Eagle is still loading the model — try again in a moment."));
        return false;
    }

    const QString text = m_chat->input()->toPlainText().trimmed();
    if (text.isEmpty()) {
        return false;
    }

    m_chat->input()->clear();
    m_chat->addMessageBubble(ChatRole::User, text);

    TurnInputs inputs;
    inputs.mode = currentMode();
    inputs.language = currentLanguage();
    inputs.wordLimit = m_chat->currentWordLimit();
    inputs.question = text;

    m_lastUserTurnMessage = m_llm.prompts->buildUserTurn(inputs, m_unitContext);
    const QVector<LlmMessage> messages =
        m_llm.prompts->buildMessages(inputs, m_unitContext, m_llm.history->messages());

    const int turnId = ++m_turnCounter;
    m_turnSpeech->beginTurn(turnId, currentMode(), currentLanguage(), inputs.wordLimit);
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
    m_chat->statusLabel()->setText(QStringLiteral("Generating…"));
    refreshTurnChrome();
    m_turnTimer.start();
    m_turnSpeech->markRequestSent();
    m_llm.backend->start(req);
    return true;
}

void MainWindow::onNewChatClicked()
{
    if (m_llm.backend && m_llm.backend->isBusy()) {
        m_llm.backend->abort();
    }
    m_turnSpeech->stop();
    m_piper->stop();
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
    m_turnSpeech->stop();
    m_piper->stop();
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
    Q_UNUSED(fullText);
    const bool hitTokenLimit = metrics.doneReason == QLatin1String("length");
    m_turnSpeech->onLlmFinished(hitTokenLimit);
    const QString spoken = m_turnSpeech->spokenAssistantText();
    if (m_chat->activeAssistantBubble()) {
        m_chat->activeAssistantBubble()->setText(spoken);
    }
    const QVector<LlmMessage> historyBeforeAppend = m_llm.history->messages();
    m_llm.history->appendTurn(m_lastUserTurnMessage, spoken, currentLanguage());
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
    if (!m_stt->isBusy()) {
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
        m_llm.estimator->estimateTokens(m_lastUserTurnMessage, currentLanguage());
    if (!historyBeforeAppend.isEmpty()
        && historyBeforeAppend.last().role == QStringLiteral("assistant")) {
        estimatedNew += m_llm.estimator->estimateTokens(historyBeforeAppend.last().content,
                                                        currentLanguage());
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

void MainWindow::syncMicPlaybackMute()
{
    if (!m_stt) {
        return;
    }
    const bool playbackBusy =
        (m_turnSpeech && m_turnSpeech->isAudioPlaybackBusy()) || m_piper->isBusy();
    m_stt->setPlaybackActive(playbackBusy && m_config.audioMuteMicWhileSpeaking());
}

void MainWindow::refreshTurnChrome()
{
    const bool llmBusy = m_llm.backend && m_llm.backend->isBusy();
    const bool turnActive = m_turnSpeech && m_turnSpeech->isTurnActive();
    const bool speechBusy = m_turnSpeech && m_turnSpeech->isSpeechBusy();
    const bool piperLegacyBusy = m_piper->isBusy();
    const bool locked = llmBusy || turnActive;
    syncMicPlaybackMute();

    m_chat->sendButton()->setDisabled(locked);
    m_chat->modelCombo()->setDisabled(locked);
    m_chat->modeCombo()->setDisabled(locked);
    m_chat->languageCombo()->setDisabled(locked);
    m_chat->wordLimitCombo()->setDisabled(locked);
    m_chat->recordButton()->setDisabled(locked || m_stt->isBusy());
    m_chat->micCombo()->setDisabled(locked || m_stt->isListening());
    m_chat->speakerCombo()->setDisabled(locked || piperLegacyBusy);
    m_chat->refreshMicsButton()->setDisabled(locked || m_stt->isListening());
    m_chat->stopButton()->setText(QStringLiteral("Stop generating"));
    m_chat->stopButton()->setVisible(llmBusy || turnActive || speechBusy || piperLegacyBusy);
    if (!m_stt->isBusy() && !turnActive && !speechBusy) {
        if (llmBusy) {
            m_chat->statusLabel()->setText(QStringLiteral("Generating…"));
        }
    } else if (turnActive && !llmBusy && !m_stt->isBusy()) {
        m_chat->statusLabel()->setText(QStringLiteral("Speaking…"));
    }
}

void MainWindow::setGenerating(bool generating)
{
    Q_UNUSED(generating);
    refreshTurnChrome();
}

void MainWindow::refreshMicrophones()
{
    const QByteArray previousId = m_chat->micCombo()->currentData().toByteArray();
    m_chat->micCombo()->blockSignals(true);
    m_chat->micCombo()->clear();

    const QList<QAudioDevice> devices = m_stt->microphones();
    for (const QAudioDevice &device : devices) {
        m_chat->micCombo()->addItem(device.description(), device.id());
    }

    int index = m_chat->micCombo()->findData(previousId);
    if (index < 0) {
        index = 0;
    }
    const bool llmBusy = m_llm.backend && m_llm.backend->isBusy();
    if (m_chat->micCombo()->count() == 0) {
        m_chat->micCombo()->addItem(QStringLiteral("No microphone found"));
        m_chat->recordButton()->setEnabled(false);
    } else {
        m_chat->micCombo()->setCurrentIndex(index);
        m_chat->recordButton()->setEnabled(!llmBusy);
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
    const QList<QAudioDevice> devices = m_stt->microphones();
    for (const QAudioDevice &device : devices) {
        if (device.id() == id) {
            m_stt->setMicrophone(device);
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
            if (m_turnSpeech) {
                m_turnSpeech->setOutputDevice(device);
            }
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
    onListenToggle();
}

void MainWindow::onListenToggle()
{
    if (m_stt->isListening()) {
        m_stt->stopListening();
        return;
    }
    const bool llmBusy = m_llm.backend && m_llm.backend->isBusy();
    const bool eagleSpeaking =
        m_turnSpeech && (m_turnSpeech->isTurnActive() || m_turnSpeech->isSpeechBusy());
    if (llmBusy || eagleSpeaking) {
        onStopClicked();
    }
    onMicSelectionChanged(m_chat->micCombo()->currentIndex());
    m_stt->setLockedReplyLanguage(currentLanguage());
    m_stt->setBiasTerms(m_config.sttBiasTerms());
    m_speechPrefix = m_chat->input()->toPlainText().trimmed();
    m_stt->startListening();
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
    const bool llmBusy = m_llm.backend && m_llm.backend->isBusy();
    m_chat->recordButton()->setEnabled(!llmBusy);
    m_chat->sendButton()->setEnabled(!llmBusy);
}

void MainWindow::onRecordingChanged(bool recording)
{
    m_chat->setMicLevelVisible(recording);
    m_chat->recordButton()->setText(recording ? QStringLiteral("Stop listening") : QStringLiteral("Mic"));
    m_chat->recordButton()->setProperty("recording", recording);
    m_chat->recordButton()->style()->unpolish(m_chat->recordButton());
    m_chat->recordButton()->style()->polish(m_chat->recordButton());
    const bool llmBusy = m_llm.backend && m_llm.backend->isBusy();
    m_chat->micCombo()->setDisabled(recording || llmBusy);
    m_chat->refreshMicsButton()->setDisabled(recording || llmBusy);
    m_chat->sendButton()->setDisabled(recording || llmBusy);
}

void MainWindow::onSpeechError(const QString &message)
{
    m_chat->statusLabel()->setText(QStringLiteral("Speech error"));
    m_chat->addMessageBubble(ChatRole::Assistant, message);
    const bool llmBusy = m_llm.backend && m_llm.backend->isBusy();
    m_chat->recordButton()->setEnabled(!llmBusy);
    m_chat->sendButton()->setEnabled(!llmBusy && !m_stt->isListening());
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
    if (!llmBusy && !m_stt->isBusy()) {
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
    const bool llmBusy = m_llm.backend && m_llm.backend->isBusy();
    const bool locked = status == QStringLiteral("Transcribing…") || m_stt->isListening() || llmBusy;
    m_chat->recordButton()->setDisabled(llmBusy || status == QStringLiteral("Transcribing…"));
    m_chat->sendButton()->setDisabled(locked);
}

void MainWindow::populateCurriculumUi()
{
    if (!m_curriculum) {
        return;
    }
    m_chat->unitCombo()->blockSignals(true);
    m_chat->topicCombo()->blockSignals(true);
    m_chat->periodCombo()->blockSignals(true);

    m_chat->unitCombo()->clear();
    m_chat->unitCombo()->addItem(QStringLiteral("No unit"), QString());
    for (const UnitLibraryEntry &entry : m_curriculum->library().entries()) {
        m_chat->unitCombo()->addItem(entry.displayLabel, entry.pack.id());
    }

    m_chat->periodCombo()->clear();
    for (const ClmPeriodInfo &period : m_curriculum->clm().periods()) {
        m_chat->periodCombo()->addItem(
            QStringLiteral("P%1 %2").arg(period.period).arg(period.name), period.period);
    }
    const int periodIdx =
        m_chat->periodCombo()->findData(m_curriculum->period());
    if (periodIdx >= 0) {
        m_chat->periodCombo()->setCurrentIndex(periodIdx);
    }

    if (m_curriculum->hasUnitSelected()) {
        const int unitIdx =
            m_chat->unitCombo()->findData(m_curriculum->selectedPackId());
        if (unitIdx >= 0) {
            m_chat->unitCombo()->setCurrentIndex(unitIdx);
            refreshTopicComboForUnit(m_curriculum->selectedPackId());
            const int topicIdx = m_chat->topicCombo()->findData(m_curriculum->selectedTopicIndex());
            if (topicIdx >= 0) {
                m_chat->topicCombo()->setCurrentIndex(topicIdx);
            }
        }
    } else {
        m_chat->unitCombo()->setCurrentIndex(0);
        m_chat->topicCombo()->clear();
        m_chat->topicCombo()->setEnabled(false);
    }

    m_chat->unitCombo()->blockSignals(false);
    m_chat->topicCombo()->blockSignals(false);
    m_chat->periodCombo()->blockSignals(false);
}

void MainWindow::refreshTopicComboForUnit(const QString &packId)
{
    m_chat->topicCombo()->blockSignals(true);
    m_chat->topicCombo()->clear();
    const UnitPack pack = m_curriculum->library().packById(packId);
    for (int i = 0; i < pack.topicCount(); ++i) {
        m_chat->topicCombo()->addItem(pack.topicTitle(i), i);
    }
    m_chat->topicCombo()->setEnabled(!pack.id().isEmpty());
    m_chat->topicCombo()->blockSignals(false);
}

void MainWindow::refreshContextLine()
{
    if (!m_curriculum || !m_chat->contextLabel()) {
        return;
    }
    const QString mode = m_chat->modeCombo()->currentText();
    const QString lang = m_chat->languageCombo()->currentText();
    const QString limit = m_chat->wordLimitCombo()->currentText();
    m_chat->contextLabel()->setText(m_curriculum->contextLine(mode, lang, limit));
}

void MainWindow::resetChatForNewUnit(const QString &unitTitle)
{
    m_turnSpeech->stop();
    m_piper->stop();
    if (m_llm.backend && m_llm.backend->isStreamInFlight()) {
        m_llm.backend->abortActiveTurn();
    }
    m_llm.history->reset();
    m_chat->clearConversation();
    m_chat->addMessageBubble(
        ChatRole::Assistant,
        QStringLiteral("New unit: %1").arg(unitTitle));
    m_chat->statusLabel()->setText(QStringLiteral("Ready"));
}

void MainWindow::applySttBiasFromUnit()
{
    QStringList terms = m_config.sttBiasTerms();
    if (m_unitContext.hasUnit && m_curriculum) {
        const UnitPack pack = m_curriculum->library().packById(m_unitContext.packId);
        const QJsonArray topics =
            pack.document().value(QStringLiteral("clm")).toObject().value(QStringLiteral("topics")).toArray();
        for (const QJsonValue &value : topics) {
            const QJsonObject topic = value.toObject();
            if (topic.value(QStringLiteral("id")).toString() != m_unitContext.topicId) {
                continue;
            }
            for (const QJsonValue &term : topic.value(QStringLiteral("key_terms")).toArray()) {
                terms.append(term.toString());
            }
            break;
        }
    }
    m_stt->setBiasTerms(terms);
}

void MainWindow::onUnitComboChanged(int index)
{
    if (!m_curriculum || index < 0) {
        return;
    }
    const QString packId = m_chat->unitCombo()->itemData(index).toString();
    if (packId.isEmpty()) {
        m_curriculum->selectNoUnit();
        m_chat->topicCombo()->clear();
        m_chat->topicCombo()->setEnabled(false);
        return;
    }
    refreshTopicComboForUnit(packId);
    m_curriculum->selectPack(packId, 0);
}

void MainWindow::onTopicComboChanged(int index)
{
    if (!m_curriculum || index < 0 || !m_curriculum->hasUnitSelected()) {
        return;
    }
    const int topicIndex = m_chat->topicCombo()->itemData(index).toInt();
    m_curriculum->selectPack(m_curriculum->selectedPackId(), topicIndex);
}

void MainWindow::onPeriodComboChanged(int index)
{
    if (!m_curriculum || index < 0) {
        return;
    }
    m_curriculum->setPeriod(m_chat->periodCombo()->itemData(index).toInt());
    refreshContextLine();
}

void MainWindow::onCurriculumContextChanged(const UnitContext &context, bool unitOrTopicChanged)
{
    m_unitContext = context;
    applySttBiasFromUnit();
    refreshContextLine();
    if (unitOrTopicChanged) {
        const QString title =
            context.hasUnit ? context.unitTitle : QStringLiteral("General");
        resetChatForNewUnit(title);
    }
}

void MainWindow::onCurriculumWarmupRequested(const QString &systemPrompt, const QString &unitTitle)
{
    if (!m_llm.backend) {
        return;
    }
    if (m_llm.backend->isStreamInFlight() || m_pendingTurnOpen) {
        m_llm.backend->abortActiveTurn();
        m_turnSpeech->stop();
        m_piper->stop();
        m_pendingTurnOpen = false;
        refreshTurnChrome();
    }
    m_curriculum->setWarming(true);
    m_chat->statusLabel()->setText(
        QStringLiteral("Preparing Eagle for %1…").arg(unitTitle));
    m_llm.backend->scheduleWarmUp(m_chat->modelCombo()->currentText(), systemPrompt);
}

void MainWindow::onWarmUpFinished(bool ok)
{
    if (m_curriculum) {
        m_curriculum->setWarming(false);
    }
    if (!ok) {
        m_chat->statusLabel()->setText(QStringLiteral("Warm-up failed — try again"));
        return;
    }
    if (m_retrySendAfterWarm) {
        m_retrySendAfterWarm = false;
        sendTurnIfReady();
        return;
    }
    if (!m_llm.backend || !m_llm.backend->isBusy()) {
        m_chat->statusLabel()->setText(QStringLiteral("Ready"));
    }
}
