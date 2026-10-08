"""Session conversation state — emotion, topic, interests, intent, question tracking."""

import re

_QUESTION_RE = re.compile(r"[^.!?]*\?", re.UNICODE)
_INTEREST_RE = re.compile(
    r"(?:i love|i like|i enjoy|my hobby|favourite|favorite|passion|i'm into|"
    r"मुझे .+ पसंद|शौक|पसंद है)\s+(.{2,40})",
    re.IGNORECASE,
)
_TOPIC_STOP = frozenset(
    "the a an is are was were i you we they it that this what how why ok okay yes no".split()
)


def _count_questions(text):
    return len(_QUESTION_RE.findall(text or ""))


class ConversationState:
    def __init__(self):
        self.emotion = "neutral"
        self.intent = "casual_chat"
        self.topic = ""
        self.interests = []
        self.questions_asked = []
        self.user_turn_count = 0
        self.assistant_question_streak = 0
        self.last_asst_question_count = 0
        # Style feedback flags — persist until user mood shifts
        self.no_questions = False
        self.be_more_exciting = False
        self.fix_listening = False
        self.soften_tone = False
        self.take_initiative = False

    def sync_from_messages(self, messages):
        user_msgs = [m["content"] for m in messages if m.get("role") == "user"]
        asst_msgs = [m["content"] for m in messages if m.get("role") == "assistant"]
        self.user_turn_count = len(user_msgs)

        interests = []
        for msg in user_msgs:
            for m in _INTEREST_RE.finditer(msg or ""):
                val = m.group(1).strip().rstrip(".!")
                if val and val.lower() not in interests:
                    interests.append(val[:50])
        self.interests = interests[-8:]

        asked = []
        streak = 0
        for msg in asst_msgs:
            n = _count_questions(msg)
            for q in _QUESTION_RE.findall(msg or ""):
                q = q.strip()
                if len(q) > 8:
                    asked.append(q[:100])
            if n > 0:
                streak += 1
            else:
                streak = 0
        self.questions_asked = asked[-12:]
        self.assistant_question_streak = streak
        self.last_asst_question_count = _count_questions(asst_msgs[-1]) if asst_msgs else 0

        if user_msgs:
            self.topic = self._guess_topic(user_msgs[-1], user_msgs)

    def apply_style_feedback(self, flags):
        if flags.get("no_questions"):
            self.no_questions = True
        if flags.get("be_more_exciting"):
            self.be_more_exciting = True
        if flags.get("fix_listening"):
            self.fix_listening = True
        if flags.get("soften_tone"):
            self.soften_tone = True
        if flags.get("take_initiative"):
            self.take_initiative = True

    def clear_style_flags_if_positive(self, emotion, seek):
        if emotion in ("happy", "playful") and seek in ("casual_chat", "humor"):
            self.no_questions = False
            self.be_more_exciting = False
            self.fix_listening = False
            self.soften_tone = False

    def _guess_topic(self, last_user, all_user):
        words = re.findall(r"[a-zA-Z\u0900-\u097F]{4,}", (last_user or "").lower())
        keywords = [w for w in words if w not in _TOPIC_STOP]
        if keywords:
            return " ".join(keywords[:4])
        for msg in reversed(all_user[:-1]):
            words = re.findall(r"[a-zA-Z\u0900-\u097F]{4,}", (msg or "").lower())
            keywords = [w for w in words if w not in _TOPIC_STOP]
            if keywords:
                return " ".join(keywords[:4])
        return self.topic

    def set_emotion(self, emotion):
        if emotion:
            self.emotion = emotion

    def set_intent(self, intent):
        if intent:
            self.intent = intent

    def should_skip_question(self, is_simple_greeting=False, user_asked_wellbeing=False):
        if user_asked_wellbeing:
            return self.no_questions
        if is_simple_greeting:
            return True
        return (
            self.no_questions
            or self.assistant_question_streak >= 1
            or self.last_asst_question_count >= 1
        )

    def to_context_lines(self, is_simple_greeting=False, user_asked_wellbeing=False):
        lines = [
            f"STATE: emotion={self.emotion}, user_seeking={self.intent}, turn={self.user_turn_count}.",
        ]
        if self.topic:
            lines.append(f"Current topic thread: {self.topic}.")
        if self.interests:
            lines.append(f"User interests mentioned: {', '.join(self.interests)}.")
        if self.questions_asked:
            recent = self.questions_asked[-3:]
            lines.append(
                "Questions you ALREADY asked (do NOT repeat): "
                + " | ".join(f'"{q}"' for q in recent)
                + "."
            )
        if user_asked_wellbeing and not self.no_questions:
            lines.append(
                "This turn: They asked how YOU are — say you're fine AND ask them back once "
                "(आप कैसे हैं?). ONE question only. No weather filler."
            )
        elif self.should_skip_question(
            is_simple_greeting=is_simple_greeting,
            user_asked_wellbeing=user_asked_wellbeing,
        ):
            lines.append(
                "This turn: NO question — reaction + thought only. End with a statement."
            )
        if self.take_initiative:
            lines.append(
                "User wants YOU to lead — pick a topic, story, fun fact, or idea NOW. "
                "Do not ask what they want to talk about."
            )
        if self.be_more_exciting:
            lines.append("User finds you boring — more energy, personality, surprise this turn.")
        if self.fix_listening:
            lines.append("User feels unheard — reflect their point, stay on their thread.")
        if self.soften_tone:
            lines.append("User feels hurt — soften tone, brief apology, no jokes.")
        return " ".join(lines)


def get_conversation_state(gui):
    state = getattr(gui, "conversation_state", None)
    if state is None:
        state = ConversationState()
        gui.conversation_state = state
    return state
