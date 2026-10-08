#pragma once

#include <QJsonObject>
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
    QString ttsPiperExe() const;
    QString ttsVoicesDir() const;
    QString ttsVoiceEn() const;
    QString ttsVoiceHi() const;
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

    static QJsonObject defaultDocument();

signals:
    void changed(const QString &section);

private:
    void ensureDefaults();
    QString stringAt(const QStringList &path, const QString &fallback = {}) const;
    int intAt(const QStringList &path, int fallback) const;
    double doubleAt(const QStringList &path, double fallback) const;

    QString m_filePath;
    QJsonObject m_document;
};
