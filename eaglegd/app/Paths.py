from __future__ import annotations

import logging
import sys
from pathlib import Path
from typing import TYPE_CHECKING

from PySide6.QtCore import QCoreApplication, QStandardPaths

if TYPE_CHECKING:
    from eaglegd.app.AppConfig import AppConfig

_log = logging.getLogger("eagle.paths")


class Paths:
    def __init__(self, config: AppConfig) -> None:
        self._config = config
        self._tools_dir = self._resolve_tools_dir()
        configured_data = config.pathsDataDir()
        if configured_data:
            self._data_dir = Path(configured_data).expanduser()
        else:
            loc = QStandardPaths.writableLocation(
                QStandardPaths.StandardLocation.AppLocalDataLocation
            )
            if not loc:
                loc = QStandardPaths.writableLocation(
                    QStandardPaths.StandardLocation.AppDataLocation
                )
            # Before QCoreApplication identity is set, Qt may return a bare
            # ~/.local/share — pin to EagleGD so we never mkdir sibling dirs.
            fallback = Path.home() / ".local/share" / "EagleGD"
            if loc:
                candidate = Path(loc)
                if candidate.name.lower() in {"share", ".local"} or candidate == Path.home() / ".local" / "share":
                    self._data_dir = fallback
                else:
                    self._data_dir = candidate
            else:
                self._data_dir = fallback
        configured_units = config.pathsUnitsDir()
        self._units_dir = (
            Path(configured_units) if configured_units else self._data_dir / "units"
        )
        configured_logs = config.pathsLogsDir()
        self._logs_dir = (
            Path(configured_logs) if configured_logs else self._data_dir / "logs"
        )
        self._logs_dir.mkdir(parents=True, exist_ok=True)

    def applicationDir(self) -> Path:
        app = QCoreApplication.instance()
        if app is not None:
            return Path(app.applicationDirPath())
        return Path(sys.argv[0]).resolve().parent

    def toolsDir(self) -> Path:
        return self._tools_dir

    def dataDir(self) -> Path:
        return self._data_dir

    def unitsDir(self) -> Path:
        return self._units_dir

    def logsDir(self) -> Path:
        return self._logs_dir

    def configFilePath(self) -> str:
        return self._config.filePath()

    @staticmethod
    def _with_native_exe_name(base_name: str) -> str:
        return base_name

    def _first_existing(self, candidates: list[Path]) -> Path | None:
        for path in candidates:
            if path and path.exists():
                return path.resolve()
        return None

    def _resolve_tools_dir(self) -> Path:
        configured = self._config.pathsToolsDir()
        if configured:
            p = Path(configured)
            if p.is_dir():
                return p.resolve()
        from eaglegd._repo import tools_search_roots

        for candidate in tools_search_roots():
            if candidate.is_dir():
                return candidate.resolve()
        app_dir = self.applicationDir()
        extras = [
            app_dir / "tools",
            app_dir / "../tools",
            app_dir / "../../tools",
            Path("/opt/eagle-gd/tools"),
            Path.home() / ".local/share/EagleGD/tools",
        ]
        for candidate in extras:
            if candidate.is_dir():
                return candidate.resolve()
        return (app_dir / "tools").resolve()

    def whisperCli(self) -> Path | None:
        cli_name = self._with_native_exe_name("whisper-cli")
        tools = self._tools_dir
        return self._first_existing(
            [
                tools / "whisper/Release" / cli_name,
                tools / "whisper" / cli_name,
                tools / "whisper/bin" / cli_name,
            ]
        )

    def whisperServerExe(self) -> Path | None:
        configured = self._config.sttServerExe()
        if configured:
            p = Path(configured)
            if p.is_file():
                return p.resolve()
        server_name = self._with_native_exe_name("whisper-server")
        tools = self._tools_dir
        return self._first_existing(
            [
                tools / "whisper/Release" / server_name,
                tools / "whisper" / server_name,
                tools / "whisper/bin" / server_name,
            ]
        )

    def resolvedWhisperModel(self) -> Path | None:
        return self.whisperModel("")

    def whisperModel(self, model_file: str) -> Path | None:
        if model_file:
            files = [model_file]
        else:
            files = [self._config.sttModel(), self._config.sttFallbackModel()]
        tools = self._tools_dir
        for file in files:
            if not file:
                continue
            found = self._first_existing(
                [
                    tools / "whisper/models" / file,
                    tools / "whisper/Release/models" / file,
                    tools / "whisper" / file,
                ]
            )
            if found:
                return found
        return None

    def piperExecutable(self) -> Path | None:
        configured = self._config.ttsPiperExe()
        if configured:
            return Path(configured).resolve()
        piper_name = self._with_native_exe_name("piper")
        tools = self._tools_dir
        return self._first_existing(
            [
                tools / "piper/piper" / piper_name,
                tools / "piper" / piper_name,
            ]
        )

    def piperVoicesDir(self) -> Path:
        configured = self._config.ttsVoicesDir()
        if configured:
            p = Path(configured)
            if p.is_dir():
                return p.resolve()
        return (self._tools_dir / "piper/voices").resolve()

    def piperVoiceOnnx(self, voice_id: str) -> Path:
        base = voice_id or self._config.ttsVoiceEn()
        path = self.piperVoicesDir() / f"{base}.onnx"
        return path.resolve() if path.is_file() else path

    def logResolvedPaths(self) -> None:
        _log.info("tools_dir: %s", self._tools_dir)
        _log.info("data_dir: %s", self._data_dir)
        _log.info("logs_dir: %s", self._logs_dir)
        _log.info("whisper_server: %s", self.whisperServerExe())
        _log.info("whisper_model: %s", self.resolvedWhisperModel())
        _log.info("piper_exe: %s", self.piperExecutable())
        _log.info("piper_voice_en: %s", self.piperVoiceOnnx(self._config.ttsVoiceEn()))

    # --- snake_case aliases ---
    @property
    def tools_dir(self) -> Path:
        return self.toolsDir()

    @property
    def data_dir(self) -> Path:
        return self.dataDir()

    @property
    def units_dir(self) -> Path:
        return self.unitsDir()

    @property
    def logs_dir(self) -> Path:
        return self.logsDir()

    def config_file_path(self) -> Path:
        return Path(self.configFilePath())

    def whisper_server_exe(self) -> Path | None:
        return self.whisperServerExe()

    def whisper_model(self, model_file: str = "") -> Path | None:
        return self.whisperModel(model_file)

    def resolved_whisper_model(self) -> Path | None:
        return self.resolvedWhisperModel()

    def piper_executable(self) -> Path | None:
        return self.piperExecutable()

    def piper_voices_dir(self) -> Path:
        return self.piperVoicesDir()

    def piper_voice_onnx(self, voice_id: str = "") -> Path:
        return self.piperVoiceOnnx(voice_id)

    def prompts_dir(self) -> Path:
        from eaglegd._repo import resource_path

        return resource_path("prompts")
