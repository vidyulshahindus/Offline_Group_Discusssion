from __future__ import annotations

import json
from copy import deepcopy
from pathlib import Path
from typing import Any

from PySide6.QtCore import QObject, Signal

from eaglegd._repo import resource_path


def _value_at(root: dict[str, Any], path: list[str]) -> Any:
    current: Any = root
    for key in path:
        if not isinstance(current, dict):
            return None
        current = current.get(key)
    return current


class AppConfig(QObject):
    changed = Signal(str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._file_path = ""
        self._document: dict[str, Any] = self.defaultDocument()

    @staticmethod
    def defaultDocument() -> dict[str, Any]:
        defaults_file = resource_path("config", "eagle_config.defaults.json")
        if defaults_file.is_file():
            with defaults_file.open(encoding="utf-8") as f:
                doc = json.load(f)
            if isinstance(doc, dict):
                return doc
        return {
            "config_version": 1,
            "profile": "school",
            "llm": {
                "backend": "ollama",
                "base_url": "http://127.0.0.1:11434",
                "model": "eagle-clm",
                "fallback_model": "qwen3.5:2b-q4_K_M",
            },
            "stt": {
                "server_exe": "",
                "model": "ggml-base.bin",
            },
            "tts": {
                "piper_exe": "",
                "voices_dir": "",
                "voices": {
                    "en": "en_US-lessac-medium",
                    "hi": "hi_IN-priyamvada-medium",
                },
            },
            "paths": {
                "tools_dir": "",
                "data_dir": "",
                "units_dir": "",
                "logs_dir": "",
            },
        }

    def ensureDefaults(self) -> None:
        defaults = self.defaultDocument()
        for key, value in defaults.items():
            if key not in self._document:
                self._document[key] = deepcopy(value)

    def load(self, file_path: str | Path) -> bool:
        self._file_path = str(file_path)
        path = Path(file_path)
        if not path.exists():
            self.ensureDefaults()
            return self.save(file_path)
        try:
            with path.open(encoding="utf-8") as f:
                doc = json.load(f)
        except (OSError, json.JSONDecodeError):
            self.ensureDefaults()
            return False
        if not isinstance(doc, dict):
            self.ensureDefaults()
            return False
        self._document = doc
        self.ensureDefaults()
        return True

    def save(self, file_path: str | Path) -> bool:
        path = Path(file_path)
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open("w", encoding="utf-8") as f:
                json.dump(self._document, f, indent=2)
                f.write("\n")
            return True
        except OSError:
            return False

    def filePath(self) -> str:
        return self._file_path

    def json(self) -> dict[str, Any]:
        return self._document

    def _string_at(self, path: list[str], fallback: str = "") -> str:
        value = _value_at(self._document, path)
        if isinstance(value, str):
            return value
        return fallback

    def _int_at(self, path: list[str], fallback: int) -> int:
        value = _value_at(self._document, path)
        if isinstance(value, (int, float)):
            return int(value)
        return fallback

    def _double_at(self, path: list[str], fallback: float) -> float:
        value = _value_at(self._document, path)
        if isinstance(value, (int, float)):
            return float(value)
        return fallback

    def profile(self) -> str:
        return self._string_at(["profile"], "school")

    def llmBaseUrl(self) -> str:
        return self._string_at(["llm", "base_url"], "http://127.0.0.1:11434")

    def llmModel(self) -> str:
        return self._string_at(["llm", "model"], "eagle-clm")

    def llmFallbackModel(self) -> str:
        return self._string_at(["llm", "fallback_model"], "qwen3.5:2b-q4_K_M")

    def sttServerExe(self) -> str:
        return self._string_at(["stt", "server_exe"])

    def sttModel(self) -> str:
        return self._string_at(["stt", "model"], "ggml-base.bin")

    def sttFallbackModel(self) -> str:
        return self._string_at(["stt", "fallback_model"], "ggml-base.bin")

    def sttHost(self) -> str:
        return self._string_at(["stt", "host"], "127.0.0.1")

    def sttPort(self) -> int:
        return self._int_at(["stt", "port"], 8178)

    def sttThreads(self) -> int:
        return self._int_at(["stt", "threads"], 0)

    def sttLanguageMode(self) -> str:
        return self._string_at(["stt", "language_mode"], "auto")

    def sttUrduScriptAsHindi(self) -> bool:
        value = _value_at(self._document, ["stt", "urdu_script_as_hindi"])
        return True if value is None else bool(value)

    def sttDevanagariLang(self) -> str:
        return self._string_at(["stt", "devanagari_lang"], "hi")

    def sttBiasTerms(self) -> list[str]:
        arr = _value_at(self._document, ["stt", "bias_terms"])
        if not isinstance(arr, list):
            return []
        return [str(v) for v in arr]

    def sttVadFrameMs(self) -> int:
        return self._int_at(["stt", "vad", "frame_ms"], 30)

    def sttVadStartThresholdDb(self) -> float:
        return self._double_at(["stt", "vad", "start_threshold_db"], 9.0)

    def sttVadEndHangoverMs(self) -> int:
        return self._int_at(["stt", "vad", "end_hangover_ms"], 700)

    def sttVadMinSegmentMs(self) -> int:
        return self._int_at(["stt", "vad", "min_segment_ms"], 500)

    def sttVadMaxSegmentMs(self) -> int:
        return self._int_at(["stt", "vad", "max_segment_ms"], 12000)

    def sttVadPreRollMs(self) -> int:
        return self._int_at(["stt", "vad", "pre_roll_ms"], 200)

    def audioMuteMicWhileSpeaking(self) -> bool:
        value = _value_at(self._document, ["audio", "mute_mic_while_speaking"])
        return True if value is None else bool(value)

    def ttsPiperExe(self) -> str:
        return self._string_at(["tts", "piper_exe"])

    def ttsVoicesDir(self) -> str:
        return self._string_at(["tts", "voices_dir"])

    def ttsVoiceEn(self) -> str:
        return self._string_at(["tts", "voices", "en"], "en_US-lessac-medium")

    def ttsVoiceHi(self) -> str:
        return self._string_at(["tts", "voices", "hi"], "hi_IN-priyamvada-medium")

    def ttsBackend(self) -> str:
        return self._string_at(["tts", "backend"], "piper_cli")

    def ttsLengthScaleDefault(self) -> float:
        return self._double_at(["tts", "length_scale", "default"], 1.1)

    def ttsLengthScaleHi(self) -> float:
        return self._double_at(["tts", "length_scale", "hi"], 1.15)

    def ttsFillerAfterMs(self) -> int:
        return self._int_at(["tts", "filler_after_ms"], 3500)

    def ttsFillersEnabled(self) -> bool:
        value = _value_at(self._document, ["tts", "fillers_enabled"])
        return True if value is None else bool(value)

    def ttsVolume(self) -> float:
        return self._double_at(["tts", "volume"], 0.7)

    def ttsSentenceSilenceS(self) -> float:
        return self._double_at(["tts", "sentence_silence_s"], 0.2)

    def audioPlaybackBackend(self) -> str:
        return self._string_at(["audio", "backend"], "mediaplayer")

    def pathsToolsDir(self) -> str:
        return self._string_at(["paths", "tools_dir"])

    def pathsDataDir(self) -> str:
        return self._string_at(["paths", "data_dir"])

    def pathsUnitsDir(self) -> str:
        return self._string_at(["paths", "units_dir"])

    def pathsLogsDir(self) -> str:
        return self._string_at(["paths", "logs_dir"])

    def schoolAvoidMetaSchoolWords(self) -> bool:
        value = _value_at(self._document, ["school", "avoid_meta_school_words"])
        return True if value is None else bool(value)

    def llmFirstSentenceHint(self) -> bool:
        value = _value_at(self._document, ["llm", "first_sentence_hint"])
        return True if value is None else bool(value)

    def llmKeepAlive(self) -> int:
        return self._int_at(["llm", "keep_alive"], -1)

    def llmNumCtx(self) -> int:
        return self._int_at(["llm", "num_ctx"], 4096)

    def llmNumThread(self) -> int:
        return self._int_at(["llm", "num_thread"], 0)

    def llmNumBatch(self) -> int:
        return self._int_at(["llm", "num_batch"], 256)

    def llmRequestTimeoutMs(self) -> int:
        return self._int_at(["llm", "request_timeout_ms"], 60000)

    def llmHistoryBudgetTokens(self) -> int:
        return self._int_at(["llm", "history_budget_tokens"], 2200)

    def llmHistoryTrimTokens(self) -> int:
        return self._int_at(["llm", "history_trim_to_tokens"], 1400)

    def llmMaxNewTurnTokens(self) -> int:
        return self._int_at(["llm", "max_new_turn_tokens"], 350)

    def llmTopP(self) -> float:
        return self._double_at(["llm", "top_p"], 0.9)

    def llmTopK(self) -> int:
        return self._int_at(["llm", "top_k"], 40)

    def llmRepeatPenalty(self) -> float:
        return self._double_at(["llm", "repeat_penalty"], 1.1)

    def llmTemperatureFor(self, mode_key: str) -> float:
        temps = _value_at(self._document, ["llm", "temperature"])
        if isinstance(temps, dict) and mode_key in temps:
            return float(temps[mode_key])
        return 0.6

    def discussionWordLimitOverflow(self) -> float:
        return self._double_at(["discussion", "word_limit_overflow"], 0.5)

    # --- snake_case aliases (used by curriculum/stt/llm ports) ---
    def school_avoid_meta_school_words(self) -> bool:
        return self.schoolAvoidMetaSchoolWords()

    def llm_first_sentence_hint(self) -> bool:
        return self.llmFirstSentenceHint()

    def llm_base_url(self) -> str:
        return self.llmBaseUrl()

    def llm_fallback_model(self) -> str:
        return self.llmFallbackModel()

    def llm_keep_alive(self) -> int:
        return self.llmKeepAlive()

    def llm_num_ctx(self) -> int:
        return self.llmNumCtx()

    def llm_num_thread(self) -> int:
        return self.llmNumThread()

    def llm_num_batch(self) -> int:
        return self.llmNumBatch()

    def llm_request_timeout_ms(self) -> int:
        return self.llmRequestTimeoutMs()

    def discussion_word_limit_overflow(self) -> float:
        return self.discussionWordLimitOverflow()

    def paths_tools_dir(self) -> str:
        return self.pathsToolsDir()

    def paths_data_dir(self) -> str:
        return self.pathsDataDir()

    def paths_units_dir(self) -> str:
        return self.pathsUnitsDir()

    def paths_logs_dir(self) -> str:
        return self.pathsLogsDir()

    def stt_server_exe(self) -> str:
        return self.sttServerExe()

    def stt_model(self) -> str:
        return self.sttModel()

    def stt_fallback_model(self) -> str:
        return self.sttFallbackModel()

    def stt_host(self) -> str:
        return self.sttHost()

    def stt_port(self) -> int:
        return self.sttPort()

    def stt_threads(self) -> int:
        return self.sttThreads()

    def stt_language_mode(self) -> str:
        return self.sttLanguageMode()

    def stt_urdu_script_as_hindi(self) -> bool:
        return self.sttUrduScriptAsHindi()

    def stt_devanagari_lang(self) -> str:
        return self.sttDevanagariLang()

    def stt_bias_terms(self) -> list[str]:
        return self.sttBiasTerms()

    def stt_vad_frame_ms(self) -> int:
        return self.sttVadFrameMs()

    def stt_vad_start_threshold_db(self) -> float:
        return self.sttVadStartThresholdDb()

    def stt_vad_end_hangover_ms(self) -> int:
        return self.sttVadEndHangoverMs()

    def stt_vad_min_segment_ms(self) -> int:
        return self.sttVadMinSegmentMs()

    def stt_vad_max_segment_ms(self) -> int:
        return self.sttVadMaxSegmentMs()

    def stt_vad_pre_roll_ms(self) -> int:
        return self.sttVadPreRollMs()

    def audio_mute_mic_while_speaking(self) -> bool:
        return self.audioMuteMicWhileSpeaking()

    def tts_piper_exe(self) -> str:
        return self.ttsPiperExe()

    def tts_voices_dir(self) -> str:
        return self.ttsVoicesDir()

    def tts_voice_en(self) -> str:
        return self.ttsVoiceEn()

    def tts_voice_hi(self) -> str:
        return self.ttsVoiceHi()

    def tts_fillers_enabled(self) -> bool:
        return self.ttsFillersEnabled()

    def tts_length_scale_default(self) -> float:
        return self.ttsLengthScaleDefault()

    def tts_length_scale_hi(self) -> float:
        return self.ttsLengthScaleHi()

    @property
    def file_path(self) -> str:
        return self.filePath()
