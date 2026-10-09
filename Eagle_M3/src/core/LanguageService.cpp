#include "LanguageService.h"

#include <QChar>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

TextScript classify(char32_t cp)
{
    if ((cp >= U'A' && cp <= U'Z') || (cp >= U'a' && cp <= U'z')
        || (cp >= 0x00C0 && cp <= 0x024F && cp != 0x00D7 && cp != 0x00F7)
        || (cp >= 0x1E00 && cp <= 0x1EFF)) {
        return TextScript::Latin;
    }
    if ((cp >= 0x0900 && cp <= 0x097F) || (cp >= 0xA8E0 && cp <= 0xA8FF)) {
        return TextScript::Devanagari;
    }
    if (cp >= 0x0980 && cp <= 0x09FF) {
        return TextScript::Bengali;
    }
    if (cp >= 0x0A00 && cp <= 0x0A7F) {
        return TextScript::Gurmukhi;
    }
    if (cp >= 0x0A80 && cp <= 0x0AFF) {
        return TextScript::Gujarati;
    }
    if (cp >= 0x0B80 && cp <= 0x0BFF) {
        return TextScript::Tamil;
    }
    if (cp >= 0x0C00 && cp <= 0x0C7F) {
        return TextScript::Telugu;
    }
    if (cp >= 0x0C80 && cp <= 0x0CFF) {
        return TextScript::Kannada;
    }
    if (cp >= 0x0D00 && cp <= 0x0D7F) {
        return TextScript::Malayalam;
    }
    if ((cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F)
        || (cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF)
        || (cp >= 0xFE70 && cp <= 0xFEFF)) {
        return TextScript::Arabic;
    }
    return TextScript::Other;
}

const QHash<QString, QString> &whisperNames()
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("english"), QStringLiteral("en")},
        {QStringLiteral("hindi"), QStringLiteral("hi")},
        {QStringLiteral("urdu"), QStringLiteral("ur")},
        {QStringLiteral("marathi"), QStringLiteral("mr")},
        {QStringLiteral("nepali"), QStringLiteral("ne")},
        {QStringLiteral("telugu"), QStringLiteral("te")},
        {QStringLiteral("tamil"), QStringLiteral("ta")},
        {QStringLiteral("kannada"), QStringLiteral("kn")},
        {QStringLiteral("malayalam"), QStringLiteral("ml")},
        {QStringLiteral("bengali"), QStringLiteral("bn")},
        {QStringLiteral("gujarati"), QStringLiteral("gu")},
        {QStringLiteral("punjabi"), QStringLiteral("pa")},
        {QStringLiteral("assamese"), QStringLiteral("as")},
        {QStringLiteral("sanskrit"), QStringLiteral("sa")},
        {QStringLiteral("sindhi"), QStringLiteral("sd")},
    };
    return names;
}

} // namespace

LanguageService::LanguageService(const LanguageOptions &options)
{
    setOptions(options);
}

void LanguageService::setOptions(const LanguageOptions &options)
{
    m_options = options;
    const QStringList tokens =
        m_options.hinglishTokens.isEmpty() ? builtinHinglishTokens() : m_options.hinglishTokens;
    m_tokens.clear();
    for (const QString &t : tokens) {
        m_tokens.insert(t.trimmed().toLower());
    }
}

QStringList LanguageService::builtinHinglishTokens()
{
    return {QStringLiteral("hai"),    QStringLiteral("hain"),    QStringLiteral("nahi"),
            QStringLiteral("nahin"),  QStringLiteral("kya"),     QStringLiteral("kaise"),
            QStringLiteral("kyun"),   QStringLiteral("kyon"),    QStringLiteral("mujhe"),
            QStringLiteral("humein"), QStringLiteral("samajh"),  QStringLiteral("samjha"),
            QStringLiteral("aap"),    QStringLiteral("tum"),     QStringLiteral("hum"),
            QStringLiteral("matlab"), QStringLiteral("accha"),   QStringLiteral("achha"),
            QStringLiteral("theek"),  QStringLiteral("thik"),    QStringLiteral("bahut"),
            QStringLiteral("kuch"),   QStringLiteral("kaun"),    QStringLiteral("kab"),
            QStringLiteral("kahan"),  QStringLiteral("bhi"),     QStringLiteral("aur"),
            QStringLiteral("lekin"),  QStringLiteral("wala"),    QStringLiteral("wali"),
            QStringLiteral("karna"),  QStringLiteral("karte"),   QStringLiteral("raha"),
            QStringLiteral("rahi"),   QStringLiteral("batao"),   QStringLiteral("bataiye"),
            QStringLiteral("yaar"),   QStringLiteral("haan"),    QStringLiteral("ji")};
}

bool LanguageService::loadHinglishResource(LanguageOptions &options, QStringList *hallucinations)
{
    QFile file(QStringLiteral(":/data/lang/hinglish.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.isEmpty()) {
        return false;
    }
    QStringList tokens;
    for (const QJsonValue &v : root.value(QStringLiteral("hinglish_tokens")).toArray()) {
        tokens.append(v.toString());
    }
    if (!tokens.isEmpty()) {
        options.hinglishTokens = tokens;
    }
    options.hinglishRatio = root.value(QStringLiteral("threshold_ratio")).toDouble(options.hinglishRatio);
    options.hinglishMinHits = root.value(QStringLiteral("min_hits")).toInt(options.hinglishMinHits);
    options.hinglishMinTokens = root.value(QStringLiteral("min_tokens")).toInt(options.hinglishMinTokens);
    if (hallucinations) {
        hallucinations->clear();
        for (const QJsonValue &v : root.value(QStringLiteral("whisper_hallucinations")).toArray()) {
            hallucinations->append(v.toString());
        }
    }
    return true;
}

TextScript LanguageService::detectScript(const QString &text, double *share)
{
    QHash<int, int> counts;
    int total = 0;
    for (const char32_t cp : text.toUcs4()) {
        if (!QChar::isLetter(cp) && !QChar::isMark(cp)) {
            continue;
        }
        ++counts[int(classify(cp))];
        ++total;
    }
    if (share) {
        *share = 0.0;
    }
    if (total == 0) {
        return TextScript::Unknown;
    }
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (it.value() * 2 > total) {
            if (share) {
                *share = double(it.value()) / double(total);
            }
            return TextScript(it.key());
        }
    }
    return TextScript::Unknown;
}

QString LanguageService::scriptName(TextScript script)
{
    switch (script) {
    case TextScript::Latin: return QStringLiteral("Latin");
    case TextScript::Devanagari: return QStringLiteral("Devanagari");
    case TextScript::Bengali: return QStringLiteral("Bengali");
    case TextScript::Gurmukhi: return QStringLiteral("Gurmukhi");
    case TextScript::Gujarati: return QStringLiteral("Gujarati");
    case TextScript::Tamil: return QStringLiteral("Tamil");
    case TextScript::Telugu: return QStringLiteral("Telugu");
    case TextScript::Kannada: return QStringLiteral("Kannada");
    case TextScript::Malayalam: return QStringLiteral("Malayalam");
    case TextScript::Arabic: return QStringLiteral("Arabic");
    case TextScript::Other: return QStringLiteral("Other");
    case TextScript::Unknown: break;
    }
    return QStringLiteral("Unknown");
}

bool LanguageService::isHinglish(const QString &text) const
{
    static const QRegularExpression splitter(QStringLiteral("[^A-Za-z]+"));
    const QStringList tokens = text.toLower().split(splitter, Qt::SkipEmptyParts);
    if (tokens.size() < qMax(1, m_options.hinglishMinTokens)) {
        return false;
    }
    int hits = 0;
    for (const QString &t : tokens) {
        if (m_tokens.contains(t)) {
            ++hits;
        }
    }
    return hits >= m_options.hinglishMinHits
           && double(hits) / double(tokens.size()) >= m_options.hinglishRatio;
}

LanguageDecision LanguageService::decide(const QString &text) const
{
    LanguageDecision d;
    d.script = detectScript(text);
    switch (d.script) {
    case TextScript::Devanagari:
        d.detectedCode = m_options.devanagariLang.isEmpty() ? QStringLiteral("hi") : m_options.devanagariLang;
        break;
    case TextScript::Arabic:
        d.detectedCode = QStringLiteral("ur");
        break;
    case TextScript::Latin:
        d.hinglish = isHinglish(text);
        d.detectedCode = d.hinglish ? QStringLiteral("hi") : QStringLiteral("en");
        break;
    case TextScript::Bengali: d.detectedCode = QStringLiteral("bn"); break;
    case TextScript::Gurmukhi: d.detectedCode = QStringLiteral("pa"); break;
    case TextScript::Gujarati: d.detectedCode = QStringLiteral("gu"); break;
    case TextScript::Tamil: d.detectedCode = QStringLiteral("ta"); break;
    case TextScript::Telugu: d.detectedCode = QStringLiteral("te"); break;
    case TextScript::Kannada: d.detectedCode = QStringLiteral("kn"); break;
    case TextScript::Malayalam: d.detectedCode = QStringLiteral("ml"); break;
    case TextScript::Other:
    case TextScript::Unknown:
        break;
    }

    if (d.script == TextScript::Arabic) {
        d.replyCode = m_options.urduScriptAsHindi ? QStringLiteral("hi") : QStringLiteral("ur");
    } else if (d.hinglish) {
        d.replyCode = m_options.hinglishReply.compare(QStringLiteral("english"), Qt::CaseInsensitive) == 0
                          ? QStringLiteral("en")
                          : QStringLiteral("hi");
    } else if (!d.detectedCode.isEmpty()) {
        d.replyCode = d.detectedCode;
    } else {
        d.replyCode = m_options.noVoiceFallback;
        return d;
    }

    if (!m_options.voicedLanguages.contains(d.replyCode)) {
        d.voiceFallback = true;
        d.note = QStringLiteral("No voice for %1 installed; replying in %2")
                     .arg(displayName(d.replyCode), displayName(m_options.noVoiceFallback));
        d.replyCode = m_options.noVoiceFallback;
    }
    return d;
}

QString LanguageService::whisperNameToCode(const QString &nameOrCode)
{
    const QString key = nameOrCode.trimmed().toLower();
    if (key.isEmpty()) {
        return {};
    }
    const auto &names = whisperNames();
    const auto it = names.constFind(key);
    if (it != names.constEnd()) {
        return it.value();
    }
    return key;
}

QString LanguageService::displayName(const QString &code)
{
    const QString key = code.trimmed().toLower();
    if (key == QLatin1String("auto")) {
        return QStringLiteral("Auto");
    }
    const auto &names = whisperNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        if (it.value() == key) {
            QString name = it.key();
            name[0] = name.at(0).toUpper();
            return name;
        }
    }
    return code;
}

SessionLanguage LanguageService::toSessionLanguage(const QString &code)
{
    return code.trimmed().toLower() == QLatin1String("hi") ? SessionLanguage::Hindi
                                                           : SessionLanguage::English;
}

QString LanguageService::codeFor(SessionLanguage language)
{
    return language == SessionLanguage::Hindi ? QStringLiteral("hi") : QStringLiteral("en");
}
