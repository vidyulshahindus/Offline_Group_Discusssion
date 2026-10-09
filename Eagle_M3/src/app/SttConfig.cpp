#include "SttConfig.h"

#include "AppConfig.h"
#include "Paths.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>

namespace {

QStringList toStringList(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &v : value.toArray()) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty()) {
            out.append(s);
        }
    }
    return out;
}

int intIn(const QJsonObject &obj, const char *key, int fallback)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    return v.isDouble() ? v.toInt(fallback) : fallback;
}

double doubleIn(const QJsonObject &obj, const char *key, double fallback)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble(fallback) : fallback;
}

QStringList specHallucinations()
{
    return {QStringLiteral("Thank you."), QStringLiteral("Thanks for watching!"),
            QStringLiteral("Thank you for watching."),
            QStringLiteral("Subtitles by the Amara.org community"), QStringLiteral("[Music]"),
            QStringLiteral("(upbeat music)"), QStringLiteral("[BLANK_AUDIO]"), QStringLiteral("you")};
}

} // namespace

int SttConfig::autoThreads()
{
    return qBound(2, QThread::idealThreadCount() / 2, 8);
}

QString SttConfig::whisperLanguageFor(const QString &uiLanguageCode)
{
    const QString code = uiLanguageCode.trimmed().toLower();
    return code.isEmpty() ? QStringLiteral("auto") : code;
}

SttSettings SttConfig::engineSettings(const AppConfig &config)
{
    SttSettings s;
    s.serverUrl = QUrl(QStringLiteral("http://%1:%2").arg(config.sttHost()).arg(config.sttPort()));
    s.languageLock = whisperLanguageFor(config.sttLanguageMode());

    const QJsonObject stt = config.valueAt({QStringLiteral("stt")}).toObject();
    s.biasTerms = toStringList(stt.value(QStringLiteral("bias_terms")));
    s.hallucinations = toStringList(stt.value(QStringLiteral("hallucinations")));
    if (s.hallucinations.isEmpty()) {
        LanguageOptions ignored;
        LanguageService::loadHinglishResource(ignored, &s.hallucinations);
    }
    if (s.hallucinations.isEmpty()) {
        s.hallucinations = specHallucinations();
    }
    s.requestTimeoutMs = intIn(stt, "request_timeout_ms", s.requestTimeoutMs);
    s.drainTimeoutMs = intIn(stt, "drain_timeout_ms", s.drainTimeoutMs);
    s.flushMinMs = intIn(stt, "flush_min_ms", s.flushMinMs);
    s.startupRetryMs = intIn(stt, "startup_timeout_ms", 90000);
    s.autoSubmitOnSilenceMs = intIn(stt, "auto_submit_on_silence_ms", 0);
    s.useBiasPrompt = stt.value(QStringLiteral("use_unit_terms_as_prompt")).toBool(true);
    s.muteWhileSpeaking = config.audioMuteMicWhileSpeaking();
    s.urduScriptAsHindi = stt.value(QStringLiteral("urdu_script_as_hindi")).toBool(true);

    const QJsonObject vad = stt.value(QStringLiteral("vad")).toObject();
    s.vad.frameMs = intIn(vad, "frame_ms", s.vad.frameMs);
    s.vad.startThresholdDb = doubleIn(vad, "start_threshold_db", s.vad.startThresholdDb);
    s.vad.endHangoverMs = intIn(vad, "end_hangover_ms", s.vad.endHangoverMs);
    s.vad.minSegmentMs = intIn(vad, "min_segment_ms", s.vad.minSegmentMs);
    s.vad.maxSegmentMs = intIn(vad, "max_segment_ms", s.vad.maxSegmentMs);
    s.vad.preRollMs = intIn(vad, "pre_roll_ms", s.vad.preRollMs);
    s.vad.tailKeepMs = intIn(vad, "tail_keep_ms", s.vad.tailKeepMs);
    s.vad.calibrationMs = intIn(vad, "calibration_ms", s.vad.calibrationMs);
    s.vad.noiseAlpha = doubleIn(vad, "noise_alpha", s.vad.noiseAlpha);
    return s;
}

WhisperServerProcess::Settings SttConfig::serverSettings(const AppConfig &config, const Paths &paths,
                                                        const QString &language)
{
    WhisperServerProcess::Settings s;
    s.exe = paths.whisperServer();
    s.model = paths.whisperModel();
    s.host = config.sttHost();
    s.port = config.sttPort();
    s.threads = config.sttThreads() > 0 ? config.sttThreads() : autoThreads();
    s.language = whisperLanguageFor(language);
    const QJsonObject stt = config.valueAt({QStringLiteral("stt")}).toObject();
    s.extraArgs = toStringList(stt.value(QStringLiteral("extra_args")));
    s.startTimeoutMs = intIn(stt, "startup_timeout_ms", s.startTimeoutMs);
    s.logFile = QDir(paths.logsDir()).filePath(QStringLiteral("whisper-server.log"));
    return s;
}

LanguageOptions SttConfig::languageOptions(const AppConfig &config)
{
    LanguageOptions o;
    const QJsonObject stt = config.valueAt({QStringLiteral("stt")}).toObject();
    o.urduScriptAsHindi = stt.value(QStringLiteral("urdu_script_as_hindi")).toBool(true);
    o.devanagariLang = stt.value(QStringLiteral("devanagari_lang")).toString(QStringLiteral("hi"));
    o.hinglishReply = stt.value(QStringLiteral("hinglish_reply")).toString(QStringLiteral("hindi"));
    LanguageService::loadHinglishResource(o);

    // A language can be the reply language only if a voice is configured for it.
    // M3 speaks English and Hindi (the prompt layer supports only these two).
    o.voicedLanguages.clear();
    const QJsonObject voices =
        config.valueAt({QStringLiteral("tts"), QStringLiteral("voices")}).toObject();
    for (const QString &code : {QStringLiteral("en"), QStringLiteral("hi")}) {
        if (!voices.value(code).toString().trimmed().isEmpty()) {
            o.voicedLanguages.append(code);
        }
    }
    if (!o.voicedLanguages.contains(QStringLiteral("en"))) {
        o.voicedLanguages.append(QStringLiteral("en"));
    }
    return o;
}
