#pragma once

#include <QString>
#include <QStringList>

class AppConfig;

class Paths
{
public:
    explicit Paths(const AppConfig &config);

    QString applicationDir() const;
    QString toolsDir() const;
    QString dataDir() const;
    QString unitsDir() const;
    QString logsDir() const;
    QString configFilePath() const;

    // Single source of truth for STT files (spec §0 rule 9, M3).
    QString whisperServer() const;
    // Configured stt.model, else stt.fallback_model. Empty if neither exists.
    QString whisperModel() const;
    // modelFile first, then stt.fallback_model.
    QString whisperModel(const QString &modelFile) const;
    QStringList whisperModelCandidates(const QString &modelFile) const;
    QString piperExecutable() const;
    QString piperVoicesDir() const;
    QString piperVoiceOnnx(const QString &voiceId) const;

    void logResolvedPaths() const;

private:
    QString resolveToolsDir() const;
    QString firstExisting(const QStringList &candidates) const;
    QString withNativeExeName(const QString &baseName) const;

    const AppConfig &m_config;
    QString m_toolsDir;
    QString m_dataDir;
    QString m_unitsDir;
    QString m_logsDir;
};
