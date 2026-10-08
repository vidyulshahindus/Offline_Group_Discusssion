#include "Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QtGlobal>

#include <cstdio>

namespace {

QMutex g_logMutex;
QFile *g_logFile = nullptr;

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

    const QString line = QStringLiteral("[%1] %2 %3")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  QString::fromUtf8(level),
                                  msg);

    if (g_logFile && g_logFile->isOpen()) {
        QTextStream stream(g_logFile);
        stream << line << '\n';
        stream.flush();
    }

    fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    (void)context;

    if (type == QtFatalMsg) {
        abort();
    }
}

} // namespace

void EagleLog::install(const QString &logsDir)
{
    QDir().mkpath(logsDir);
    const QString path =
        QDir(logsDir).filePath(QStringLiteral("eaglegd_%1.log")
                                   .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"))));

    g_logFile = new QFile(path);
    if (g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qInstallMessageHandler(messageHandler);
        qInfo("Eagle GD logging to %s", qUtf8Printable(path));
    } else {
        delete g_logFile;
        g_logFile = nullptr;
    }
}

void EagleLog::shutdown()
{
    qInstallMessageHandler(nullptr);
    if (g_logFile) {
        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }
}
