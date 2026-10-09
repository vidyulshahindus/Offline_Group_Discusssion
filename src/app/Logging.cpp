#include "Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>
#include <QtGlobal>

#include <cstdio>

namespace {

QMutex g_logMutex;
QFile *g_logFile = nullptr;
QString g_logDir;
QString g_logPath;

bool tryOpenLog(const QString &logsDir)
{
    if (logsDir.isEmpty()) {
        return false;
    }
    if (!QDir().mkpath(logsDir)) {
        return false;
    }
    const QString path =
        QDir(logsDir).filePath(QStringLiteral("eaglegd_%1.log")
                                   .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"))));
    auto *file = new QFile(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete file;
        return false;
    }
    g_logFile = file;
    g_logDir = QDir(logsDir).absolutePath();
    g_logPath = QFileInfo(*g_logFile).absoluteFilePath();
    return true;
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QMutexLocker lock(&g_logMutex);

    const char *level = "INFO";
    switch (type) {
    case QtDebugMsg:
        level = "DEBUG";
        break;
    case QtInfoMsg:
        level = "INFO";
        break;
    case QtWarningMsg:
        level = "WARN";
        break;
    case QtCriticalMsg:
        level = "CRIT";
        break;
    case QtFatalMsg:
        level = "FATAL";
        break;
    }

    QString category = QString::fromUtf8(context.category);
    if (category.isEmpty()) {
        category = QStringLiteral("default");
    }

    const QString line = QStringLiteral("[%1] %2 %3 %4")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  QString::fromUtf8(level),
                                  category,
                                  msg);

    if (g_logFile && g_logFile->isOpen()) {
        QTextStream stream(g_logFile);
        stream << line << '\n';
        stream.flush();
    }

    fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    fflush(stderr);

    if (type == QtFatalMsg) {
        abort();
    }
}

} // namespace

void EagleLog::install(const QString &preferredLogsDir)
{
    QLoggingCategory::setFilterRules(QStringLiteral(
        "*.debug=false\n"
        "eagle.*=true\n"
        "eagle.*.debug=true"));

    const QString localLogs =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("logs"));
    const QString roamingLogs =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("logs"));
    const QString tempLogs =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("EagleGD/logs"));

    const QStringList candidates = {preferredLogsDir, localLogs, roamingLogs, tempLogs};
    bool opened = false;
    for (const QString &dir : candidates) {
        if (tryOpenLog(dir)) {
            opened = true;
            break;
        }
    }

    qInstallMessageHandler(messageHandler);
    if (opened) {
        qInfo("Eagle GD logging to %s", qUtf8Printable(g_logPath));
    } else {
        qWarning("Eagle GD could not open a log file; messages go to stderr only");
    }
}

void EagleLog::shutdown()
{
    qInstallMessageHandler(nullptr);
    QMutexLocker lock(&g_logMutex);
    if (g_logFile) {
        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }
}

QString EagleLog::directory()
{
    return g_logDir;
}

QString EagleLog::filePath()
{
    return g_logPath;
}
