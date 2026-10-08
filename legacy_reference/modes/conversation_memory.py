"""Memory + intent/emotion detection for conversational mode."""

import re

from modes.conversation_state import get_conversation_state

_SAD = re.compile(
    r"(sad|upset|depressed|lonely|low\b|unwell|sick|hurt|cry|anxious|stressed|worried|feel bad|"
    r"उदास|दुखी|दुख|परेशान|बीमार|तकलीफ|अकेला|चिंता|तनाव|मन खराब)",
    re.IGNORECASE,
)
_HAPPY = re.compile(
    r"(happy|excited|great|awesome|fun|laugh|joy|amazing|"
    r"खुश|मजा|हंस|अच्छा लग|बढ़िया)",
    re.IGNORECASE,
)
_FRUSTRATED = re.compile(
    r"(frustrat|annoyed|angry|pissed|fed up|repeating|same question|not listening|"
    r"you already asked|नाराज|गुस्सा|बार बार)",
    re.IGNORECASE,
)
_CHEER_UP = re.compile(
    r"(cheer me up|feel better|lighten|cheer up|make me feel|"
    r"खुश कर|हँसा|मन हल्का)",
    re.IGNORECASE,
)
_ASK_EAGLE = re.compile(
    r"(you (ask|initiate|start)|ask me something|your turn to ask|you pick|pick (a )?topic|"
    r"tell me something|say something|you lead|my turn to listen|"
    r"तुम पूछो|कुछ पूछो|तुम शुरू|तुम बताओ)",
    re.IGNORECASE,
)
_NOT_LISTENING = re.compile(
    r"(not (talking|listening|understanding|exciting)|boring|you are boring|not exciting|"
    r"random thing|what i (said|asked|meant)|not what i|you are not talking|"
    r"ask too (much|many)|too many question|stop asking|"
    r"hurting me|you (are )?hurting|that hurt|"
    r"सुन नहीं|बकवास बोल|बोर|ज्यादा सवाल)",
    re.IGNORECASE,
)
_TOO_MANY_QUESTIONS = re.compile(
    r"(ask too (much|many)|too many question|stop asking|why (do you|are you) ask|"
    r"interview|सवाल मत|बहुत सवाल)",
    re.IGNORECASE,
)
_BORING = re.compile(
    r"(boring|not exciting|dull|you are boring|exciting me|entertain me|"
    r"बोर|मजा नहीं|रोचक नहीं)",
    re.IGNORECASE,
)
_HURTING = re.compile(
    r"(hurting me|you (are )?hurting|that hurt|you hurt|दिल दुख|चोट)",
    re.IGNORECASE,
)
_INITIATE = re.compile(
    r"(you initiate|you start|your turn|you pick|pick (a )?topic|you lead|"
    r"tell me something interesting|say something|तुम शुरू|तुम बताओ)",
    re.IGNORECASE,
)
_LEARNING = re.compile(
    r"(explain|how does|how do|what is|why does|teach me|learn|help me understand|meaning of|"
    r"समझाओ|क्या है|कैसे (?:काम|होता|बनता|करें|करते)|सिखाओ|मतलब)",
    re.IGNORECASE,
)
_GREETING = re.compile(
    r"(?:^|\b)(?:नमस्ते|नमस्कार|namaste|hello|hi\b|hey\b|"
    r"good\s+(?:morning|evening|afternoon|night)|how are you|what'?s up|"
    r"कैसे\s+(?:हो|हैं|हुआ|हा)|कैस[ाी]\s+(?:है|हो)|क्या\s+हाल|कैसा\s+चल|सुप्रभात|शुभ)",
    re.IGNORECASE,
)
_HOW_ARE_YOU = re.compile(
    r"कैसे\s+(?:हो|हैं|हुआ|हा)|कैस[ाी]\s+(?:है|हो)|how are you|how'?s it going|"
    r"kaise ho|क्या\s+हाल|कैसा\s+चल",
    re.IGNORECASE,
)
_WEATHER_FILLER = re.compile(r"मौसम|weather|धूप|बारिश|season", re.IGNORECASE)
_BOUNCE_BACK_Q = re.compile(
    r"(तुम कैसे|आप कैसे|तू कैसा|how are you|how'?s it going|kaise ho)",
    re.IGNORECASE,
)
_TAG_QUESTION = re.compile(r"है\s*ना\??\s*$", re.I)
_INFO = re.compile(
    r"(tell me about|what do you know|who is|when did|where is|"
    r"बताओ|जानकारी)",
    re.IGNORECASE,
)
_HUMOR = re.compile(
    r"(joke|funny|haha|lol|make me laugh|roast me|"
    r"मजाक|हंसा|ठीक है न)",
    re.IGNORECASE,
)
_VALIDATION = re.compile(
    r"(am i (right|wrong)|was i wrong|did i do (the )?right|don't you think|agree with me|"
    r"is it (ok|okay|fine)|validate|fair of me|was that (ok|bad)|correct me|"
    r"क्या मैं सही|सही कह|गलत तो नहीं)",
    re.IGNORECASE,
)
_DEBATE = re.compile(
    r"(disagree|you're wrong|i think you|argue|debate|versus|\bvs\b|counter|challenge|"
    r"but what about|don't you agree|खिलाफ|बहस|गलत है)",
    re.IGNORECASE,
)
_DEEP_DISCUSSION = re.compile(
    r"(what do you think about|your opinion on|philosophy|meaning of|deep down|"
    r"why do (we|people)|what if|bigger picture|existential|ethics of|"
    r"तुम्हारा क्या ख्याल|गहरा|ज़िंदगी)",
    re.IGNORECASE,
)
_EMOTIONAL_CONN = re.compile(
    r"(feel alone|need (someone|to talk)|just venting|nobody understands|miss (him|her|them)|"
    r"feel low|feeling down|need a friend|listen to me|"
    r"अकेला|कोई नहीं|सुनो|दिल की बात)",
    re.IGNORECASE,
)
_INSULT = re.compile(
    r"(fuck|shit|damn|asshole|screw you|hate you|bullshit|idiot|moron|stupid|dumb|"
    r"bakwas|बकवास|बेवकूफ|गधा|पागल|कमीने|उल्लू)",
    re.IGNORECASE,
)
_PLAYFUL_MARKERS = re.compile(
    r"(haha|hehe|lol|lmao|rofl|😂|🤣|😜|just kidding|kidding|jk\b|mazaak|मजाक|मज़ाक|"
    r"चुगली|चिढ़ा|teas(e|ing)|banter|यार\b|यार!|arre|अरे|अरे वाह|हंस|hansi|"
    r"funny|joke|शोले|sholay|filmy|filmi)",
    re.IGNORECASE,
)
_REAL_ANGER = re.compile(
    r"(i hate you|hate you|go away|leave me|shut up|never talk|you suck|worst|"
    r"तुमसे नफरत|छोड़ो|बकवास मत|गुस्सा|नाराज हूँ|बहुत गुस्सा)",
    re.IGNORECASE,
)
_ADULT_QUERY = re.compile(
    r"(porn|porno|xxx|nude|naked|nsfw|erotic|sex\b|sexual|adultery|"
    r"सेक्स|अश्लील|पर्न)",
    re.IGNORECASE,
)
_SHARES_INTEREST = re.compile(
    r"(i love|i like|i enjoy|my hobby|favourite|favorite|passion|"
    r"मुझे .+ पसंद|शौक|पसंद है)",
    re.IGNORECASE,
)
_NAME = re.compile(
    r"(?:my name is|call me|मेरा नाम है?)\s+([A-Za-z\u0900-\u097F]{2,20})",
    re.IGNORECASE,
)

# Iconic quotes / pop culture — (pattern, turn guidance)
_CULTURAL_REFS = (
    (
        re.compile(
            r"बसंती|basanti|तुम्हारा नाम क्या है|tumhara naam kya hai|naam kya hai basanti",
            re.IGNORECASE,
        ),
        (
            "SHOLAY (1975) — Dharmendra's famous teasing line to Hema Malini's character Basanti. "
            "User is quoting the film playfully, NOT literally asking your name. "
            "Play along with Sholay wit (horse-cart, Veeru–Basanti, maybe light Gabbar nod). "
            "Say you're Eagle only in passing — NO formal 'proud of my name like a bird' speech."
        ),
    ),
    (
        re.compile(r"कितने आदमी|kitne aadmi the|kitne aadmi", re.IGNORECASE),
        (
            "SHOLAY — Gabbar Singh's iconic 'Kitne aadmi the?' "
            "Play along with film humor; user wants banter, not a headcount lesson."
        ),
    ),
    (
        re.compile(r"मोगैम्बो|mogambo|खुश हुआ", re.IGNORECASE),
        "MR. INDIA — 'Mogambo khush hua' quote. Match the playful film-reference energy.",
    ),
    (
        re.compile(r"pushpa.*नाम|pushpa.*naam|पुष्पा.*नाम", re.IGNORECASE),
        "PUSHPA — famous 'Pushpa, Pushpa' name dialogue. Playful film banter, not a literal name Q&A.",
    ),
    (
        re.compile(r"all is well|ऑल इज वेल|अल इज वेल", re.IGNORECASE),
        "3 IDIOTS — 'All is well' reference. Light, warm, culturally aware reply.",
    ),
    (
        re.compile(r"बाबू मोशाइ|babu moshai|babu moshay", re.IGNORECASE),
        "Classic Indian cinema (Ray/Bollywood) — 'Babu moshai' quote. Acknowledge with cultural wit.",
    ),
    (
        re.compile(r"मैं नहीं मानता|main nahi manta|gabbar", re.IGNORECASE),
        "SHOLAY / Gabbar Singh territory — iconic villain lines. Play along, don't lecture.",
    ),
)

_LITERAL_NAME_Q = re.compile(
    r"(what(?:'s| is) your name|your name\??|तुम्हारा नाम|आपका नाम|तेरा नाम)",
    re.IGNORECASE,
)

_EMOTION_LABELS = {
    "sad": "sad",
    "happy": "happy",
    "frustrated": "frustrated",
    "playful": "playful",
    "neutral": "neutral",
    "support_needed": "support_needed",
}

_USER_SEEKS = (
    "information",
    "emotional_connection",
    "debate",
    "humor",
    "validation",
    "deep_discussion",
    "casual_chat",
)


_CULTURAL_UNCERTAINTY = (
    " If not 100% sure of the exact reference, guess lightly and invite correction — "
    "never claim wrong movie/character/lyric details."
)

def user_asked_how_eagle_is(text):
    """User asked how Eagle is — reciprocal 'how are you' back is natural."""
    if not text:
        return False
    return bool(_HOW_ARE_YOU.search(text.strip()))


def is_simple_greeting_only(text):
    """Hello/namaste only — no how-are-you; no question needed back."""
    if not text or user_asked_how_eagle_is(text):
        return False
    t = text.strip()
    if len(t) > 60:
        return False
    return bool(_GREETING.search(t))


def is_greeting_or_how_are_you(text):
    """Any greeting including how-are-you."""
    if not text:
        return False
    t = text.strip()
    if len(t) > 90:
        return False
    if _GREETING.search(t):
        return True
    if len(t.split()) <= 8 and re.search(r"कैस[ेी]|how are|kaise", t, re.I):
        if not _LEARNING.search(t) and not _INFO.search(t):
            return True
    return False


def _split_sentences(text):
    return [p.strip() for p in re.split(r"(?<=[.!?।])\s+", text.strip()) if p.strip()]


def _strip_all_questions(reply):
    parts = _split_sentences(reply)
    kept = []
    for p in parts:
        if p.endswith("?") or _TAG_QUESTION.search(p):
            continue
        if _BOUNCE_BACK_Q.search(p):
            continue
        if _WEATHER_FILLER.search(p):
            continue
        kept.append(p)
    if kept:
        result = " ".join(kept)
        if result[-1] not in ".!?":
            result += "."
        return result
    first = parts[0] if parts else reply.strip()
    first = re.sub(r"\?[^.!?]*$", ".", first)
    if first and first[-1] not in ".!?":
        first += "."
    return first


def _sanitize_wellbeing_greeting_reply(reply):
    """Keep warm answer + at most ONE 'how are you' back; drop filler."""
    parts = _split_sentences(reply)
    statements = []
    reciprocal = None
    for p in parts:
        if p.endswith("?"):
            if reciprocal is None and _BOUNCE_BACK_Q.search(p):
                reciprocal = p
            continue
        if _TAG_QUESTION.search(p):
            continue
        if _WEATHER_FILLER.search(p):
            continue
        statements.append(p)
    result = " ".join(statements).strip()
    if reciprocal:
        result = f"{result} {reciprocal}".strip() if result else reciprocal
    if result and result[-1] not in ".!?":
        result += "."
    return result or reply.strip()


def sanitize_conversational_reply(user_text, reply, force_no_questions=False):
    """Strip interview-style questions; allow one reciprocal how-are-you when appropriate."""
    if not reply or not reply.strip():
        return reply
    reply = reply.strip()
    if user_asked_how_eagle_is(user_text):
        return _sanitize_wellbeing_greeting_reply(reply)
    if force_no_questions or is_simple_greeting_only(user_text):
        return _strip_all_questions(reply)
    if force_no_questions:
        return _strip_all_questions(reply)
    return reply


def detect_cultural_reference(text):
    """Return guidance string if user message matches a known cultural quote/reference."""
    if not text:
        return None
    for pattern, guidance in _CULTURAL_REFS:
        if pattern.search(text):
            g = guidance if isinstance(guidance, str) else guidance
            return g + _CULTURAL_UNCERTAINTY
    return None


def is_playful_teasing(text):
    """Rude-looking words used in banter, not real anger."""
    if not text:
        return False
    if detect_cultural_reference(text):
        return True
    if _PLAYFUL_MARKERS.search(text):
        return True
    if _HUMOR.search(text):
        return True
    # Short sharp line without anger markers — likely teasing
    if _INSULT.search(text) and not _REAL_ANGER.search(text) and len(text.split()) <= 12:
        return True
    return False


def is_real_frustration(text):
    """Genuine upset — not playful banter."""
    if not text:
        return False
    if is_playful_teasing(text) and not _REAL_ANGER.search(text):
        return False
    if _REAL_ANGER.search(text):
        return True
    if _HURTING.search(text):
        return True
    if _FRUSTRATED.search(text) and not _PLAYFUL_MARKERS.search(text):
        return True
    if _NOT_LISTENING.search(text) and not _PLAYFUL_MARKERS.search(text):
        return True
    return False


def detect_user_seek(text, emotion=None):
    """
    What the user is actually seeking — intention behind the words.
    Priority order matters when multiple signals appear.
    """
    if not text:
        return "casual_chat"
    if is_greeting_or_how_are_you(text):
        return "casual_chat"
    if detect_cultural_reference(text):
        return "humor"
    if _ADULT_QUERY.search(text):
        return "information"
    if is_playful_teasing(text) or (_INSULT.search(text) and not is_real_frustration(text)):
        return "humor"
    if _HUMOR.search(text) and emotion != "support_needed":
        return "humor"
    if (
        _SAD.search(text)
        or _CHEER_UP.search(text)
        or _EMOTIONAL_CONN.search(text)
        or emotion == "support_needed"
    ):
        return "emotional_connection"
    if _VALIDATION.search(text):
        return "validation"
    if _DEBATE.search(text):
        return "debate"
    if _DEEP_DISCUSSION.search(text):
        return "deep_discussion"
    if _LEARNING.search(text) or _INFO.search(text):
        return "information"
    if _HUMOR.search(text):
        return "humor"
    if _SHARES_INTEREST.search(text):
        return "emotional_connection"
    return "casual_chat"


def detect_style_feedback(text):
    """User criticism of Eagle's conversation style — triggers immediate adaptation."""
    flags = {}
    if not text:
        return flags
    if _TOO_MANY_QUESTIONS.search(text) or (
        _FRUSTRATED.search(text) and re.search(r"question|ask|repeat|same", text, re.I)
    ):
        flags["no_questions"] = True
    if _BORING.search(text):
        flags["be_more_exciting"] = True
    if _NOT_LISTENING.search(text) and not _BORING.search(text):
        flags["fix_listening"] = True
    if _HURTING.search(text):
        flags["soften_tone"] = True
    if _INITIATE.search(text) or _ASK_EAGLE.search(text):
        flags["take_initiative"] = True
    return flags


def detect_intent(text):
    """Alias — maps to user_seek for state tracking."""
    return detect_user_seek(text)


def _seek_guidance(seek):
    guides = {
        "information": (
            "They want INFORMATION — clear useful answer plus one vivid analogy or surprising fact. "
            "Do not over-empathize or ask unrelated questions."
        ),
        "emotional_connection": (
            "They want EMOTIONAL CONNECTION — be present like a friend, not a counselor. "
            "React specifically to what they said; avoid canned lines like 'I understand' or 'I'm here for you'."
        ),
        "debate": (
            "They want DEBATE — engage their idea thoughtfully. "
            "Respect their view, offer a reasoned perspective, no dismissiveness."
        ),
        "humor": (
            "They want HUMOR — playful, witty, light energy. "
            "Match the banter; no preaching or formal answers."
        ),
        "validation": (
            "They want VALIDATION — affirm their feeling or choice genuinely first. "
            "Then add gentle nuance only if truly helpful."
        ),
        "deep_discussion": (
            "They want DEEP DISCUSSION — thoughtful, nuanced exploration. "
            "Share your perspective, connect ideas, go beyond surface answers."
        ),
        "casual_chat": (
            "Casual chat — natural back-and-forth with light personality. "
            "Curious reaction or playful line when mood fits; respond to the vibe, not just keywords."
        ),
    }
    return guides.get(seek, guides["casual_chat"])


def detect_emotion(text):
    if not text:
        return "neutral"
    if _SAD.search(text):
        return "support_needed"
    if _CHEER_UP.search(text):
        return "support_needed"
    if is_real_frustration(text):
        return "frustrated"
    if is_playful_teasing(text) or (_INSULT.search(text) and not is_real_frustration(text)):
        return "playful"
    if detect_cultural_reference(text):
        return "happy"
    if _HAPPY.search(text) or _HUMOR.search(text):
        return "happy"
    return "neutral"


def _engagement_style_guidance(emotion, seek):
    """Per-turn personality layer — when to be witty vs thoughtful vs warm."""
    if emotion == "support_needed":
        return (
            "ENGAGEMENT STYLE: Warm and present — no jokes, no surprising facts, no forced cheer. "
            "One gentle relatable line is fine; empathy comes first."
        )
    if emotion == "frustrated":
        return (
            "ENGAGEMENT STYLE: Calm and grounded — acknowledge first, fix the thread. "
            "No humor this turn unless they clearly started joking."
        )
    if seek == "humor" or emotion == "playful":
        return (
            "ENGAGEMENT STYLE: Playful banter — one witty or cheeky line, match their energy. "
            "Not a comedy routine; keep it natural."
        )
    if seek == "deep_discussion":
        return (
            "ENGAGEMENT STYLE: Thoughtful reflection — connect ideas, share a perspective. "
            "No random jokes; depth over cleverness."
        )
    if seek == "information":
        return (
            "ENGAGEMENT STYLE: Teach clearly, then one vivid analogy or surprising fact. "
            "Make it memorable without being silly unless they seem playful."
        )
    if seek in ("casual_chat", "emotional_connection", "validation"):
        return (
            "ENGAGEMENT STYLE: Natural companion energy — curious reaction or relatable example. "
            "Light humor if mood fits; do NOT end with a question."
        )
    if seek == "debate":
        return (
            "ENGAGEMENT STYLE: Engaged and sharp — reasoned take, maybe one pointed observation. "
            "Respectful, not sarcastic unless they invited banter."
        )
    return (
        "ENGAGEMENT STYLE: Mix warmth with one small engaging touch — "
        "observation, example, or curiosity — matched to their vibe."
    )


def _emotion_style_guidance(emotion, seek):
    base = _seek_guidance(seek)
    if emotion == "support_needed" and seek != "humor":
        return (
            base
            + " MOOD: They seem low — acknowledge feelings FIRST. "
            "No immediate questions or solutions unless they asked."
        )
    if emotion == "frustrated":
        return (
            base
            + " MOOD: Frustrated — patient, brief apology if needed, fix the thread."
        )
    if emotion == "playful":
        return base + " MOOD: Playful teasing — match their slang and energy; banter, don't lecture."
    if emotion == "happy":
        return base + " MOOD: Upbeat — match their energy."
    return base


def _adaptation_guidance(text, state):
    flags = detect_style_feedback(text)
    if flags:
        state.apply_style_feedback(flags)
    parts = []
    if state.no_questions or flags.get("no_questions"):
        parts.append(
            "ADAPT NOW: They said you ask too much — zero questions this turn. "
            "Reaction + thought/story only."
        )
    if state.be_more_exciting or flags.get("be_more_exciting"):
        parts.append(
            "ADAPT NOW: They find you boring — be more lively: story, fun fact, bold opinion, playful energy."
        )
    if state.fix_listening or flags.get("fix_listening"):
        parts.append(
            "ADAPT NOW: They feel unheard — reflect what they meant, do not defend, stay on thread."
        )
    if state.soften_tone or flags.get("soften_tone"):
        parts.append(
            "ADAPT NOW: They feel hurt — soften tone, brief apology, no jokes or sass."
        )
    if state.take_initiative or flags.get("take_initiative"):
        parts.append(
            "ADAPT NOW: They want YOU to lead — introduce topic/story/idea in THIS reply. "
            "Use a playful opener if mood fits ('Random thought...', 'Fun fact...', 'Okay my turn...')."
        )
    return " ".join(parts) if parts else None


def _turn_guidance(text, emotion, seek, state=None):
    if state:
        adapt = _adaptation_guidance(text, state)
        if adapt:
            return adapt
    cultural = detect_cultural_reference(text)
    if cultural:
        return f"CULTURAL MOMENT: {cultural}"
    if is_greeting_or_how_are_you(text):
        if user_asked_how_eagle_is(text):
            return (
                "They greeted AND asked how YOU are — warm 2-3 sentence reply: "
                "नमस्ते + मैं ठीक हूँ/अच्छी हूँ + ask THEM back once (आप कैसे हैं?). "
                "ONE reciprocal question is natural Hindi etiquette. "
                "No weather talk, no extra questions, no 'है ना?'."
            )
        return (
            "Simple hello only — warm नमस्ते back + one friendly line. "
            "No question needed."
        )
    if _LITERAL_NAME_Q.search(text) and not cultural:
        return (
            "Simple name question — short answer: Eagle. One witty line max. "
            "No poetic speeches about birds or freedom."
        )
    if _ADULT_QUERY.search(text):
        return "Refuse adult topic cleanly; redirect naturally."
    if _INSULT.search(text) or is_playful_teasing(text):
        if is_real_frustration(text):
            return (
                "Real frustration — stay calm, brief apology if needed, fix the thread. "
                "No sass or joke-this-off energy."
            )
        return (
            "Playful teasing or banter — match their vibe with witty comeback. "
            "Not anger, not therapy. 2 short clean sentences."
        )
    if _NOT_LISTENING.search(text) or (
        _FRUSTRATED.search(text) and re.search(r"repeat|same|listen", text, re.I)
    ):
        return (
            "They feel unheard or you're repeating — apologize briefly, reflect what they meant, "
            "continue the thread. Do NOT ask a generic filler question."
        )
    if _ASK_EAGLE.search(text) or _INITIATE.search(text):
        return (
            "YOU lead this turn — share a topic, mini-story, fun fact, or interesting idea. "
            "One optional question at most; do not ask what they want to discuss."
        )
    if _CHEER_UP.search(text):
        return "Acknowledge frustration/sadness first, then warm encouragement or one complete clean joke."
    if _SHARES_INTEREST.search(text):
        return "React to what they love specifically; share a relatable thought or playful observation; question optional."
    if emotion == "support_needed" and seek == "emotional_connection":
        return (
            "They need connection — specific empathy, not therapist phrases. "
            "No 'I understand' / 'I'm here for you'. No questions unless they asked one."
        )
    if seek == "validation":
        return "Affirm what they're feeling or deciding — make them feel heard before anything else."
    if seek == "debate":
        return "Engage the idea directly — reasoned response, not a generic answer."
    return None


_SKIP_NAMES = frozenset({
    "eagle", "ईगल", "sad", "happy", "fine", "good", "well", "here", "there",
})


def _extract_user_name(user_messages):
    for msg in reversed(user_messages):
        m = _NAME.search(msg or "")
        if m:
            name = m.group(1).strip()
            if name.lower() not in _SKIP_NAMES:
                return name
    return None


def build_memory_context(gui):
    messages = getattr(gui, "chat_messages", []) or []
    user_msgs = [m["content"] for m in messages if m.get("role") == "user"]
    asst_msgs = [m["content"] for m in messages if m.get("role") == "assistant"]
    if not user_msgs:
        return ""

    state = get_conversation_state(gui)
    state.sync_from_messages(messages)

    last_user = user_msgs[-1].strip()
    emotion = detect_emotion(last_user)
    seek = detect_user_seek(last_user, emotion)
    state.set_emotion(emotion)
    state.set_intent(seek)
    state.clear_style_flags_if_positive(emotion, seek)

    cultural = detect_cultural_reference(last_user)
    wellbeing = user_asked_how_eagle_is(last_user)
    simple_greet = is_simple_greeting_only(last_user)

    parts = [
        state.to_context_lines(
            is_simple_greeting=simple_greet,
            user_asked_wellbeing=wellbeing,
        ),
        f'User just said: "{last_user[:160]}"',
        f"USER IS SEEKING: {seek} — respond to this INTENTION, not just the literal words.",
        _emotion_style_guidance(emotion, seek),
        _engagement_style_guidance(emotion, seek),
    ]
    if wellbeing:
        parts.append(
            "REPLY SHAPE: नमस्ते + मैं ठीक हूँ + आप कैसे हैं? — warm, complete, ONE question back."
        )
    elif simple_greet:
        parts.append("REPLY SHAPE: Warm greeting back only — no question needed.")
    else:
        parts.append(
            "REPLY SHAPE: Reaction → Thought → statement. Question only if truly needed."
        )
    if simple_greet:
        parts.append("GREETING: Short friendly hello — no interview questions.")
    if cultural:
        parts.append(f"CULTURAL CONTEXT: {cultural}")
    elif is_playful_teasing(last_user):
        parts.append(
            "TONE: Playful teasing or slang banter — match their energy, not formal or defensive."
        )

    guidance = _turn_guidance(last_user, emotion, seek, state)
    if guidance:
        parts.append(f"This turn: {guidance}")

    name = _extract_user_name(user_msgs)
    if name:
        parts.append(f"User name may be {name}.")

    if len(user_msgs) >= 2:
        prev = user_msgs[-2][:90].strip()
        if prev != last_user:
            parts.append(f'Previous user message: "{prev}" — build on this thread.')

    if asst_msgs:
        parts.append(f'Your last reply started with: "{asst_msgs[-1][:70]}..." — do not repeat it.')

    return "SESSION MEMORY: " + " ".join(parts)
