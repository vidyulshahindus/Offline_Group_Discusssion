#include "Application.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication qtApp(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Eagle GD"));
    QApplication::setOrganizationName(QStringLiteral("IndusInternational"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0-m0"));

    Application app;
    if (!app.initialize()) {
        return 1;
    }
    return app.run();
}
