#pragma once

#include "FillerCache.h"
#include "PiperCliBackend.h"
#include "PromptTypes.h"

#include <QObject>

class AppConfig;
class Paths;

class TtsEngine : public QObject
{
    Q_OBJECT

public:
    explicit TtsEngine(const AppConfig &config, const Paths &paths, QObject *parent = nullptr);

    void startSession();
    void stop();
    void speakSentence(int turnId, int sentenceIndex, const QString &text, SessionLanguage language);
    void speakTestPhrase(SessionLanguage language, int phraseIndex, const QString &text);
    bool isBusy() const;

    const FillerCache &fillers() const { return m_fillers; }
    qint64 lastColdSynthMs(SessionLanguage language) const;
    qint64 lastWarmSynthMs(SessionLanguage language) const;

signals:
    void wavReady(int turnId, int sentenceIndex, const QString &wavPath, qint64 synthMs);
    void firstTtsDone(int turnId, int sentenceIndex);
    void synthesisFailed(int turnId, int sentenceIndex, const QString &error);
    void errorOccurred(const QString &message);

private:
    QString voiceFor(SessionLanguage language) const;
    void ensureWorker(SessionLanguage language);

    const AppConfig &m_config;
    const Paths &m_paths;
    PiperCliBackend m_en;
    PiperCliBackend m_hi;
    FillerCache m_fillers;
    SessionLanguage m_activeLanguage = SessionLanguage::English;
};
