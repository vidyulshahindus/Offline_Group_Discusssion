#pragma once

#include "stt/SttEngine.h"

#include <QObject>
#include <QString>

class AppConfig;
class ChatPanel;
class QMediaDevices;
class ServiceSupervisor;

// UI side of speech input (spec §10.3, §19.2): the Listen button, microphone
// picker and hot-plug, the level meter, and the live transcript in the input box.
// Barge-in (stopping Eagle before listening) is decided by MainWindow.
class ListenController : public QObject
{
    Q_OBJECT

public:
    ListenController(const AppConfig &config, ServiceSupervisor &supervisor, ChatPanel *chat,
                     QObject *parent = nullptr);

    SttEngine &engine() { return m_engine; }
    bool isListening() const { return m_engine.isListening(); }
    bool isBusy() const { return m_engine.isBusy(); }

    void refreshMicrophones();
    void selectMicrophone(int index);
    void setLanguage(const QString &code);
    void setOutputActive(bool active) { m_engine.setOutputActive(active); }

    bool startListening();
    // sendWhenFinal: emit sendRequested() once the final transcript is in the box.
    void stopListening(bool sendWhenFinal);
    void cancel();

signals:
    void sendRequested();
    void statusMessage(const QString &text);
    void errorMessage(const QString &text);
    void busyChanged(bool busy);

private:
    void onStateChanged(SttEngine::State state);
    void onTranscript(const QString &text, bool speechInProgress);
    void onFinal(const QString &text, const QString &language, qint64 stopToFinalMs);
    void showText(const QString &text, bool speechInProgress);
    void updateButton();

    const AppConfig &m_config;
    ServiceSupervisor &m_supervisor;
    ChatPanel *m_chat = nullptr;
    SttEngine m_engine;
    QMediaDevices *m_devices = nullptr;
    QString m_prefix;
    bool m_sendWhenFinal = false;
    bool m_appliedConfigDevice = false;
};
