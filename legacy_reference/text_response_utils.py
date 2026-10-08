"""Clean and shorten assistant replies for display and TTS."""

import re

# ([theguardian.com])(https://...) or [text](url)
_MD_PAREN_LINK = re.compile(r"\(\[([^\]]+)\]\)\(([^)]+)\)", re.IGNORECASE)
_MD_LINK = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")
_URL = re.compile(r"https?://\S+")
_UTM_TAIL = re.compile(r"\?utm_source=\S+", re.IGNORECASE)
_BOLD = re.compile(r"\*+([^*]+)\*+")
_SENTENCE_END = re.compile(r"[.!?।]")


def _source_label(domain_or_text):
    label = (domain_or_text or "").strip()
    label = label.replace("www.", "").split("/")[0]
    name = label.split(".")[0]
    return name.replace("-", " ").title() if name else ""


_INTERVIEW_TAIL = re.compile(
    r"(want me to (pull|get|fetch)|would you like|let me know if).*$",
    re.IGNORECASE | re.DOTALL,
)


def strip_urls_and_markdown(text):
    """Remove speakable URLs; keep short source names for display."""
    if not text:
        return text

    def _paren_repl(match):
        label = _source_label(match.group(1))
        return f" ({label})" if label else ""

    def _md_repl(match):
        link_text, url = match.group(1), match.group(2)
        if url.startswith("http"):
            label = _source_label(link_text) or _source_label(url)
            return f" ({label})" if label else ""
        return link_text

    text = _MD_PAREN_LINK.sub(_paren_repl, text)
    text = _MD_LINK.sub(_md_repl, text)
    text = _URL.sub("", text)
    text = _UTM_TAIL.sub("", text)
    text = _BOLD.sub(r"\1", text)
    text = re.sub(r"`+", "", text)
    text = re.sub(r"\(\s*\)", "", text)
    text = re.sub(r"  +", " ", text)
    text = _INTERVIEW_TAIL.sub("", text).strip()
    return text


def truncate_to_word_limit(text, word_limit):
    if not text or word_limit <= 0:
        return text or ""
    words = text.split()
    if len(words) <= word_limit:
        return text.strip()
    cut = " ".join(words[:word_limit])
    for i in range(len(cut) - 1, max(0, int(len(cut) * 0.45)), -1):
        if cut[i] in ".!?।":
            return cut[: i + 1].strip()
    if cut and cut[-1] not in ".!?।":
        cut += "."
    return cut.strip()


def extract_complete_sentences(buffer):
    """
    Pull finished sentences from a streaming buffer.
    Returns (sentences, remainder) — remainder has no terminal punctuation yet.
    """
    if not buffer:
        return [], ""
    sentences = []
    start = 0
    for m in re.finditer(r"[.!?।]+(?:\s+|$)", buffer):
        piece = buffer[start : m.end()].strip()
        if piece:
            sentences.append(piece)
        start = m.end()
    return sentences, buffer[start:]


def maybe_early_tts_chunk(buffer, min_words=10):
    """
    Low-latency: start speech before the first full stop.
    Takes the first ~min_words (prefer soft break at comma/;/:) when enough words exist.
    Returns (chunk_or_None, remainder).
    """
    buf = buffer or ""
    if not buf.strip():
        return None, buf
    # Soft break preferred (still early)
    m = re.match(r"^(.{12,}?)([,;:—–])\s+", buf)
    if m and len(m.group(1).split()) >= max(6, min_words - 2):
        chunk = (m.group(1) + m.group(2)).strip()
        return chunk, buf[m.end() :]
    words = buf.split()
    if len(words) < min_words:
        return None, buf
    chunk = " ".join(words[:min_words]).strip()
    rest = " ".join(words[min_words:]).strip()
    return chunk, rest


def split_into_sentences(text):
    """Split full text into sentence list (same rules as stream extractor)."""
    text = (text or "").strip()
    if not text:
        return []
    sentences, rem = extract_complete_sentences(text + (" " if text[-1] not in ".!?।" else ""))
    if rem.strip():
        sentences.append(rem.strip())
    return sentences


def prepare_assistant_reply(text, word_limit, is_realtime=False):
    """
    Returns (display_text, tts_text).
    Display may include short source names; TTS never reads URLs.
    """
    cleaned = strip_urls_and_markdown(text or "")
    cleaned = truncate_to_word_limit(cleaned, word_limit)
    if is_realtime and cleaned:
        lines = [ln.strip() for ln in re.split(r"\n+", cleaned) if ln.strip()]
        if len(lines) > 1:
            head = lines[0]
            bullets = [ln for ln in lines[1:] if re.match(r"^\d+[\).]", ln)]
            if bullets:
                cleaned = head + " " + " ".join(bullets[:3])
        cleaned = truncate_to_word_limit(cleaned, word_limit)
    tts_text = cleaned
    return cleaned, tts_text


def realtime_system_addon(word_limit):
    return (
        f" REAL-TIME WEB SEARCH: Use web results but reply in {word_limit} words or LESS. "
        "Spoken answer — short, natural, like talking to a friend. "
        "NEVER put URLs, markdown links, or ([site])(https://...) in your reply. "
        "Mention sources by name only (e.g. 'Bloomberg says…', 'The Guardian reports…'). "
        "For news lists: max 3 headlines, one short line each, no link syntax. "
        "No bullet essays, no 'want me to pull more', no extra questions at the end."
    )
