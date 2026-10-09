#include "FillerCache.h"

#include "AppConfig.h"
#include "Paths.h"
#include "PiperCliBackend.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

bool FillerCache::load(const AppConfig &config, const Paths &paths)
{
    m_enVoice = config.ttsVoiceEn();
    m_hiVoice = config.ttsVoiceHi();
    m_enScale = config.ttsLengthScaleDefault();
    m_hiScale = config.ttsLengthScaleHi();
    m_cacheRoot = QDir(paths.dataDir()).filePath(QStringLiteral("cache/fillers"));
    QDir().mkpath(m_cacheRoot);

    QFile file(QStringLiteral(":/data/lang/fillers.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        const QString disk = QDir(paths.dataDir()).filePath(QStringLiteral("lang/fillers.json"));
        file.setFileName(disk);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    m_enFillers.clear();
    m_hiFillers.clear();
    for (const QJsonValue &value : root.value(QStringLiteral("en")).toArray()) {
        m_enFillers.append(value.toString());
    }
    for (const QJsonValue &value : root.value(QStringLiteral("hi")).toArray()) {
        m_hiFillers.append(value.toString());
    }
    return !m_enFillers.isEmpty();
}

QString FillerCache::cacheFilePath(const QString &voiceId, double lengthScale,
                                   const QString &text) const
{
    const QByteArray hash =
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex();
    const QString dir =
        QDir(m_cacheRoot).filePath(QStringLiteral("%1/%2").arg(voiceId).arg(lengthScale, 0, 'f', 2));
    QDir().mkpath(dir);
    return QDir(dir).filePath(QString::fromLatin1(hash) + QStringLiteral(".wav"));
}

bool FillerCache::synthesizeToCache(PiperCliBackend &worker, const QString &text,
                                    const QString &wavPath)
{
    if (QFile::exists(wavPath) && QFileInfo(wavPath).size() > 44) {
        m_paths.insert(wavPath, wavPath);
        return true;
    }

    QEventLoop loop;
    bool ok = false;
    const QMetaObject::Connection conn = QObject::connect(
        &worker, &PiperCliBackend::wavReady, &loop,
        [&](int, int, const QString &src, qint64) {
            QFile::remove(wavPath);
            if (QFile::copy(src, wavPath)) {
                QFile::remove(src);
                m_paths.insert(wavPath, wavPath);
                ok = true;
            }
            loop.quit();
        });
    const QMetaObject::Connection fail = QObject::connect(
        &worker, &PiperCliBackend::synthesisFailed, &loop,
        [&](int, int, const QString &) { loop.quit(); });

    worker.synthesize(-1, 0, text);
    loop.exec();
    QObject::disconnect(conn);
    QObject::disconnect(fail);
    return ok;
}

bool FillerCache::warmUp(PiperCliBackend &enWorker, PiperCliBackend &hiWorker)
{
    enWorker.startWorker();
    hiWorker.startWorker();
    bool allOk = true;
    for (int i = 0; i < m_enFillers.size(); ++i) {
        const QString path = cacheFilePath(m_enVoice, m_enScale, m_enFillers.at(i));
        allOk = synthesizeToCache(enWorker, m_enFillers.at(i), path) && allOk;
    }
    for (int i = 0; i < m_hiFillers.size(); ++i) {
        const QString path = cacheFilePath(m_hiVoice, m_hiScale, m_hiFillers.at(i));
        allOk = synthesizeToCache(hiWorker, m_hiFillers.at(i), path) && allOk;
    }
    m_ready = allOk;
    return allOk;
}

QString FillerCache::cacheKey(SessionLanguage language, int index) const
{
    const QString voice = language == SessionLanguage::Hindi ? m_hiVoice : m_enVoice;
    const double scale = language == SessionLanguage::Hindi ? m_hiScale : m_enScale;
    const QStringList &list = language == SessionLanguage::Hindi ? m_hiFillers : m_enFillers;
    if (index < 0 || index >= list.size()) {
        return {};
    }
    return cacheFilePath(voice, scale, list.at(index));
}

QString FillerCache::wavPathFor(SessionLanguage language, int index) const
{
    const QString key = cacheKey(language, index);
    if (key.isEmpty()) {
        return {};
    }
    if (QFile::exists(key)) {
        return key;
    }
    return m_paths.value(key);
}

int FillerCache::count(SessionLanguage language) const
{
    return language == SessionLanguage::Hindi ? m_hiFillers.size() : m_enFillers.size();
}

int FillerCache::pickIndex(SessionLanguage language, int avoidIndex) const
{
    const int n = count(language);
    if (n <= 0) {
        return -1;
    }
    if (n == 1) {
        return 0;
    }
    int idx = QRandomGenerator::global()->bounded(n);
    if (idx == avoidIndex) {
        idx = (idx + 1) % n;
    }
    return idx;
}
