#include "Application.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("IndusInternational"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("indus.local"));
    QCoreApplication::setApplicationName(QStringLiteral("Eagle GD"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0-m0"));

    QApplication qtApp(argc, argv);

    Application app;
    if (!app.initialize()) {
        return 1;
    }
    return app.run();
}
