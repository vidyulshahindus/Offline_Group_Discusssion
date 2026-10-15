from __future__ import annotations

import logging
import sys
from datetime import datetime
from pathlib import Path
from threading import Lock

from PySide6.QtCore import QDateTime, QLoggingCategory, QtMsgType, qInstallMessageHandler
from PySide6.QtCore import QStandardPaths

_lock = Lock()
_log_file: Path | None = None
_log_dir = ""
_log_path = ""


def _try_open_log(logs_dir: Path) -> bool:
    global _log_file, _log_dir, _log_path
    if not logs_dir:
        return False
    try:
        logs_dir.mkdir(parents=True, exist_ok=True)
    except OSError:
        return False
    stamp = QDateTime.currentDateTime().toString("yyyyMMdd")
    path = logs_dir / f"eaglegd_{stamp}.log"
    try:
        handle = path.open("a", encoding="utf-8")
    except OSError:
        return False
    _log_file = path
    _log_dir = str(logs_dir.resolve())
    _log_path = str(path.resolve())
    handle.close()
    return True


def _message_handler(msg_type: QtMsgType, context, message: str) -> None:
    levels = {
        QtMsgType.QtDebugMsg: "DEBUG",
        QtMsgType.QtInfoMsg: "INFO",
        QtMsgType.QtWarningMsg: "WARN",
        QtMsgType.QtCriticalMsg: "CRIT",
        QtMsgType.QtFatalMsg: "FATAL",
    }
    level = levels.get(msg_type, "INFO")
    category = context.category or "default"
    line = (
        f"[{QDateTime.currentDateTime().toString('yyyy-MM-ddTHH:mm:ss.zzz')}] "
        f"{level} {category} {message}"
    )
    with _lock:
        if _log_file is not None:
            try:
                with _log_file.open("a", encoding="utf-8") as f:
                    f.write(line + "\n")
            except OSError:
                pass
    print(line, file=sys.stderr, flush=True)
    if msg_type == QtMsgType.QtFatalMsg:
        raise SystemExit(1)


class EagleLog:
    @staticmethod
    def install(preferred_logs_dir: str | Path) -> None:
        QLoggingCategory.setFilterRules(
            "*.debug=false\n"
            "eagle.*=true\n"
            "eagle.*.debug=true"
        )
        local_logs = Path(
            QStandardPaths.writableLocation(
                QStandardPaths.StandardLocation.AppLocalDataLocation
            )
            or ""
        ) / "logs"
        roaming_logs = Path(
            QStandardPaths.writableLocation(
                QStandardPaths.StandardLocation.AppDataLocation
            )
            or ""
        ) / "logs"
        temp_logs = Path(
            QStandardPaths.writableLocation(QStandardPaths.StandardLocation.TempLocation)
            or "/tmp"
        ) / "EagleGD/logs"
        candidates = [Path(preferred_logs_dir), local_logs, roaming_logs, temp_logs]
        opened = False
        for directory in candidates:
            if directory and _try_open_log(directory):
                opened = True
                break
        qInstallMessageHandler(_message_handler)
        logging.basicConfig(level=logging.INFO)
        if opened:
            logging.getLogger("eagle").info("Eagle GD logging to %s", _log_path)
        else:
            logging.getLogger("eagle").warning(
                "Eagle GD could not open a log file; messages go to stderr only"
            )

    @staticmethod
    def shutdown() -> None:
        qInstallMessageHandler(None)

    @staticmethod
    def directory() -> str:
        return _log_dir

    @staticmethod
    def filePath() -> str:
        return _log_path
