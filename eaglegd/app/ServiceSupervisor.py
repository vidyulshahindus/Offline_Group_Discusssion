from __future__ import annotations

import json
import logging
import time
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import TYPE_CHECKING

from PySide6.QtCore import QObject, QProcess, QThread, QTimer, QUrl, Signal
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest

if TYPE_CHECKING:
    from eaglegd.app.AppConfig import AppConfig
    from eaglegd.app.Paths import Paths

_log = logging.getLogger("eagle.supervisor")


class ServiceHealth(Enum):
    Unknown = auto()
    Checking = auto()
    Ok = auto()
    Warning = auto()
    Error = auto()


@dataclass
class ServiceStatus:
    id: str
    label: str
    health: ServiceHealth = ServiceHealth.Unknown
    detail: str = ""
    fixHint: str = ""


class ServiceSupervisor(QObject):
    statusesChanged = Signal()

    def __init__(self, config: AppConfig, paths: Paths, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._config = config
        self._paths = paths
        self._network = QNetworkAccessManager(self)
        self._whisper: QProcess | None = None
        self._whisper_lang = ""
        self._statuses: list[ServiceStatus] = []
        self._seed_placeholders()

    def __del__(self) -> None:
        self.stopWhisperServer()

    def statuses(self) -> list[ServiceStatus]:
        return list(self._statuses)

    def _index_of_status(self, service_id: str) -> int:
        for i, status in enumerate(self._statuses):
            if status.id == service_id:
                return i
        return -1

    def _seed_placeholders(self) -> None:
        self._statuses = [
            ServiceStatus(
                "ollama",
                "Ollama (LLM)",
                ServiceHealth.Unknown,
                "Not checked yet",
                "Install Ollama and ensure it is running on localhost:11434.",
            ),
            ServiceStatus(
                "whisper",
                "whisper-server (STT)",
                ServiceHealth.Unknown,
                "Not checked yet",
                "Place whisper-server.exe and ggml model under tools/whisper/.",
            ),
            ServiceStatus(
                "piper",
                "Piper (TTS)",
                ServiceHealth.Unknown,
                "Not supervised in M0",
                "Place piper under tools/piper or configure tts.piper_exe.",
            ),
            ServiceStatus(
                "robot",
                "Robot bridge",
                ServiceHealth.Unknown,
                "Not supervised in M0",
                "Optional: run robot_bridge on port 9100.",
            ),
        ]

    def _set_status(
        self, service_id: str, health: ServiceHealth, detail: str, fix_hint: str = ""
    ) -> None:
        idx = self._index_of_status(service_id)
        if idx < 0:
            return
        self._statuses[idx].health = health
        self._statuses[idx].detail = detail
        if fix_hint:
            self._statuses[idx].fixHint = fix_hint
        self.statusesChanged.emit()

    def whisperBaseUrl(self) -> QUrl:
        return QUrl(f"http://{self._config.sttHost()}:{self._config.sttPort()}")

    def whisper_base_url(self) -> QUrl:
        return self.whisperBaseUrl()

    def runningWhisperLanguage(self) -> str:
        return self._whisper_lang

    def running_whisper_language(self) -> str:
        return self.runningWhisperLanguage()

    def ensure_whisper_server(self, language_code: str) -> bool:
        return self.ensureWhisperServer(language_code)

    def stop_whisper_server(self) -> None:
        self.stopWhisperServer()

    def refresh_all(self) -> None:
        self.refreshAll()

    def stopWhisperServer(self) -> None:
        proc = self._whisper
        self._whisper = None
        self._whisper_lang = ""
        if proc is None:
            return
        for signal in (
            proc.readyReadStandardOutput,
            proc.readyReadStandardError,
            proc.finished,
            proc.errorOccurred,
            proc.started,
        ):
            try:
                signal.disconnect()
            except (RuntimeError, TypeError):
                pass
        if proc.state() != QProcess.ProcessState.NotRunning:
            proc.terminate()
            if not proc.waitForFinished(2000):
                proc.kill()
                proc.waitForFinished(1000)
        proc.deleteLater()
        _log.info("whisper-server stopped")

    def _start_whisper_process(self, language_code: str) -> bool:
        exe = self._paths.whisperServerExe()
        model = self._paths.resolvedWhisperModel()
        if not exe or not exe.is_file():
            _log.warning("whisper-server exe missing")
            return False
        if not model or not model.is_file():
            _log.warning("whisper model missing %s", model)
            return False
        self.stopWhisperServer()
        self._whisper = QProcess(self)
        self._whisper.setWorkingDirectory(str(exe.parent))
        self._whisper.setProcessChannelMode(QProcess.ProcessChannelMode.SeparateChannels)
        ideal = QThread.idealThreadCount()
        default_threads = min(6, ideal // 2)
        threads = (
            self._config.sttThreads()
            if self._config.sttThreads() > 0
            else max(1, default_threads)
        )
        args = [
            "-m",
            str(model),
            "--host",
            self._config.sttHost(),
            "--port",
            str(self._config.sttPort()),
            "-t",
            str(max(1, threads)),
            "-l",
            language_code,
            "--inference-path",
            "/inference",
        ]
        _log.info("starting whisper-server threads %s args %s", threads, args)
        self._whisper.start(str(exe), args)
        if not self._whisper.waitForStarted(8000):
            _log.warning(
                "whisper-server failed to start %s", self._whisper.errorString()
            )
            self.stopWhisperServer()
            return False
        self._whisper_lang = language_code
        return True

    def ensureWhisperServer(self, language_code: str) -> bool:
        if (
            self._whisper
            and self._whisper.state() == QProcess.ProcessState.Running
            and self._whisper_lang == language_code
        ):
            return True
        if not self._start_whisper_process(language_code):
            return False
        from PySide6.QtCore import QEventLoop

        loop = QEventLoop()
        timer = QTimer()
        timer.setSingleShot(True)
        ok = False

        def probe() -> None:
            nonlocal ok
            url = self.whisperBaseUrl().resolved(QUrl("/health"))
            reply = self._network.get(QNetworkRequest(url))

            def finished() -> None:
                nonlocal ok
                ok = reply.error() == QNetworkReply.NetworkError.NoError
                reply.deleteLater()
                loop.quit()

            reply.finished.connect(finished)

        timer.setInterval(4000)
        timer.timeout.connect(loop.quit)
        for _ in range(8):
            probe()
            timer.start()
            loop.exec()
            if ok:
                return True
            time.sleep(0.2)
        return (
            self._whisper is not None
            and self._whisper.state() == QProcess.ProcessState.Running
        )

    def _probe_whisper_health(self) -> None:
        exe = self._paths.whisperServerExe()
        model = self._paths.resolvedWhisperModel()
        if not exe or not exe.is_file():
            self._set_status(
                "whisper",
                ServiceHealth.Error,
                "whisper-server.exe not found",
                "Copy tools/whisper/Release/whisper-server.exe (see Paths).",
            )
            return
        if not model or not model.is_file():
            self._set_status(
                "whisper",
                ServiceHealth.Error,
                "Model file not found",
                "Place ggml-base.bin or ggml-small-q5_1.bin under tools/whisper/models/.",
            )
            return
        if not self._whisper or self._whisper.state() != QProcess.ProcessState.Running:
            self._set_status(
                "whisper",
                ServiceHealth.Warning,
                f"Binary and model OK; server starts on Listen ({model})",
                "Press Listen or refresh after first use.",
            )
            return
        self._set_status(
            "whisper",
            ServiceHealth.Checking,
            f"Checking {self.whisperBaseUrl().toString()}…",
        )
        url = self.whisperBaseUrl().resolved(QUrl("/health"))
        reply = self._network.get(QNetworkRequest(url))
        reply.finished.connect(self._on_whisper_health_finished)

    def _on_whisper_health_finished(self) -> None:
        reply = self.sender()
        if not isinstance(reply, QNetworkReply):
            return
        reply.deleteLater()
        model = self._paths.resolvedWhisperModel()
        if reply.error() != QNetworkReply.NetworkError.NoError:
            self._set_status(
                "whisper",
                ServiceHealth.Warning,
                f"Running but /health failed: {reply.errorString()}",
                f"Check firewall; model {model}",
            )
            return
        self._set_status(
            "whisper",
            ServiceHealth.Ok,
            f"{self._paths.whisperServerExe()} | lang={self._whisper_lang} | {model}",
        )

    def refreshAll(self) -> None:
        self._seed_placeholders()
        self.statusesChanged.emit()
        exe = self._paths.whisperServerExe()
        model = self._paths.resolvedWhisperModel()
        if exe and exe.is_file() and model and model.is_file():
            lang = self._config.sttLanguageMode()
            if lang not in ("auto", "en", "hi"):
                lang = "auto"
            self.ensureWhisperServer(lang)
        self._probe_whisper_health()
        piper = self._paths.piperExecutable()
        if not piper:
            self._set_status(
                "piper",
                ServiceHealth.Warning,
                "Piper executable not found",
                str(self._paths.toolsDir()),
            )
        else:
            self._set_status("piper", ServiceHealth.Ok, str(piper))
        self._probe_ollama()

    def _probe_ollama(self) -> None:
        self._set_status(
            "ollama",
            ServiceHealth.Checking,
            f"Checking {self._config.llmBaseUrl()}…",
        )
        url = QUrl(f"{self._config.llmBaseUrl()}/api/version")
        reply = self._network.get(QNetworkRequest(url))
        reply.finished.connect(self._on_ollama_probe_finished)

    def _on_ollama_probe_finished(self) -> None:
        reply = self.sender()
        if not isinstance(reply, QNetworkReply):
            return
        reply.deleteLater()
        if reply.error() != QNetworkReply.NetworkError.NoError:
            self._set_status(
                "ollama",
                ServiceHealth.Error,
                f"Unreachable: {reply.errorString()}",
                "Start Ollama (ollama serve) and confirm llm.base_url in config.",
            )
            return
        doc = json.loads(reply.readAll().data())
        version = doc.get("version", "") if isinstance(doc, dict) else ""
        detail = f"Version {version}" if version else "Reachable"
        self._set_status("ollama", ServiceHealth.Ok, detail)
