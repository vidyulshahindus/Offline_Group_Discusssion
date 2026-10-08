"""Mode router — Discussion and Conversational stay in separate files."""

import random

from . import discussion_mode, conversational_mode
from .persona import eagle_persona_instruction

MODE_REGISTRY = {
    discussion_mode.MODE_ID: discussion_mode,
    conversational_mode.MODE_ID: conversational_mode,
}


def get_mode_handler(mode_name="Discussion"):
    return MODE_REGISTRY.get(mode_name, discussion_mode)


def get_system_message(word_limit=50, language_name="English", mode_name="Discussion", memory_context=""):
    handler = get_mode_handler(mode_name)
    fn = handler.get_system_message
    try:
        return fn(word_limit, language_name, memory_context)
    except TypeError:
        return fn(word_limit, language_name)


def get_completion_temperature(mode_name="Discussion"):
    fn = getattr(get_mode_handler(mode_name), "get_completion_temperature", None)
    return fn() if fn else None


def get_phrase_file(mode_name="Discussion"):
    return get_mode_handler(mode_name).get_phrase_file()


def should_run_follow_up(gui, mode_name="Discussion"):
    return get_mode_handler(mode_name).should_run_follow_up(gui)


def get_tts_delivery_tag(sentence, index=0, total=1, mode_name="Discussion"):
    fn = getattr(get_mode_handler(mode_name), "infer_delivery_tag", None)
    return fn(sentence, index, total) if fn else None


def get_tts_voice_settings(sentence, mode_name="Discussion", index=0, total=1):
    fn = getattr(get_mode_handler(mode_name), "voice_settings_for_sentence", None)
    if not fn:
        return None
    try:
        return fn(sentence, index, total)
    except TypeError:
        return fn(sentence)


def get_tts_intro_voice_settings(mode_name="Discussion"):
    fn = getattr(get_mode_handler(mode_name), "intro_voice_settings", None)
    return fn() if fn else None


def use_tts_intro(mode_name="Discussion"):
    fn = getattr(get_mode_handler(mode_name), "use_tts_intro", None)
    return fn() if fn else False


def resolve_engaging_phrase(openai_client, language_name, user_text, mode_name, load_phrases_fn):
    """Optional intro phrase before main TTS — disabled when use_tts_intro() is False."""
    if not use_tts_intro(mode_name):
        return None
    handler = get_mode_handler(mode_name)
    generate_fn = getattr(handler, "generate_engaging_phrase", None)
    if generate_fn:
        return generate_fn(openai_client, language_name, user_text)
    phrase_file = get_phrase_file(mode_name)
    if not phrase_file:
        return None
    phrases = load_phrases_fn(phrase_file, language_name)
    valid = [p.strip() for p in phrases if p and p.strip()]
    return random.choice(valid) if valid else None
