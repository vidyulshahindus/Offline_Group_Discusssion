"""Text-to-speech layer."""

from typing import TYPE_CHECKING

__all__ = [
    "AudioPlayer",
    "FillerCache",
    "PiperCliBackend",
    "SpeechQueuePlayer",
    "TtsEngine",
]

_LAZY = {
    "AudioPlayer": ("eaglegd.tts.audio_player", "AudioPlayer"),
    "FillerCache": ("eaglegd.tts.filler_cache", "FillerCache"),
    "PiperCliBackend": ("eaglegd.tts.piper_cli_backend", "PiperCliBackend"),
    "SpeechQueuePlayer": ("eaglegd.tts.speech_queue_player", "SpeechQueuePlayer"),
    "TtsEngine": ("eaglegd.tts.tts_engine", "TtsEngine"),
}


def __getattr__(name: str):
    if name in _LAZY:
        module_name, attr = _LAZY[name]
        import importlib

        return getattr(importlib.import_module(module_name), attr)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


if TYPE_CHECKING:
    from eaglegd.tts.audio_player import AudioPlayer
    from eaglegd.tts.filler_cache import FillerCache
    from eaglegd.tts.piper_cli_backend import PiperCliBackend
    from eaglegd.tts.speech_queue_player import SpeechQueuePlayer
    from eaglegd.tts.tts_engine import TtsEngine
