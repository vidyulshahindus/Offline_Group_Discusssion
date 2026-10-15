from __future__ import annotations

import sys

from PySide6.QtCore import QCoreApplication
from PySide6.QtWidgets import QApplication

from eaglegd.app.Application import Application


def main() -> int:
    QCoreApplication.setOrganizationName("IndusInternational")
    QCoreApplication.setOrganizationDomain("indus.local")
    QCoreApplication.setApplicationName("Eagle GD")
    QCoreApplication.setApplicationVersion("0.1.0-m0")

    qt_app = QApplication(sys.argv)
    app = Application()
    if not app.initialize():
        return 1
    return app.run()


if __name__ == "__main__":
    raise SystemExit(main())
