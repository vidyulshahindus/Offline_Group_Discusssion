"""GUI model selection — Legacy (chat) vs Real Time (Responses + web search)."""

import os
import logging

from modes import get_completion_temperature

LEGACY_LABEL = "Legacy"
REALTIME_LABEL = "Real Time"

WEB_SEARCH_TOOL = {"type": "web_search", "search_context_size": "low"}


def get_model_selection(gui):
    return getattr(gui, "model_selection", LEGACY_LABEL) or LEGACY_LABEL


def is_realtime_mode(gui):
    return get_model_selection(gui) == REALTIME_LABEL


def get_legacy_model():
    return os.getenv("OPENAI_LEGACY_MODEL", "gpt-4o-mini")


def get_realtime_model():
    return os.getenv("OPENAI_REALTIME_MODEL", "gpt-5.2")


def _split_history(messages):
    instructions = ""
    input_items = []
    for msg in messages or []:
        role = msg.get("role")
        content = (msg.get("content") or "").strip()
        if not content:
            continue
        if role == "system":
            instructions = content
        elif role in ("user", "assistant"):
            input_items.append({"role": role, "content": content})
    return instructions, input_items


def _stream_chat_completions(openai_client, conversation_history, adjusted_token_limit, interaction_mode):
    kwargs = {
        "model": get_legacy_model(),
        "messages": conversation_history,
        "max_tokens": adjusted_token_limit,
        "stream": True,
    }
    temp = get_completion_temperature(interaction_mode)
    if temp is not None:
        kwargs["temperature"] = temp
    logging.info(f"Legacy model: {kwargs['model']}")
    stream = openai_client.chat.completions.create(**kwargs)
    for chunk in stream:
        if (
            chunk.choices
            and chunk.choices[0].delta is not None
            and hasattr(chunk.choices[0].delta, "content")
        ):
            text_chunk = chunk.choices[0].delta.content
            if text_chunk:
                yield text_chunk


def _stream_responses_web(openai_client, conversation_history, adjusted_token_limit, interaction_mode):
    model = get_realtime_model()
    instructions, input_items = _split_history(conversation_history)
    if not input_items:
        input_items = [{"role": "user", "content": "Hello"}]

    kwargs = {
        "model": model,
        "instructions": instructions or None,
        "input": input_items,
        "tools": [WEB_SEARCH_TOOL],
        "max_output_tokens": adjusted_token_limit,
        "stream": True,
    }
    temp = get_completion_temperature(interaction_mode)
    if temp is not None:
        kwargs["temperature"] = temp

    logging.info(f"Real Time model: {model} (web_search enabled)")
    try:
        stream = openai_client.responses.create(**kwargs)
        for event in stream:
            if getattr(event, "type", None) == "response.output_text.delta":
                delta = getattr(event, "delta", "") or ""
                if delta:
                    yield delta
    except Exception as exc:
        logging.warning(f"Responses stream failed ({exc}); trying non-streaming fallback")
        kwargs.pop("stream", None)
        response = openai_client.responses.create(**kwargs)
        text = getattr(response, "output_text", None) or ""
        if text:
            yield text


def stream_assistant_reply(openai_client, gui, conversation_history, adjusted_token_limit, interaction_mode):
    """Yield assistant text chunks — Legacy or Real Time; everything else unchanged."""
    if is_realtime_mode(gui):
        yield from _stream_responses_web(
            openai_client, conversation_history, adjusted_token_limit, interaction_mode
        )
    else:
        yield from _stream_chat_completions(
            openai_client, conversation_history, adjusted_token_limit, interaction_mode
        )
