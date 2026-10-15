from __future__ import annotations

import logging

from PySide6.QtCore import QCoreApplication
from PySide6.QtGui import QFont
from PySide6.QtWidgets import QApplication

from eaglegd._repo import resource_path
from eaglegd.app.AppConfig import AppConfig
from eaglegd.app.LlmServices import LlmServices
from eaglegd.app.Logging import EagleLog
from eaglegd.app.Paths import Paths
from eaglegd.app.ServiceSupervisor import ServiceSupervisor
from eaglegd.ui.MainWindow import MainWindow
from eaglegd.ui.ReadinessPanel import ReadinessPanel

_log = logging.getLogger("eagle.app")


class Application:
    def __init__(self, parent=None) -> None:
        del parent
        self._config: AppConfig | None = None
        self._paths: Paths | None = None
        self._supervisor: ServiceSupervisor | None = None
        self._llm: LlmServices | None = None
        self._main_window: MainWindow | None = None

    def __del__(self) -> None:
        EagleLog.shutdown()

    def config(self) -> AppConfig:
        assert self._config is not None
        return self._config

    def paths(self) -> Paths:
        assert self._paths is not None
        return self._paths

    def supervisor(self) -> ServiceSupervisor:
        assert self._supervisor is not None
        return self._supervisor

    def llm(self) -> LlmServices:
        assert self._llm is not None
        return self._llm

    def _resolve_config_path(self) -> str:
        from pathlib import Path

        from PySide6.QtCore import QCommandLineOption, QCommandLineParser, QStandardPaths

        parser = QCommandLineParser()
        parser.setApplicationDescription("Eagle GD Offline")
        parser.addHelpOption()
        parser.addOption(
            QCommandLineOption(["config"], "Path to eagle_config.json", "path")
        )
        parser.process(QCoreApplication.arguments())
        if parser.isSet("config"):
            return parser.value("config")
        config_dir = QStandardPaths.writableLocation(
            QStandardPaths.StandardLocation.AppConfigLocation
        )
        Path(config_dir).mkdir(parents=True, exist_ok=True)
        return str(Path(config_dir) / "eagle_config.json")

    def initialize(self) -> bool:
        self._config = AppConfig()
        config_path = self._resolve_config_path()
        self._config.load(config_path)

        self._paths = Paths(self._config)
        EagleLog.install(self._paths.logsDir())
        self._paths.logResolvedPaths()

        self._supervisor = ServiceSupervisor(self._config, self._paths)
        self._supervisor.refreshAll()
        app = QCoreApplication.instance()
        if app is not None:
            app.aboutToQuit.connect(self._on_about_to_quit)

        self._llm = LlmServices()
        llm_errors: list[str] = []
        if not self._llm.initialize(self._config, self._paths, llm_errors):
            _log.warning(
                "LLM services failed to initialize: %s", "; ".join(llm_errors)
            )
        elif llm_errors:
            _log.warning("Prompt validation: %s", "; ".join(llm_errors))
        return True

    def _on_about_to_quit(self) -> None:
        if self._main_window is not None:
            try:
                self._main_window.shutdown()
            except Exception:
                _log.exception("Main window shutdown failed")
        if self._supervisor is not None:
            try:
                self._supervisor.stopWhisperServer()
            except Exception:
                _log.exception("whisper-server stop failed")
        EagleLog.shutdown()

    def run(self) -> int:
        app = QApplication.instance()
        if app is None:
            return 1

        app.setFont(QFont("Segoe UI", 10))
        style_path = resource_path("styles", "app.qss")
        if style_path.is_file():
            app.setStyleSheet(style_path.read_text(encoding="utf-8"))

        assert self._config and self._paths and self._supervisor and self._llm
        self._main_window = MainWindow(
            self._config, self._paths, self._supervisor, self._llm
        )
        self._main_window.show()

        if self._llm.backend:
            backend = self._llm.backend

            def on_probed(ok: bool, _version: str, _models: list) -> None:
                if not ok or not self._llm or not self._llm.prompts or not self._main_window:
                    return
                backend.runStartupSequence(
                    self._config.llmModel(),
                    self._config.llmFallbackModel(),
                    self._main_window.systemPromptForStartup(),
                )

            backend.probed.connect(on_probed)
            backend.probe()

        readiness = ReadinessPanel(
            self._supervisor, self._main_window.turnSpeech(), self._main_window
        )
        from PySide6.QtCore import Qt

        readiness.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        readiness.show()

        return app.exec()
