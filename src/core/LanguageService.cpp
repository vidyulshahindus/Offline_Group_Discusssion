#include "LanguageService.h"

#include "AppConfig.h"
#include "Paths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

int countLettersInScript(const QString &text, const QRegularExpression &re)
{
    int count = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        count += it.next().capturedLength();
    }
    return count;
}

} // namespace

bool LanguageService::load(const AppConfig &config, const Paths &paths)
{
    m_urduScriptAsHindi = config.sttUrduScriptAsHindi();
    m_devanagariLang = config.sttDevanagariLang();

    QFile langFile(QStringLiteral(":/data/lang/languages.json"));
    if (!langFile.open(QIODevice::ReadOnly)) {
        const QString disk = QDir(paths.dataDir()).filePath(QStringLiteral("lang/languages.json"));
        langFile.setFileName(disk);
        langFile.open(QIODevice::ReadOnly);
    }
    if (langFile.isOpen()) {
        const QJsonArray langs = QJsonDocument::fromJson(langFile.readAll()).object()
                                     .value(QStringLiteral("languages"))
                                     .toArray();
        for (const QJsonValue &v : langs) {
            const QJsonObject o = v.toObject();
            const QString code = o.value(QStringLiteral("code")).toString();
            m_promptLanguage.insert(code, o.value(QStringLiteral("prompt_language")).toString());
            m_voices.insert(code, o.value(QStringLiteral("voice")).toString());
        }
    }

    QFile hiFile(QStringLiteral(":/data/lang/hinglish.json"));
    if (!hiFile.open(QIODevice::ReadOnly)) {
        const QString disk = QDir(paths.dataDir()).filePath(QStringLiteral("lang/hinglish.json"));
        hiFile.setFileName(disk);
        hiFile.open(QIODevice::ReadOnly);
    }
    if (!hiFile.isOpen()) {
        m_hinglishTokens = {
            QStringLiteral("hai"), QStringLiteral("kya"), QStringLiteral("nahi"),
            QStringLiteral("mujhe"), QStringLiteral("aap"), QStringLiteral("hum"),
        };
        m_hallucinations = {QStringLiteral("Thank you."), QStringLiteral("Thanks for watching!"),
                            QStringLiteral("[Music]")};
        return true;
    }
    const QJsonObject root = QJsonDocument::fromJson(hiFile.readAll()).object();
    m_hinglishRatio = root.value(QStringLiteral("threshold_ratio")).toDouble(0.25);
    m_hinglishMinHits = root.value(QStringLiteral("min_hits")).toInt(3);
    m_hinglishReply = root.value(QStringLiteral("hinglish_reply")).toString(QStringLiteral("hindi"));
    for (const QJsonValue &t : root.value(QStringLiteral("hinglish_tokens")).toArray()) {
        m_hinglishTokens.insert(t.toString().toLower());
    }
    for (const QJsonValue &h : root.value(QStringLiteral("whisper_hallucinations")).toArray()) {
        m_hallucinations.append(h.toString());
    }
    return !m_hinglishTokens.isEmpty();
}

DetectedScript LanguageService::detectScript(const QString &text) const
{
    struct ScriptRule {
        DetectedScript script;
        QRegularExpression re;
    };
    static const ScriptRule rules[] = {
        {DetectedScript::Devanagari, QRegularExpression(QStringLiteral("[\\x{0900}-\\x{097F}]"))},
        {DetectedScript::Telugu, QRegularExpression(QStringLiteral("[\\x{0C00}-\\x{0C7F}]"))},
        {DetectedScript::Malayalam, QRegularExpression(QStringLiteral("[\\x{0D00}-\\x{0D7F}]"))},
        {DetectedScript::Tamil, QRegularExpression(QStringLiteral("[\\x{0B80}-\\x{0BFF}]"))},
        {DetectedScript::Kannada, QRegularExpression(QStringLiteral("[\\x{0C80}-\\x{0CFF}]"))},
        {DetectedScript::Bengali, QRegularExpression(QStringLiteral("[\\x{0980}-\\x{09FF}]"))},
        {DetectedScript::Gujarati, QRegularExpression(QStringLiteral("[\\x{0A80}-\\x{0AFF}]"))},
        {DetectedScript::Gurmukhi, QRegularExpression(QStringLiteral("[\\x{0A00}-\\x{0A7F}]"))},
        {DetectedScript::Arabic, QRegularExpression(QStringLiteral("[\\x{0600}-\\x{06FF}]"))},
        {DetectedScript::Latin, QRegularExpression(QStringLiteral("[A-Za-z]"))},
    };

    int total = 0;
    QHash<DetectedScript, int> counts;
    for (const ScriptRule &rule : rules) {
        const int n = countLettersInScript(text, rule.re);
        counts[rule.script] = n;
        total += n;
    }
    if (total <= 0) {
        return DetectedScript::Latin;
    }
    DetectedScript best = DetectedScript::Other;
    int bestCount = 0;
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            best = it.key();
        }
    }
    if (bestCount * 2 <= total) {
        return DetectedScript::Other;
    }
    return best;
}

QString LanguageService::scriptName(DetectedScript script) const
{
    switch (script) {
    case DetectedScript::Latin:
        return QStringLiteral("Latin");
    case DetectedScript::Devanagari:
        return QStringLiteral("Devanagari");
    case DetectedScript::Telugu:
        return QStringLiteral("Telugu");
    case DetectedScript::Malayalam:
        return QStringLiteral("Malayalam");
    case DetectedScript::Tamil:
        return QStringLiteral("Tamil");
    case DetectedScript::Kannada:
        return QStringLiteral("Kannada");
    case DetectedScript::Bengali:
        return QStringLiteral("Bengali");
    case DetectedScript::Gujarati:
        return QStringLiteral("Gujarati");
    case DetectedScript::Gurmukhi:
        return QStringLiteral("Gurmukhi");
    case DetectedScript::Arabic:
        return QStringLiteral("Arabic");
    default:
        return QStringLiteral("Other");
    }
}

bool LanguageService::isHinglish(const QString &text) const
{
    const QStringList tokens =
        text.toLower().split(QRegularExpression(QStringLiteral("\\W+")), Qt::SkipEmptyParts);
    if (tokens.size() < m_hinglishMinHits) {
        return false;
    }
    int hits = 0;
    for (const QString &tok : tokens) {
        if (m_hinglishTokens.contains(tok)) {
            ++hits;
        }
    }
    return hits >= m_hinglishMinHits
           && double(hits) / double(tokens.size()) >= m_hinglishRatio;
}

QString LanguageService::filterWhisperHallucination(const QString &text) const
{
    QString trimmed = text.trimmed();
    for (const QString &h : m_hallucinations) {
        if (trimmed.compare(h, Qt::CaseInsensitive) == 0) {
            return {};
        }
    }
    if (trimmed.compare(QStringLiteral("you"), Qt::CaseInsensitive) == 0) {
        return {};
    }
    return trimmed;
}

SessionLanguage LanguageService::replyLanguageFor(const QString &transcript,
                                                  SessionLanguage lockedFallback,
                                                  QString *noteOut) const
{
    const DetectedScript script = detectScript(transcript);
    switch (script) {
    case DetectedScript::Devanagari:
        return SessionLanguage::Hindi;
    case DetectedScript::Latin:
        if (isHinglish(transcript) && m_hinglishReply == QLatin1String("hindi")) {
            return SessionLanguage::Hindi;
        }
        return SessionLanguage::English;
    case DetectedScript::Arabic:
        if (m_urduScriptAsHindi) {
            return SessionLanguage::Hindi;
        }
        break;
    default:
        break;
    }
    const QString code = script == DetectedScript::Telugu ? QStringLiteral("te")
                       : script == DetectedScript::Malayalam ? QStringLiteral("ml")
                                                             : QString();
    if (!code.isEmpty() && voiceIdFor(code).isEmpty() && noteOut) {
        *noteOut = QStringLiteral("No voice for %1 installed; replying in English")
                       .arg(scriptName(script));
    }
    if (!code.isEmpty() && !voiceIdFor(code).isEmpty()) {
        return lockedFallback;
    }
    return lockedFallback;
}

QString LanguageService::promptLanguageString(const QString &code) const
{
    return m_promptLanguage.value(code, QStringLiteral("English"));
}

QString LanguageService::voiceIdFor(const QString &code) const
{
    return m_voices.value(code);
}
