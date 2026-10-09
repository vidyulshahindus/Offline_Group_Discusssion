#include "TtsEngine.h"

#include "AppConfig.h"
#include "Paths.h"

TtsEngine::TtsEngine(const AppConfig &config, const Paths &paths, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_paths(paths)
{
    auto connectWorker = [this](PiperCliBackend &worker) {
        connect(&worker, &PiperCliBackend::wavReady, this,
                [this](int turnId, int sentenceIndex, const QString &wavPath, qint64 synthMs) {
                    if (turnId >= 0) {
                        emit firstTtsDone(turnId, sentenceIndex);
                    }
                    emit wavReady(turnId, sentenceIndex, wavPath, synthMs);
                });
        connect(&worker, &PiperCliBackend::synthesisFailed, this,
                [this](int turnId, int sentenceIndex, const QString &error) {
                    emit synthesisFailed(turnId, sentenceIndex, error);
                    if (turnId >= 0) {
                        emit errorOccurred(error);
                    }
                });
    };
    connectWorker(m_en);
    connectWorker(m_hi);
}

void TtsEngine::startSession()
{
    ensureWorker(SessionLanguage::English);
    ensureWorker(SessionLanguage::Hindi);
    m_en.startWorker();
    m_hi.startWorker();
    if (m_config.ttsFillersEnabled()) {
        m_fillers.load(m_config, m_paths);
        m_fillers.warmUp(m_en, m_hi);
    }
}

void TtsEngine::stop()
{
    m_en.stopWorker();
    m_hi.stopWorker();
}

QString TtsEngine::voiceFor(SessionLanguage language) const
{
    return language == SessionLanguage::Hindi ? m_config.ttsVoiceHi() : m_config.ttsVoiceEn();
}

void TtsEngine::ensureWorker(SessionLanguage language)
{
    PiperCliBackend &worker = language == SessionLanguage::Hindi ? m_hi : m_en;
    if (worker.isConfigured()) {
        return;
    }
    const QString voice = voiceFor(language);
    const QString onnx = m_paths.piperVoiceOnnx(voice);
    const double scale = language == SessionLanguage::Hindi
                             ? m_config.ttsLengthScaleHi()
                             : m_config.ttsLengthScaleDefault();
    worker.configure(m_paths.piperExecutable(), onnx, scale);
}

void TtsEngine::speakSentence(int turnId, int sentenceIndex, const QString &text,
                              SessionLanguage language)
{
    m_activeLanguage = language;
    ensureWorker(language);
    PiperCliBackend &worker = language == SessionLanguage::Hindi ? m_hi : m_en;
    worker.startWorker();
    worker.synthesize(turnId, sentenceIndex, text);
}

bool TtsEngine::isBusy() const
{
    return m_en.hasPendingJobs() || m_hi.hasPendingJobs();
}

void TtsEngine::speakTestPhrase(SessionLanguage language, int phraseIndex, const QString &text)
{
    ensureWorker(language);
    PiperCliBackend &worker = language == SessionLanguage::Hindi ? m_hi : m_en;
    worker.startWorker();
    worker.synthesize(-1, phraseIndex, text);
}

qint64 TtsEngine::lastColdSynthMs(SessionLanguage language) const
{
    const PiperCliBackend &worker = language == SessionLanguage::Hindi ? m_hi : m_en;
    return worker.lastColdSynthMs();
}

qint64 TtsEngine::lastWarmSynthMs(SessionLanguage language) const
{
    const PiperCliBackend &worker = language == SessionLanguage::Hindi ? m_hi : m_en;
    return worker.lastWarmSynthMs();
}
