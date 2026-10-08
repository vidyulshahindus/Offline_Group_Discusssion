#include "AppConfig.h"

#include <QFile>
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
