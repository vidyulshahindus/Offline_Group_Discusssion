#include "ListenController.h"

#include "AppConfig.h"
#include "ChatPanel.h"
#include "ServiceSupervisor.h"
#include "SttConfig.h"

#include <QAudioDevice>
#include <QComboBox>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QTextCursor>
#include <QTextEdit>

Q_LOGGING_CATEGORY(lcListen, "eagle.listen")

ListenController::ListenController(const AppConfig &config, ServiceSupervisor &supervisor,
                                   ChatPanel *chat, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_supervisor(supervisor)
    , m_chat(chat)
    , m_devices(new QMediaDevices(this))
{
    SttSettings settings = SttConfig::engineSettings(config);
    settings.serverUrl = supervisor.whisperUrl();
    m_engine.configure(settings);

    connect(&m_engine, &SttEngine::stateChanged, this, &ListenController::onStateChanged);
    connect(&m_engine, &SttEngine::transcriptUpdated, this, &ListenController::onTranscript);
    connect(&m_engine, &SttEngine::finalText, this, &ListenController::onFinal);
    connect(&m_engine, &SttEngine::level, this, [this](float level) {
        m_chat->micLevelBar()->setValue(int(level * 100.0f));
    });
    connect(&m_engine, &SttEngine::errorOccurred, this, &ListenController::errorMessage);
    connect(&m_engine, &SttEngine::statusChanged, this, &ListenController::statusMessage);
    connect(&m_engine, &SttEngine::autoSubmitRequested, this, [this]() { stopListening(true); });
    connect(m_devices, &QMediaDevices::audioInputsChanged, this, &ListenController::refreshMicrophones);
    connect(m_chat, &ChatPanel::micIndexChanged, this, &ListenController::selectMicrophone);

    refreshMicrophones();
    updateButton();
}

void ListenController::refreshMicrophones()
{
    QComboBox *combo = m_chat->micCombo();
    const QByteArray previousId = combo->currentData().toByteArray();
    const QList<QAudioDevice> devices = SttEngine::microphones();

    combo->blockSignals(true);
    combo->clear();
    for (const QAudioDevice &device : devices) {
        combo->addItem(device.description(), device.id());
    }

    int index = -1;
    if (!m_appliedConfigDevice) {
        m_appliedConfigDevice = true;
        const QString preferred = m_config.audioInputDevice().trimmed();
        if (!preferred.isEmpty()) {
            index = combo->findData(preferred.toUtf8());
            if (index < 0) {
                index = combo->findText(preferred);
            }
        }
    }
    if (index < 0 && !previousId.isEmpty()) {
        index = combo->findData(previousId);
    }
    const bool lost = !previousId.isEmpty() && combo->findData(previousId) < 0;
    if (index < 0) {
        index = qMax(0, combo->findData(QMediaDevices::defaultAudioInput().id()));
    }
    if (devices.isEmpty()) {
        combo->addItem(QStringLiteral("No microphone found"));
    } else {
        combo->setCurrentIndex(index);
    }
    combo->blockSignals(false);

    if (!devices.isEmpty()) {
        selectMicrophone(combo->currentIndex());
    }
    if (lost) {
        emit statusMessage(devices.isEmpty()
                               ? QStringLiteral("Microphone disconnected")
                               : QStringLiteral("Microphone disconnected; using %1").arg(combo->currentText()));
    }
    updateButton();
}

void ListenController::selectMicrophone(int index)
{
    if (index < 0) {
        return;
    }
    const QByteArray id = m_chat->micCombo()->itemData(index).toByteArray();
    for (const QAudioDevice &device : SttEngine::microphones()) {
        if (device.id() == id) {
            m_engine.setDevice(device);
            return;
        }
    }
}

void ListenController::setLanguage(const QString &code)
{
    m_engine.setLanguageLock(SttConfig::whisperLanguageFor(code));
}

bool ListenController::startListening()
{
    if (m_engine.isBusy()) {
        return false;
    }
    const WhisperServerProcess::State serverState = m_supervisor.whisperState();
    if (serverState == WhisperServerProcess::State::Failed
        || serverState == WhisperServerProcess::State::Stopped) {
        emit errorMessage(QStringLiteral("Speech to text is not available: %1\nOpen Readiness "
                                         "(Ctrl+Shift+R) for details.")
                              .arg(m_supervisor.whisperDetail()));
        return false;
    }
    m_prefix = m_chat->input()->toPlainText().trimmed();
    m_sendWhenFinal = false;
    if (!m_engine.startListening()) {
        return false;
    }
    if (serverState == WhisperServerProcess::State::Starting) {
        emit statusMessage(QStringLiteral("Listening… (speech recogniser still loading)"));
    }
    return true;
}

void ListenController::stopListening(bool sendWhenFinal)
{
    if (m_engine.state() == SttEngine::State::Listening) {
        m_sendWhenFinal = sendWhenFinal;
        m_engine.stopListening();
    } else if (m_engine.state() == SttEngine::State::Finishing) {
        m_sendWhenFinal = m_sendWhenFinal || sendWhenFinal;
    }
}

void ListenController::cancel()
{
    m_sendWhenFinal = false;
    m_engine.cancel();
}

void ListenController::onStateChanged(SttEngine::State state)
{
    const bool listening = state == SttEngine::State::Listening;
    m_chat->input()->setReadOnly(state != SttEngine::State::Idle);
    m_chat->micLevelBar()->setVisible(listening);
    if (!listening) {
        m_chat->micLevelBar()->setValue(0);
    }
    updateButton();
    emit busyChanged(state != SttEngine::State::Idle);
}

void ListenController::onTranscript(const QString &text, bool speechInProgress)
{
    if (m_engine.state() != SttEngine::State::Idle) {
        showText(text, speechInProgress);
    }
}

void ListenController::showText(const QString &text, bool speechInProgress)
{
    QString combined = m_prefix;
    if (!combined.isEmpty() && !text.isEmpty()) {
        combined += QLatin1Char(' ');
    }
    combined += text;
    if (speechInProgress) {
        combined += combined.isEmpty() ? QStringLiteral("…") : QStringLiteral(" …");
    }
    QTextEdit *input = m_chat->input();
    input->setPlainText(combined);
    input->moveCursor(QTextCursor::End);
}

void ListenController::onFinal(const QString &text, const QString &language, qint64 stopToFinalMs)
{
    showText(text, false);
    m_chat->input()->setFocus();
    qCInfo(lcListen) << "final transcript" << text.size() << "chars, lang" << language
                     << "stopToFinalMs" << stopToFinalMs;
    if (text.isEmpty()) {
        emit statusMessage(QStringLiteral("No speech detected. Try again a little closer to the microphone."));
    } else {
        emit statusMessage(QStringLiteral("Heard you · text ready %1 s after Stop")
                               .arg(double(stopToFinalMs) / 1000.0, 0, 'f', 1));
    }
    const bool send = m_sendWhenFinal && !m_chat->input()->toPlainText().trimmed().isEmpty();
    m_sendWhenFinal = false;
    if (send) {
        emit sendRequested();
    }
}

void ListenController::updateButton()
{
    QPushButton *button = m_chat->recordButton();
    const bool haveMic = !SttEngine::microphones().isEmpty();
    bool recording = false;
    switch (m_engine.state()) {
    case SttEngine::State::Idle:
        button->setText(QStringLiteral("Listen"));
        button->setEnabled(haveMic);
        break;
    case SttEngine::State::Listening:
        button->setText(QStringLiteral("Stop listening"));
        button->setEnabled(true);
        recording = true;
        break;
    case SttEngine::State::Finishing:
        button->setText(QStringLiteral("Transcribing…"));
        button->setEnabled(false);
        break;
    }
    button->setProperty("recording", recording);
    button->style()->unpolish(button);
    button->style()->polish(button);
    m_chat->micCombo()->setDisabled(m_engine.isBusy() || !haveMic);
}
