from __future__ import annotations

import hashlib
import json
import random
from pathlib import Path
from typing import TYPE_CHECKING

from PySide6.QtCore import QEventLoop

from eaglegd._repo import resource_path
from eaglegd.llm.PromptTypes import SessionLanguage

if TYPE_CHECKING:
    from eaglegd.app.AppConfig import AppConfig
    from eaglegd.app.Paths import Paths
    from eaglegd.tts.PiperCliBackend import PiperCliBackend


class FillerCache:
    def __init__(self) -> None:
        self._en_fillers: list[str] = []
        self._hi_fillers: list[str] = []
        self._paths: dict[str, str] = {}
        self._en_voice = ""
        self._hi_voice = ""
        self._cache_root = Path()
        self._en_scale = 1.1
        self._hi_scale = 1.15
        self._ready = False

    def load(self, config: AppConfig, paths: Paths) -> bool:
        self._en_voice = config.ttsVoiceEn()
        self._hi_voice = config.ttsVoiceHi()
        self._en_scale = config.ttsLengthScaleDefault()
        self._hi_scale = config.ttsLengthScaleHi()
        self._cache_root = paths.dataDir() / "cache/fillers"
        self._cache_root.mkdir(parents=True, exist_ok=True)

        fillers_path = resource_path("data", "lang", "fillers.json")
        if not fillers_path.is_file():
            fillers_path = paths.dataDir() / "lang/fillers.json"
        if not fillers_path.is_file():
            return False
        root = json.loads(fillers_path.read_text(encoding="utf-8"))
        self._en_fillers = [str(v) for v in root.get("en", [])]
        self._hi_fillers = [str(v) for v in root.get("hi", [])]
        return bool(self._en_fillers)

    def _cache_file_path(self, voice_id: str, length_scale: float, text: str) -> Path:
        digest = hashlib.sha1(text.encode("utf-8")).hexdigest()
        directory = self._cache_root / voice_id / f"{length_scale:.2f}"
        directory.mkdir(parents=True, exist_ok=True)
        return directory / f"{digest}.wav"

    def _synthesize_to_cache(
        self, worker: PiperCliBackend, text: str, wav_path: Path
    ) -> bool:
        if wav_path.is_file() and wav_path.stat().st_size > 44:
            self._paths[str(wav_path)] = str(wav_path)
            return True
        if not worker.isConfigured():
            return False

        from PySide6.QtCore import QTimer

        loop = QEventLoop()
        ok = False
        finished = False

        def on_ready(_turn, _idx, src: str, _ms: int) -> None:
            nonlocal ok, finished
            if finished:
                return
            finished = True
            wav_path.unlink(missing_ok=True)
            src_path = Path(src)
            if src_path.is_file():
                src_path.replace(wav_path)
                self._paths[str(wav_path)] = str(wav_path)
                ok = True
            loop.quit()

        def on_fail(*_args) -> None:
            nonlocal finished
            if finished:
                return
            finished = True
            loop.quit()

        worker.wavReady.connect(on_ready)
        worker.synthesisFailed.connect(on_fail)
        # Start after exec() so a synchronous failure cannot race quit()/exec().
        QTimer.singleShot(0, lambda: worker.synthesize(-1, 0, text))
        QTimer.singleShot(20000, loop.quit)
        loop.exec()
        try:
            worker.wavReady.disconnect(on_ready)
            worker.synthesisFailed.disconnect(on_fail)
        except (RuntimeError, TypeError):
            pass
        return ok

    def warmUp(self, en_worker: PiperCliBackend, hi_worker: PiperCliBackend) -> bool:
        if not en_worker.isConfigured() and not hi_worker.isConfigured():
            self._ready = False
            return False
        en_worker.startWorker()
        hi_worker.startWorker()
        all_ok = True
        for text in self._en_fillers:
            path = self._cache_file_path(self._en_voice, self._en_scale, text)
            all_ok = self._synthesize_to_cache(en_worker, text, path) and all_ok
        for text in self._hi_fillers:
            path = self._cache_file_path(self._hi_voice, self._hi_scale, text)
            all_ok = self._synthesize_to_cache(hi_worker, text, path) and all_ok
        self._ready = all_ok
        return all_ok

    def wavPathFor(self, language: SessionLanguage, index: int) -> str:
        key = self._cache_key(language, index)
        if not key:
            return ""
        path = Path(key)
        if path.is_file():
            return key
        return self._paths.get(key, "")

    def count(self, language: SessionLanguage) -> int:
        return len(self._hi_fillers if language == SessionLanguage.Hindi else self._en_fillers)

    def pickIndex(self, language: SessionLanguage, avoid_index: int) -> int:
        n = self.count(language)
        if n <= 0:
            return -1
        if n == 1:
            return 0
        idx = random.randrange(n)
        if idx == avoid_index:
            idx = (idx + 1) % n
        return idx

    def isReady(self) -> bool:
        return self._ready

    def _cache_key(self, language: SessionLanguage, index: int) -> str:
        voice = self._hi_voice if language == SessionLanguage.Hindi else self._en_voice
        scale = self._hi_scale if language == SessionLanguage.Hindi else self._en_scale
        fillers = self._hi_fillers if language == SessionLanguage.Hindi else self._en_fillers
        if index < 0 or index >= len(fillers):
            return ""
        return str(self._cache_file_path(voice, scale, fillers[index]))
