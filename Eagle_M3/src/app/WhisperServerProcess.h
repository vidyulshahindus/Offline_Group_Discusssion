#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QUrl>

class WhisperServerClient;

// Runs whisper.cpp `whisper-server` as a child process (spec §10.1, §6.2 ServiceSupervisor):
// start, wait for GET /health, restart on crash with backoff, kill on exit.
class WhisperServerProcess : public QObject
{
    Q_OBJECT

public:
    enum class State { Stopped, Starting, Ready, Failed };
    Q_ENUM(State)

    struct Settings {
        QString exe;
        QString model;
        QString host = QStringLiteral("127.0.0.1");
        int port = 8178;
        int threads = 4;
        QString language = QStringLiteral("en");
        QStringList extraArgs;
        QString logFile;
        int startTimeoutMs = 90000;
    };

    explicit WhisperServerProcess(QObject *parent = nullptr);
    ~WhisperServerProcess() override;

    void start(const Settings &settings);
    void restart(const Settings &settings);
    void stop();

    State state() const { return m_state; }
    QString detail() const { return m_detail; }
    QUrl baseUrl() const;
    bool adopted() const { return m_adopted; }
    const Settings &settings() const { return m_settings; }

    static QStringList buildArguments(const Settings &settings);

signals:
    void stateChanged(WhisperServerProcess::State state, const QString &detail);

private:
    void launch();
    void pollHealth();
    void onHealthChecked(bool ok, int httpStatus, const QString &detail);
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void scheduleRestart(const QString &why);
    void setState(State state, const QString &detail);
    void killProcess(bool wait);
    QString logTail(int maxChars = 600) const;

    Settings m_settings;
    QProcess *m_process = nullptr;
    WhisperServerClient *m_health = nullptr;
    QTimer m_healthTimer;
    QTimer m_restartTimer;
    QElapsedTimer m_startClock;
    QList<qint64> m_crashTimes;
    QElapsedTimer m_uptime;
    State m_state = State::Stopped;
    QString m_detail;
    int m_backoffMs = 1000;
    bool m_stopping = false;
    bool m_restartPending = false;
    bool m_adopted = false;
    bool m_probingExisting = false;
    bool m_healthInFlight = false;
};
