#pragma once

#include <QObject>
#include <memory>

class AppConfig;
class MainWindow;
class Paths;
class ServiceSupervisor;

class Application : public QObject
{
    Q_OBJECT

public:
    explicit Application(QObject *parent = nullptr);
    ~Application() override;

    bool initialize();
    int run();

    AppConfig &config();
    const Paths &paths() const;
    ServiceSupervisor &supervisor();

private:
    QString resolveConfigPath() const;

    std::unique_ptr<AppConfig> m_config;
    std::unique_ptr<Paths> m_paths;
    std::unique_ptr<ServiceSupervisor> m_supervisor;
    MainWindow *m_mainWindow = nullptr;
};
