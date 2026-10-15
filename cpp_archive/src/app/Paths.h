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

    QString whisperCli() const;
    QString whisperServerExe() const;
    QString whisperModel(const QString &modelFile) const;
    QString resolvedWhisperModel() const;
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
