#include "Application.h"

#include "AppConfig.h"
#include "Logging.h"
#include "Paths.h"
#include "LlmServices.h"
#include "OllamaBackend.h"
#include "HistoryManager.h"
#include "Metrics.h"
#include "PromptBuilder.h"
#include "TokenEstimator.h"
#include "ServiceSupervisor.h"
#include "MainWindow.h"
#include "ReadinessPanel.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QStandardPaths>

Application::Application(QObject *parent)
    : QObject(parent)
{
}

Application::~Application()
{
    EagleLog::shutdown();
}

AppConfig &Application::config()
{
    return *m_config;
}

const Paths &Application::paths() const
{
    return *m_paths;
}

ServiceSupervisor &Application::supervisor()
{
    return *m_supervisor;
}

LlmServices &Application::llm()
{
    return *m_llm;
}

QString Application::resolveConfigPath() const
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Eagle GD Offline"));
    parser.addHelpOption();
    QCommandLineOption configOption(QStringList{QStringLiteral("config")},
                                    QStringLiteral("Path to eagle_config.json"),
                                    QStringLiteral("path"));
    parser.addOption(configOption);
    parser.process(QCoreApplication::arguments());

    if (parser.isSet(configOption)) {
        return parser.value(configOption);
    }

    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    return QDir(configDir).filePath(QStringLiteral("eagle_config.json"));
}

bool Application::initialize()
{
    m_config = std::make_unique<AppConfig>();
    const QString configPath = resolveConfigPath();
    m_config->load(configPath);

    m_paths = std::make_unique<Paths>(*m_config);
    EagleLog::install(m_paths->logsDir());
    m_paths->logResolvedPaths();

    m_supervisor = std::make_unique<ServiceSupervisor>(*m_config);
    m_supervisor->refreshAll();

    m_llm = std::make_unique<LlmServices>();
    QStringList llmErrors;
    if (!m_llm->initialize(*m_config, *m_paths, &llmErrors)) {
        qWarning("LLM services failed to initialize: %s",
                 qUtf8Printable(llmErrors.join(QStringLiteral("; "))));
    } else if (!llmErrors.isEmpty()) {
        qWarning("Prompt validation: %s", qUtf8Printable(llmErrors.join(QStringLiteral("; "))));
    }

    return true;
}

int Application::run()
{
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app) {
        return 1;
    }

    QFont uiFont(QStringLiteral("Segoe UI"), 10);
    app->setFont(uiFont);

    QFile styleFile(QStringLiteral(":/styles/app.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    m_mainWindow = new MainWindow(*m_config, *m_paths, *m_supervisor, *m_llm);
    m_mainWindow->show();

    if (m_llm->backend) {
        QObject::connect(m_llm->backend.get(), &OllamaBackend::probed, m_mainWindow,
                         [this](bool ok, const QString &, const QStringList &) {
                             if (!ok || !m_llm->prompts) {
                                 return;
                             }
                             m_llm->backend->runStartupSequence(m_config->llmModel(),
                                                                m_config->llmFallbackModel(),
                                                                m_llm->prompts->systemPrompt());
                         });
        m_llm->backend->probe();
    }

    auto *readiness = new ReadinessPanel(*m_supervisor, m_mainWindow->turnSpeech(), m_mainWindow);
    readiness->setAttribute(Qt::WA_DeleteOnClose);
    readiness->show();

    return app->exec();
}
