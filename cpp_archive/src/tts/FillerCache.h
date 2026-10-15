#pragma once

#include "PromptTypes.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

class AppConfig;
class Paths;
class PiperCliBackend;

class FillerCache
{
public:
    bool load(const AppConfig &config, const Paths &paths);
    bool warmUp(PiperCliBackend &enWorker, PiperCliBackend &hiWorker);

    QString wavPathFor(SessionLanguage language, int index) const;
    int count(SessionLanguage language) const;
    int pickIndex(SessionLanguage language, int avoidIndex) const;

    bool isReady() const { return m_ready; }

private:
    QString cacheKey(SessionLanguage language, int index) const;
    QString cacheFilePath(const QString &voiceId, double lengthScale, const QString &text) const;
    bool synthesizeToCache(PiperCliBackend &worker, const QString &text, const QString &wavPath);

    QStringList m_enFillers;
    QStringList m_hiFillers;
    QHash<QString, QString> m_paths;
    QString m_enVoice;
    QString m_hiVoice;
    QString m_cacheRoot;
    double m_enScale = 1.1;
    double m_hiScale = 1.15;
    bool m_ready = false;
};
