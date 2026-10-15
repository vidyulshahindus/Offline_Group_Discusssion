"""Speech-to-text layer."""

from typing import TYPE_CHECKING

__all__ = [
    "AudioCapture",
    "PcmTo16kMono",
    "SttEngine",
    "Vad",
    "WhisperServerClient",
    "pcm16k_mono_to_wav",
]

_LAZY = {
    "AudioCapture": ("eaglegd.stt.audio_capture", "AudioCapture"),
    "PcmTo16kMono": ("eaglegd.stt.pcm_to_16k_mono", "PcmTo16kMono"),
    "SttEngine": ("eaglegd.stt.stt_engine", "SttEngine"),
    "Vad": ("eaglegd.stt.vad", "Vad"),
    "WhisperServerClient": ("eaglegd.stt.whisper_server_client", "WhisperServerClient"),
    "pcm16k_mono_to_wav": ("eaglegd.stt.wav_util", "pcm16k_mono_to_wav"),
}


def __getattr__(name: str):
    if name in _LAZY:
        module_name, attr = _LAZY[name]
        import importlib

        return getattr(importlib.import_module(module_name), attr)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


if TYPE_CHECKING:
    from eaglegd.stt.audio_capture import AudioCapture
    from eaglegd.stt.pcm_to_16k_mono import PcmTo16kMono
    from eaglegd.stt.stt_engine import SttEngine
    from eaglegd.stt.vad import Vad
    from eaglegd.stt.wav_util import pcm16k_mono_to_wav
    from eaglegd.stt.whisper_server_client import WhisperServerClient
