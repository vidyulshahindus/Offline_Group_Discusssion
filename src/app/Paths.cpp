#include "Paths.h"

#include "AppConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcPaths, "eagle.paths")

Paths::Paths(const AppConfig &config)
    : m_config(config)
{
    m_toolsDir = resolveToolsDir();
    m_dataDir = config.pathsDataDir();
    if (m_dataDir.isEmpty()) {
        m_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    m_unitsDir = config.pathsUnitsDir();
    if (m_unitsDir.isEmpty()) {
        m_unitsDir = QDir(m_dataDir).filePath(QStringLiteral("units"));
    }
    m_logsDir = config.pathsLogsDir();
    if (m_logsDir.isEmpty()) {
        m_logsDir = QDir(m_dataDir).filePath(QStringLiteral("logs"));
    }
    QDir().mkpath(m_logsDir);
}

QString Paths::applicationDir() const
{
    return QCoreApplication::applicationDirPath();
}

QString Paths::toolsDir() const
{
    return m_toolsDir;
}

QString Paths::dataDir() const
{
    return m_dataDir;
}

QString Paths::unitsDir() const
{
    return m_unitsDir;
}

QString Paths::logsDir() const
{
    return m_logsDir;
}

QString Paths::configFilePath() const
{
    return m_config.filePath();
}

QString Paths::withNativeExeName(const QString &baseName) const
{
#ifdef Q_OS_WIN
    if (!baseName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        return baseName + QStringLiteral(".exe");
    }
#endif
    return baseName;
}

QString Paths::firstExisting(const QStringList &candidates) const
{
    for (const QString &path : candidates) {
        if (path.isEmpty()) {
            continue;
        }
        const QFileInfo info(path);
        if (info.exists()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

QString Paths::resolveToolsDir() const
{
    const QString configured = m_config.pathsToolsDir();
    if (!configured.isEmpty() && QDir(configured).exists()) {
        return QDir(configured).absolutePath();
    }

    const QString appDir = applicationDir();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("tools")),
        QDir(appDir).filePath(QStringLiteral("../tools")),
        QDir(appDir).filePath(QStringLiteral("../../tools")),
#ifdef Q_OS_LINUX
        QStringLiteral("/opt/eagle-gd/tools"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
            .filePath(QStringLiteral(".local/share/EagleGD/tools")),
#endif
#ifdef Q_OS_WIN
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath(QStringLiteral("EagleGD/tools")),
#endif
    };

    for (const QString &candidate : candidates) {
        if (QDir(candidate).exists()) {
            return QDir(candidate).absolutePath();
        }
    }
    return QDir(appDir).filePath(QStringLiteral("tools"));
}

QString Paths::whisperCli() const
{
    const QString configured = m_config.sttServerExe();
    if (!configured.isEmpty()) {
        return QFileInfo(configured).absoluteFilePath();
    }

    const QString cliName = withNativeExeName(QStringLiteral("whisper-cli"));
    const QString tools = m_toolsDir;
    return firstExisting({
        QDir(tools).filePath(QStringLiteral("whisper/Release/") + cliName),
        QDir(tools).filePath(QStringLiteral("whisper/") + cliName),
        QDir(tools).filePath(QStringLiteral("whisper/bin/") + cliName),
    });
}

QString Paths::whisperModel(const QString &modelFile) const
{
    const QString file = modelFile.isEmpty() ? m_config.sttModel() : modelFile;
    const QString tools = m_toolsDir;
    return firstExisting({
        QDir(tools).filePath(QStringLiteral("whisper/models/") + file),
        QDir(tools).filePath(QStringLiteral("whisper/") + file),
    });
}

QString Paths::piperExecutable() const
{
    const QString configured = m_config.ttsPiperExe();
    if (!configured.isEmpty()) {
        return QFileInfo(configured).absoluteFilePath();
    }

    const QString piperName = withNativeExeName(QStringLiteral("piper"));
    const QString tools = m_toolsDir;
    return firstExisting({
        QDir(tools).filePath(QStringLiteral("piper/piper/") + piperName),
        QDir(tools).filePath(QStringLiteral("piper/") + piperName),
    });
}

QString Paths::piperVoicesDir() const
{
    const QString configured = m_config.ttsVoicesDir();
    if (!configured.isEmpty() && QDir(configured).exists()) {
        return QDir(configured).absolutePath();
    }
    const QString tools = m_toolsDir;
    const QString voices = QDir(tools).filePath(QStringLiteral("piper/voices"));
    if (QDir(voices).exists()) {
        return voices;
    }
    return voices;
}

QString Paths::piperVoiceOnnx(const QString &voiceId) const
{
    const QString base = voiceId.isEmpty() ? m_config.ttsVoiceEn() : voiceId;
    const QString path = QDir(piperVoicesDir()).filePath(base + QStringLiteral(".onnx"));
    if (QFileInfo::exists(path)) {
        return QFileInfo(path).absoluteFilePath();
    }
    return path;
}

void Paths::logResolvedPaths() const
{
    qCInfo(lcPaths) << "tools_dir:" << m_toolsDir;
    qCInfo(lcPaths) << "data_dir:" << m_dataDir;
    qCInfo(lcPaths) << "logs_dir:" << m_logsDir;
    qCInfo(lcPaths) << "whisper_cli:" << whisperCli();
    qCInfo(lcPaths) << "whisper_model:" << whisperModel(m_config.sttModel());
    qCInfo(lcPaths) << "piper_exe:" << piperExecutable();
    qCInfo(lcPaths) << "piper_voice_en:" << piperVoiceOnnx(m_config.ttsVoiceEn());
}
