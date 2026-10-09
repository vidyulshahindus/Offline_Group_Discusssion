#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>

class AppConfig : public QObject
{
    Q_OBJECT

public:
    explicit AppConfig(QObject *parent = nullptr);

    bool load(const QString &filePath);
    bool save(const QString &filePath) const;

    QString filePath() const { return m_filePath; }
    QJsonObject json() const { return m_document; }

    QString profile() const;
    QString llmBaseUrl() const;
    QString llmModel() const;
    QString llmFallbackModel() const;
    QString sttServerExe() const;
    QString sttModel() const;
    QString sttFallbackModel() const;
    QString sttHost() const;
    int sttPort() const;
    int sttThreads() const;
    QString sttLanguageMode() const;
    bool audioMuteMicWhileSpeaking() const;
    QString audioInputDevice() const;
    QString ttsPiperExe() const;
    QString ttsVoicesDir() const;
    QString ttsVoiceEn() const;
    QString ttsVoiceHi() const;
    QString ttsBackend() const;
    double ttsLengthScaleDefault() const;
    double ttsLengthScaleHi() const;
    int ttsFillerAfterMs() const;
    bool ttsFillersEnabled() const;
    double ttsVolume() const;
    double ttsSentenceSilenceS() const;
    QString audioPlaybackBackend() const;
    QString pathsToolsDir() const;
    QString pathsDataDir() const;
    QString pathsUnitsDir() const;
    QString pathsLogsDir() const;

    bool schoolAvoidMetaSchoolWords() const;
    bool llmFirstSentenceHint() const;
    qint64 llmKeepAlive() const;
    int llmNumCtx() const;
    int llmNumThread() const;
    int llmNumBatch() const;
    int llmRequestTimeoutMs() const;
    int llmHistoryBudgetTokens() const;
    int llmHistoryTrimTokens() const;
    int llmMaxNewTurnTokens() const;
    double llmTopP() const;
    int llmTopK() const;
    double llmRepeatPenalty() const;
    double llmTemperatureFor(const QString &modeKey) const;

    // Generic read access for modules with many keys (e.g. SttConfig).
    QJsonValue valueAt(const QStringList &path) const;

    static QJsonObject defaultDocument();
    // Adds keys that exist in defaults but not in doc, at every depth.
    // Existing values are never changed. Returns the number of keys added.
    static int mergeMissing(QJsonObject &doc, const QJsonObject &defaults);

signals:
    void changed(const QString &section);

private:
    int ensureDefaults();
    QString stringAt(const QStringList &path, const QString &fallback = {}) const;
    int intAt(const QStringList &path, int fallback) const;
    double doubleAt(const QStringList &path, double fallback) const;

    QString m_filePath;
    QJsonObject m_document;
};
