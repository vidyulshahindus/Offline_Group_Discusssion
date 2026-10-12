#include "AppConfig.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace {

QJsonValue valueAt(const QJsonObject &root, const QStringList &path)
{
    QJsonValue current = root;
    for (const QString &key : path) {
        if (!current.isObject()) {
            return {};
        }
        current = current.toObject().value(key);
    }
    return current;
}

} // namespace

AppConfig::AppConfig(QObject *parent)
    : QObject(parent)
    , m_document(defaultDocument())
{
}

QJsonObject AppConfig::defaultDocument()
{
    QFile file(QStringLiteral(":/config/eagle_config.defaults.json"));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            return doc.object();
        }
    }

    return QJsonObject{
        {QStringLiteral("config_version"), 1},
        {QStringLiteral("profile"), QStringLiteral("school")},
        {QStringLiteral("llm"),
         QJsonObject{
             {QStringLiteral("backend"), QStringLiteral("ollama")},
             {QStringLiteral("base_url"), QStringLiteral("http://127.0.0.1:11434")},
             {QStringLiteral("model"), QStringLiteral("eagle-clm")},
             {QStringLiteral("fallback_model"), QStringLiteral("qwen3.5:2b-q4_K_M")},
         }},
        {QStringLiteral("stt"),
         QJsonObject{
             {QStringLiteral("server_exe"), QString()},
             {QStringLiteral("model"), QStringLiteral("ggml-base.bin")},
         }},
        {QStringLiteral("tts"),
         QJsonObject{
             {QStringLiteral("piper_exe"), QString()},
             {QStringLiteral("voices_dir"), QString()},
             {QStringLiteral("voices"),
              QJsonObject{
                  {QStringLiteral("en"), QStringLiteral("en_US-lessac-medium")},
                  {QStringLiteral("hi"), QStringLiteral("hi_IN-priyamvada-medium")},
              }},
         }},
        {QStringLiteral("paths"),
         QJsonObject{
             {QStringLiteral("tools_dir"), QString()},
             {QStringLiteral("data_dir"), QString()},
             {QStringLiteral("units_dir"), QString()},
             {QStringLiteral("logs_dir"), QString()},
         }},
    };
}

void AppConfig::ensureDefaults()
{
    const QJsonObject defaults = defaultDocument();
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!m_document.contains(it.key())) {
            m_document.insert(it.key(), it.value());
        }
    }
}

bool AppConfig::load(const QString &filePath)
{
    m_filePath = filePath;
    QFile file(filePath);
    if (!file.exists()) {
        ensureDefaults();
        return save(filePath);
    }
    if (!file.open(QIODevice::ReadOnly)) {
        ensureDefaults();
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        ensureDefaults();
        return false;
    }
    m_document = doc.object();
    ensureDefaults();
    return true;
}

bool AppConfig::save(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(m_document).toJson(QJsonDocument::Indented));
    return true;
}

QString AppConfig::stringAt(const QStringList &path, const QString &fallback) const
{
    const QJsonValue value = valueAt(m_document, path);
    if (value.isString()) {
        return value.toString();
    }
    return fallback;
}

int AppConfig::intAt(const QStringList &path, int fallback) const
{
    const QJsonValue value = valueAt(m_document, path);
    if (value.isDouble()) {
        return value.toInt(fallback);
    }
    return fallback;
}

double AppConfig::doubleAt(const QStringList &path, double fallback) const
{
    const QJsonValue value = valueAt(m_document, path);
    if (value.isDouble()) {
        return value.toDouble(fallback);
    }
    return fallback;
}

QString AppConfig::profile() const
{
    return stringAt({QStringLiteral("profile")}, QStringLiteral("school"));
}

QString AppConfig::llmBaseUrl() const
{
    return stringAt({QStringLiteral("llm"), QStringLiteral("base_url")},
                    QStringLiteral("http://127.0.0.1:11434"));
}

QString AppConfig::llmModel() const
{
    return stringAt({QStringLiteral("llm"), QStringLiteral("model")}, QStringLiteral("eagle-clm"));
}

QString AppConfig::llmFallbackModel() const
{
    return stringAt({QStringLiteral("llm"), QStringLiteral("fallback_model")},
                    QStringLiteral("qwen3.5:2b-q4_K_M"));
}

QString AppConfig::sttServerExe() const
{
    return stringAt({QStringLiteral("stt"), QStringLiteral("server_exe")});
}

QString AppConfig::sttModel() const
{
    return stringAt({QStringLiteral("stt"), QStringLiteral("model")}, QStringLiteral("ggml-base.bin"));
}

QString AppConfig::sttFallbackModel() const
{
    return stringAt({QStringLiteral("stt"), QStringLiteral("fallback_model")},
                    QStringLiteral("ggml-base.bin"));
}

QString AppConfig::sttHost() const
{
    return stringAt({QStringLiteral("stt"), QStringLiteral("host")}, QStringLiteral("127.0.0.1"));
}

int AppConfig::sttPort() const
{
    return intAt({QStringLiteral("stt"), QStringLiteral("port")}, 8178);
}

int AppConfig::sttThreads() const
{
    return intAt({QStringLiteral("stt"), QStringLiteral("threads")}, 0);
}

QString AppConfig::sttLanguageMode() const
{
    return stringAt({QStringLiteral("stt"), QStringLiteral("language_mode")}, QStringLiteral("auto"));
}

bool AppConfig::sttUrduScriptAsHindi() const
{
    return valueAt(m_document, {QStringLiteral("stt"), QStringLiteral("urdu_script_as_hindi")})
        .toBool(true);
}

QString AppConfig::sttDevanagariLang() const
{
    return stringAt({QStringLiteral("stt"), QStringLiteral("devanagari_lang")}, QStringLiteral("hi"));
}

QStringList AppConfig::sttBiasTerms() const
{
    QStringList out;
    const QJsonArray arr =
        valueAt(m_document, {QStringLiteral("stt"), QStringLiteral("bias_terms")}).toArray();
    for (const QJsonValue &v : arr) {
        out.append(v.toString());
    }
    return out;
}

int AppConfig::sttVadFrameMs() const
{
    return intAt({QStringLiteral("stt"), QStringLiteral("vad"), QStringLiteral("frame_ms")}, 30);
}

double AppConfig::sttVadStartThresholdDb() const
{
    return doubleAt({QStringLiteral("stt"), QStringLiteral("vad"), QStringLiteral("start_threshold_db")},
                    9.0);
}

int AppConfig::sttVadEndHangoverMs() const
{
    return intAt({QStringLiteral("stt"), QStringLiteral("vad"), QStringLiteral("end_hangover_ms")},
                 700);
}

int AppConfig::sttVadMinSegmentMs() const
{
    return intAt({QStringLiteral("stt"), QStringLiteral("vad"), QStringLiteral("min_segment_ms")},
                 500);
}

int AppConfig::sttVadMaxSegmentMs() const
{
    return intAt({QStringLiteral("stt"), QStringLiteral("vad"), QStringLiteral("max_segment_ms")},
                 12000);
}

int AppConfig::sttVadPreRollMs() const
{
    return intAt({QStringLiteral("stt"), QStringLiteral("vad"), QStringLiteral("pre_roll_ms")}, 200);
}

bool AppConfig::audioMuteMicWhileSpeaking() const
{
    return valueAt(m_document, {QStringLiteral("audio"), QStringLiteral("mute_mic_while_speaking")})
        .toBool(true);
}

QString AppConfig::ttsPiperExe() const
{
    return stringAt({QStringLiteral("tts"), QStringLiteral("piper_exe")});
}

QString AppConfig::ttsVoicesDir() const
{
    return stringAt({QStringLiteral("tts"), QStringLiteral("voices_dir")});
}

QString AppConfig::ttsVoiceEn() const
{
    return stringAt({QStringLiteral("tts"), QStringLiteral("voices"), QStringLiteral("en")},
                    QStringLiteral("en_US-lessac-medium"));
}

QString AppConfig::ttsVoiceHi() const
{
    return stringAt({QStringLiteral("tts"), QStringLiteral("voices"), QStringLiteral("hi")},
                    QStringLiteral("hi_IN-priyamvada-medium"));
}

QString AppConfig::ttsBackend() const
{
    return stringAt({QStringLiteral("tts"), QStringLiteral("backend")}, QStringLiteral("piper_cli"));
}

double AppConfig::ttsLengthScaleDefault() const
{
    return doubleAt({QStringLiteral("tts"), QStringLiteral("length_scale"), QStringLiteral("default")},
                    1.1);
}

double AppConfig::ttsLengthScaleHi() const
{
    return doubleAt({QStringLiteral("tts"), QStringLiteral("length_scale"), QStringLiteral("hi")},
                    1.15);
}

int AppConfig::ttsFillerAfterMs() const
{
    return intAt({QStringLiteral("tts"), QStringLiteral("filler_after_ms")}, 3500);
}

bool AppConfig::ttsFillersEnabled() const
{
    return valueAt(m_document, {QStringLiteral("tts"), QStringLiteral("fillers_enabled")})
        .toBool(true);
}

double AppConfig::ttsVolume() const
{
    return doubleAt({QStringLiteral("tts"), QStringLiteral("volume")}, 0.7);
}

double AppConfig::ttsSentenceSilenceS() const
{
    return doubleAt({QStringLiteral("tts"), QStringLiteral("sentence_silence_s")}, 0.2);
}

QString AppConfig::audioPlaybackBackend() const
{
    return stringAt({QStringLiteral("audio"), QStringLiteral("backend")},
                    QStringLiteral("mediaplayer"));
}

QString AppConfig::pathsToolsDir() const
{
    return stringAt({QStringLiteral("paths"), QStringLiteral("tools_dir")});
}

QString AppConfig::pathsDataDir() const
{
    return stringAt({QStringLiteral("paths"), QStringLiteral("data_dir")});
}

QString AppConfig::pathsUnitsDir() const
{
    return stringAt({QStringLiteral("paths"), QStringLiteral("units_dir")});
}

QString AppConfig::pathsLogsDir() const
{
    return stringAt({QStringLiteral("paths"), QStringLiteral("logs_dir")});
}

bool AppConfig::schoolAvoidMetaSchoolWords() const
{
    return valueAt(m_document, {QStringLiteral("school"), QStringLiteral("avoid_meta_school_words")})
        .toBool(true);
}

bool AppConfig::llmFirstSentenceHint() const
{
    return valueAt(m_document, {QStringLiteral("llm"), QStringLiteral("first_sentence_hint")}).toBool(true);
}

qint64 AppConfig::llmKeepAlive() const
{
    return intAt({QStringLiteral("llm"), QStringLiteral("keep_alive")}, -1);
}

int AppConfig::llmNumCtx() const
{
    return intAt({QStringLiteral("llm"), QStringLiteral("num_ctx")}, 4096);
}

int AppConfig::llmNumThread() const
{
    return intAt({QStringLiteral("llm"), QStringLiteral("num_thread")}, 0);
}

int AppConfig::llmNumBatch() const
{
    return intAt({QStringLiteral("llm"), QStringLiteral("num_batch")}, 256);
}

int AppConfig::llmRequestTimeoutMs() const
{
    return intAt({QStringLiteral("llm"), QStringLiteral("request_timeout_ms")}, 60000);
}

int AppConfig::llmHistoryBudgetTokens() const
{
    return intAt({QStringLiteral("llm"), QStringLiteral("history_budget_tokens")}, 2200);
}

int AppConfig::llmHistoryTrimTokens() const
{
    return intAt({QStringLiteral("llm"), QStringLiteral("history_trim_to_tokens")}, 1400);
}

int AppConfig::llmMaxNewTurnTokens() const
{
    return intAt({QStringLiteral("llm"), QStringLiteral("max_new_turn_tokens")}, 350);
}

double AppConfig::llmTopP() const
{
    return doubleAt({QStringLiteral("llm"), QStringLiteral("top_p")}, 0.9);
}

int AppConfig::llmTopK() const
{
    return intAt({QStringLiteral("llm"), QStringLiteral("top_k")}, 40);
}

double AppConfig::llmRepeatPenalty() const
{
    return doubleAt({QStringLiteral("llm"), QStringLiteral("repeat_penalty")}, 1.1);
}

double AppConfig::llmTemperatureFor(const QString &modeKey) const
{
    const QJsonObject temps =
        valueAt(m_document, {QStringLiteral("llm"), QStringLiteral("temperature")}).toObject();
    return temps.value(modeKey).toDouble(0.6);
}
