import os
import subprocess
import time
import logging
from datetime import datetime
from bson import ObjectId  
from pymongo import MongoClient
import tkinter as tk
import speech_recognition as sr
import threading
import re
import json
import random
import queue
from pathlib import Path
from openai import OpenAI
from dotenv import load_dotenv
from enhanced_tts_openai import text_to_speech_realtime, parallel_speak, configure_tts, set_tts_mode
from language_registry import (
    SUPPORTED_LANGUAGES,
    get_language_config,
    get_stt_config,
    ISO_TO_NAME,
    name_from_stt_code,
    LANGUAGE_KEYWORD_HINTS,
    INDIAN_KEYWORD_HINTS,
    INDIAN_LANGUAGE_NAMES,
    is_indian_language,
)
from modes import get_system_message, get_phrase_file, should_run_follow_up, resolve_engaging_phrase
from model_selection import is_realtime_mode, stream_assistant_reply, get_legacy_model, get_realtime_model
from text_response_utils import (
    extract_complete_sentences,
    prepare_assistant_reply,
    realtime_system_addon,
    split_into_sentences,
    strip_urls_and_markdown,
)

from language_lock import LanguageLockManager, LANG_PRIORITY_WHISPER
from dominant_language import language_from_transcript

LANG_PRIORITY_STT = 100
LANG_PRIORITY_SCRIPT = 90
LANG_PRIORITY_SCRIPT_VETO = 250  # clear native Indian script beats wrong Whisper lock
LANG_PRIORITY_INDIAN_KW = 80
LANG_PRIORITY_INTL_KW = 60
LANG_PRIORITY_LANGDETECT = 40
LANG_PRIORITY_ENGLISH = 20
import functools
import pyaudio
from google.cloud import speech
from google.oauth2 import service_account

# -------------------------
# Timing Decorator for Functions That Need Overall Timing (if needed)
# -------------------------
def time_process(func):
    """
    A decorator to measure and print the execution time of a function.
    
    Args:
        func (function): The function whose runtime is to be measured.
        
    Returns:
        function: The wrapped function with added timing functionality.
    """
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        # Record the start time
        start_time = time.time()
        print(f"*** Starting process: {func.__name__} ***")
        # Execute the original function and capture its result
        result = func(*args, **kwargs)
        # Calculate elapsed time
        elapsed = time.time() - start_time
        print(f"*** Process {func.__name__} took {elapsed:.2f} seconds total. ***")
        return result
    return wrapper

# -------------------------
# Global Setup
# -------------------------

# Get the base directory of the script (where main.py is located)
# This ensures paths work correctly when launched from desktop icon
def get_base_dir():
    """Get the base directory of the application."""
    # Get the directory where this file (eagle_core.py) is located
    current_file_dir = os.path.dirname(os.path.abspath(__file__))
    return current_file_dir

BASE_DIR = get_base_dir()

# Load environment variables from .env file (before DB setup so SCHOOL_NAME is available)
load_dotenv()
openai_api_key = os.getenv("OPENAI_API_KEY")

# MongoDB Setup: cloud Atlas; database "Humanoid_GenAI"; collection = {SCHOOL_NAME}_GenAI (e.g. IISB_GenAI)
mongodb_uri = os.getenv("MONGODB_URI", "mongodb://localhost:27017/")
mongo_client = MongoClient(mongodb_uri)
db = mongo_client["Humanoid_GenAI"]
_school_name = (os.getenv("SCHOOL_NAME") or "DefaultSchool").strip().replace(" ", "_")
collection = db[f"{_school_name}_GenAI"]

# Initialize OpenAI client (renamed to avoid conflict with MongoDB client)
# Initialize OpenAI client with timeout to prevent hanging
openai_client = OpenAI(
    api_key=openai_api_key,
    timeout=120.0  # 2 minutes timeout for API calls (prevents infinite hanging)
)

def _detect_from_script(text):
    """Map Unicode scripts to supported languages — Indian scripts before Devanagari Hindi."""
    script_checks = [
        (r"[\u0D00-\u0D7F]", "Malayalam"),
        (r"[\u0B80-\u0BFF]", "Tamil"),
        (r"[\u0C00-\u0C7F]", "Telugu"),
        (r"[\u0C80-\u0CFF]", "Kannada"),
        (r"[\u0980-\u09FF]", "Bengali"),
        (r"[\u0A80-\u0AFF]", "Gujarati"),
        (r"[\u0A00-\u0A7F]", "Punjabi"),
        (r"[\u0900-\u097F]", "Hindi"),
        (r"[\u10A0-\u10FF]", "Georgian"),
        (r"[\u0530-\u058F]", "Armenian"),
        (r"[\u3040-\u30FF]", "Japanese"),
        (r"[\uAC00-\uD7AF]", "Korean"),
        (r"[\u4E00-\u9FFF]", "Chinese"),
        (r"[\u0E00-\u0E7F]", "Thai"),
        (r"[\u0400-\u04FF]", "Russian"),
        (r"[\u0600-\u06FF]", "Arabic"),
    ]
    # Prefer majority script count so mixed text does not pick the first match
    best_name, best_n = None, 0
    for pattern, name in script_checks:
        n = len(re.findall(pattern, text))
        if n > best_n:
            best_name, best_n = name, n
    if best_n >= 2:
        return best_name
    for pattern, name in script_checks:
        if re.search(pattern, text):
            return name
    return None


def _detect_from_langdetect(text, prefer_indian=False):
    """Detect language using langdetect; in Indian context skip Indonesian/Somali false positives."""
    try:
        from langdetect import DetectorFactory, detect_langs

        DetectorFactory.seed = 0
        stripped = text.strip()
        if len(stripped) < 3:
            return None
        min_prob = 0.55 if len(stripped) < 20 else 0.40
        skip_isos = {"id", "so", "sw", "tl"} if prefer_indian else set()
        for candidate in detect_langs(stripped):
            if candidate.prob < min_prob:
                continue
            iso = candidate.lang.lower()
            if iso in skip_isos:
                continue
            name = ISO_TO_NAME.get(iso) or ISO_TO_NAME.get(iso.split("-")[0])
            if name and name in SUPPORTED_LANGUAGES:
                if prefer_indian and not is_indian_language(name) and name != "English":
                    continue
                return name
    except Exception:
        pass
    return None


def _detect_from_indian_keywords(text):
    """Pick Indian language with the most keyword hits (not first match)."""
    best_lang, best_hits = None, 0
    for lang_name, pattern in INDIAN_KEYWORD_HINTS.items():
        hits = len(re.findall(pattern, text, re.I))
        if hits > best_hits:
            best_hits = hits
            best_lang = lang_name
    return best_lang if best_hits >= 1 else None


def _detect_from_keywords(text):
    """Fast keyword hints for common languages on short romanized text."""
    for lang_name, pattern in LANGUAGE_KEYWORD_HINTS.items():
        if re.search(pattern, text, re.I):
            return lang_name
    return None


def _detect_english_latin(text):
    """Detect English on Latin-script text (langdetect often misclassifies short English)."""
    words = re.findall(r"[a-zA-Z']+", text.lower())
    if len(words) < 2:
        return None
    common_en = {
        "the", "a", "an", "is", "are", "was", "were", "have", "has", "this", "that",
        "what", "how", "you", "your", "we", "they", "i", "my", "our", "in", "on", "at",
        "to", "for", "of", "and", "or", "but", "not", "with", "about", "hello", "hi",
        "thanks", "please", "think", "know", "want", "like", "good", "very", "well",
    }
    hits = sum(1 for w in words if w in common_en)
    if hits >= max(2, len(words) // 3):
        return "English"
    return None


def _detect_language_segment(text, indian_context=False):
    """Detect language from a single text segment."""
    if not text or not text.strip():
        return None
    t = text.strip()
    script_lang = _detect_from_script(t)
    if script_lang:
        return script_lang
    indian_kw = _detect_from_indian_keywords(t)
    if indian_kw:
        return indian_kw
    kw_lang = _detect_from_keywords(t)
    if kw_lang:
        return kw_lang
    en_lang = _detect_english_latin(t)
    if en_lang:
        return en_lang
    ld_lang = _detect_from_langdetect(t, prefer_indian=indian_context)
    if ld_lang:
        return ld_lang
    if re.search(r"[äöåÄÖÅ]", t) or re.search(
        r"\b(hei|kiitos|mitä|mita|onko|suomi|minä|mina|sinä|sina)\b", t, re.I
    ):
        return "Finnish"
    return None


def detect_language_from_text(text, prefer_latest=True, indian_context=False):
    """Detect language from text; prioritize latest words."""
    if not text or not text.strip():
        return None
    if prefer_latest:
        sentences = [s.strip() for s in re.split(r"(?<=[.!?])\s+", text.strip()) if s.strip()]
        for sentence in reversed(sentences[-3:]):
            detected = _detect_language_segment(sentence, indian_context=indian_context)
            if detected:
                return detected
        tail = text.strip()[-220:]
        detected = _detect_language_segment(tail, indian_context=indian_context)
        if detected:
            return detected
    return _detect_language_segment(text, indian_context=indian_context)


def resolve_detected_language(stt_bcp47, text, locked_language=None):
    """
    Industrial fusion: clear native script > Indian keywords > Whisper Indian lock >
    Google STT > langdetect. Soft English Whisper lock must not freeze Multi Language.
    """
    script_name = _detect_from_script(text) if text else None
    # Script veto BEFORE locked language — never answer Kn/Te as Hindi/English
    if script_name and is_indian_language(script_name):
        if locked_language and locked_language != script_name:
            logging.info(
                f"Script veto on fusion: lock={locked_language} -> script={script_name}"
            )
        return script_name, LANG_PRIORITY_SCRIPT_VETO

    stt_name = name_from_stt_code(stt_bcp47) if stt_bcp47 else None
    indian_kw = _detect_from_indian_keywords(text) if text else None
    text_name = detect_language_from_text(text, prefer_latest=True, indian_context=True) if text else None

    # Native script always wins
    if script_name:
        return script_name, LANG_PRIORITY_SCRIPT_VETO

    # Romanized Hindi/Telugu/etc. beats a soft English lock (common Google STT bias)
    if indian_kw:
        return indian_kw, LANG_PRIORITY_INDIAN_KW

    # Hard lock only for confirmed Indian languages from Whisper audio
    if locked_language and is_indian_language(locked_language):
        return locked_language, LANG_PRIORITY_WHISPER
    # English Whisper lock is soft — fall through so Multi can still switch

    # Google heard an Indian language from audio
    if stt_name and is_indian_language(stt_name):
        return stt_name, LANG_PRIORITY_STT

    # STT heard a non-English language
    if stt_name and stt_name != "English":
        return stt_name, LANG_PRIORITY_STT

    # Text heuristics (Indian context — blocks Indonesian false positives)
    if text_name:
        if text_name in INDIAN_LANGUAGE_NAMES:
            return text_name, LANG_PRIORITY_INDIAN_KW
        if text_name != "Indonesian" and text_name != "English":
            return text_name, LANG_PRIORITY_LANGDETECT

    # STT said English — only accept if text also looks English
    if stt_name == "English":
        if text_name == "English" or _detect_english_latin(text or ""):
            return "English", LANG_PRIORITY_ENGLISH

    if text_name == "English":
        return "English", LANG_PRIORITY_ENGLISH

    return text_name or stt_name or locked_language, LANG_PRIORITY_LANGDETECT


def extract_new_user_utterance(gui, full_user_text):
    """Return only the user text added since the previous submit."""
    full_user_text = (full_user_text or "").strip()
    if not full_user_text:
        return ""
    previous = getattr(gui, "last_submitted_user_text", "")
    if previous and full_user_text.startswith(previous):
        new_text = full_user_text[len(previous):].strip()
        if new_text:
            return new_text
    if previous and full_user_text != previous:
        return full_user_text[-220:].strip()
    return full_user_text


def get_ui_language_lock(gui):
    """English/Hindi UI lock; None for Multi Language (auto-detect as today)."""
    mode = (getattr(gui, "language_mode", None) or "English").strip()
    if mode == "English":
        return "English"
    if mode == "Hindi":
        return "Hindi"
    return None


def desired_stt_fingerprint(gui, language_name=None):
    """
    (primary, alts_tuple) Google should use now.
    Used to skip pointless stream restarts when config is unchanged.
    """
    ui_lock = get_ui_language_lock(gui)
    if ui_lock:
        lang = ui_lock
        single = True
    else:
        lock = getattr(gui, "language_lock", None)
        indian_locked = (
            lock
            and lock.is_enabled()
            and lock.is_locked()
            and is_indian_language(lock.get_language() or "")
        )
        single = False
        lang = language_name or (
            lock.get_language()
            if indian_locked
            else (getattr(gui, "detected_language", "English") or "English")
        )
        if not is_indian_language(lang):
            lang = "English"
    primary, alts = get_stt_config(lang, single_language=single)
    return primary, tuple(alts)


def request_stt_restart_if_config_changed(gui, language_name=None, reason=""):
    """Set _stt_restart_requested only when Google primary/alts would change."""
    primary, alts = desired_stt_fingerprint(gui, language_name)
    current = getattr(gui, "_active_stt_fingerprint", None)
    if current == (primary, alts):
        gui._stt_restart_requested = False
        logging.debug(
            f"STT restart skipped ({reason or 'unchanged'}): still {primary} alts={list(alts)}"
        )
        return False
    gui._stt_restart_requested = True
    logging.info(
        f"STT restart needed ({reason or 'config change'}): "
        f"{current} -> primary={primary} alts={list(alts)}"
    )
    return True


def set_detected_language(gui, language_name, source="text", priority=0):
    """Update GUI language; clear Indian-script veto can break a wrong session lock."""
    # UI Language dropdown lock — English/Hindi always win over auto-detect
    ui_lock = get_ui_language_lock(gui)
    if ui_lock:
        language_name = ui_lock
        priority = max(priority, 999)

    if not language_name or language_name not in SUPPORTED_LANGUAGES:
        return False

    wt = getattr(gui, "whisper_transcriber", None)
    if wt and wt.get_dominant_tracker().is_locked():
        locked = wt.get_dominant_tracker().get_dominant()
        if locked and language_name != locked:
            if priority >= LANG_PRIORITY_SCRIPT:
                logging.info(f"Language switch {locked} -> {language_name} (priority={priority})")
                wt.get_dominant_tracker().force_lock(language_name)
            else:
                logging.debug(f"Blocked language switch {language_name} — session locked to {locked}")
                return False

    current_priority = getattr(gui, "_language_priority", 0)
    if language_name == getattr(gui, "detected_language", None) and priority <= current_priority:
        return False
    # Script veto always allowed to downgrade a wrong high-priority lock
    if priority < current_priority and priority < LANG_PRIORITY_SCRIPT_VETO:
        return False
    gui.detected_language = language_name
    gui.discussion_language = language_name
    gui._language_priority = priority
    if hasattr(gui, "update_language_display"):
        try:
            if hasattr(gui, "root") and gui.root.winfo_exists():
                gui.root.after(0, gui.update_language_display)
        except Exception:
            pass
    active = getattr(gui, "_active_stt_codes", None)
    new_stt = get_language_config(language_name)["stt_code"]
    if language_name == "English" and active and "en-IN" in active:
        new_stt = "en-IN"
    if active and new_stt not in active:
        request_stt_restart_if_config_changed(gui, language_name, reason=source)
    logging.info(f"Language set ({source}, priority={priority}): {language_name}")
    return True


def apply_detected_language_from_stt(gui, stt_bcp47, text=""):
    """Apply fused detection; clear Indian script can override Whisper audio lock."""
    lock = getattr(gui, "language_lock", None)
    if stt_bcp47:
        gui._last_stt_language_code = stt_bcp47
    locked = lock.get_language() if lock and lock.is_enabled() and lock.is_locked() else None
    name, priority = resolve_detected_language(stt_bcp47, text, locked_language=locked)
    if name:
        # If script veto beats lock, update language_lock too
        if (
            lock
            and locked
            and name != locked
            and priority >= LANG_PRIORITY_SCRIPT_VETO
        ):
            lock._locked_language = name
            lock._locked_at = time.time()
        return set_detected_language(gui, name, source=f"fusion:{stt_bcp47 or 'none'}", priority=priority)
    return None


def apply_detected_language(gui, text, prefer_latest=True, stt_bcp47=None):
    """Detect language and update GUI using audio+text fusion."""
    stt = stt_bcp47 or getattr(gui, "_last_stt_language_code", "") or ""
    return apply_detected_language_from_stt(gui, stt, text)


def sync_language_from_text(gui, text):
    """Lock response language from what the user actually said (native script)."""
    lang = language_from_transcript(text)
    if not lang:
        lang = detect_language_from_text(text, prefer_latest=True, indian_context=True)
    if not lang:
        return None
    priority = (
        LANG_PRIORITY_SCRIPT_VETO
        if is_indian_language(lang)
        else LANG_PRIORITY_SCRIPT
    )
    set_detected_language(gui, lang, source="transcript", priority=priority)
    wt = getattr(gui, "whisper_transcriber", None)
    if wt:
        wt.get_dominant_tracker().force_lock(lang)
    lock = getattr(gui, "language_lock", None)
    if lock:
        lock._locked_language = lang
        lock._locked_at = time.time()
    return lang


def resolve_response_language(gui, user_text=None):
    """Resolve language for GPT — transcript script beats wrong audio guesses."""
    # UI Language dropdown: English/Hindi always; Multi Language = existing auto path
    ui_lock = get_ui_language_lock(gui)
    if ui_lock:
        gui.detected_language = ui_lock
        gui.discussion_language = ui_lock
        return ui_lock

    if user_text and user_text.strip():
        text_lang = sync_language_from_text(gui, user_text)
        if text_lang:
            return text_lang

    wt = getattr(gui, "whisper_transcriber", None)
    if wt:
        tracker = wt.get_dominant_tracker()
        if tracker.is_locked():
            return tracker.get_dominant()
        dom = tracker.resolve_for_response()
        if dom and dom != "English":
            return dom

    lang = getattr(gui, "detected_language", None)
    if lang and lang != "English":
        return lang
    return lang or "English"


# Initialize with default word limit (Discussion mode)
system_message = get_system_message(75)
conversation_history = [system_message]

# Global flag to control the speech recognition (recording) process
listening = False
# Placeholder for the MongoDB document ObjectId once created
document_id = None

# Soft UX prompts — does not change STT/LLM/TTS core
LISTENING_PROMPT_SEC = 7
LISTENING_PROMPT_TEXT = "I am listening please continue...."
_listen_prompt_token = 0
# Restart dead STT worker while listening=True (Start → Stop only)
STT_WATCHDOG_SEC = float(os.getenv("STT_WATCHDOG_SEC", "2"))
_stt_worker_thread = None
_stt_worker_lock = threading.Lock()

# -------------------------
# Database Functions
# -------------------------

@time_process
def create_default_document():
    """
    Use or create one document per robot per school.
    Collection = {SCHOOL_NAME}_GenAI. Within it: one document per robot_name.
    - Same school + same robot → reuse existing document, append to its conversation.
    - New document only when school is different (different collection) or robot_name is different.
    """
    global document_id
    try:
        robot_name = (os.getenv("ROBOT_NAME") or "Eagle5-6").strip()
        school_name = (os.getenv("SCHOOL_NAME") or "DefaultSchool").strip()

        # Find existing document for this robot + school
        existing = collection.find_one({"robot_name": robot_name, "school": school_name})
        if existing:
            document_id = existing["_id"]
            logging.info(f"Using existing document in {collection.name} for robot={robot_name}, school={school_name}, ID={document_id}")
            return document_id

        # No document for this robot+school → create one
        default_details = {
            "robot_name": robot_name,
            "school": school_name,
            "conversation": []
        }
        result = collection.insert_one(default_details)
        document_id = result.inserted_id
        logging.info(f"Created new document in {collection.name} for robot={robot_name}, school={school_name}, ID={document_id}")
        return document_id
    except Exception as e:
        logging.error(f"Error creating default MongoDB document: {str(e)}")
        raise

@time_process
def execute_data_script():
    """
    Execute the external data.py script and wait until a file named details_completed.txt is created.
    Once the file exists, read the ObjectId from it and then delete the file.
    
    This function uses a blocking loop (with a sleep interval) until the file appears.
    Added timeout to prevent infinite hanging if data.py fails.
    
    NOTE: This function is kept for backward compatibility but is no longer used by default.
    Use create_default_document() instead to skip the details form.
    """
    # Launch data.py as a separate process
    process = subprocess.Popen(["python", "data.py"])
    # Wait until the "details_completed.txt" file is created (with timeout)
    max_wait_time = 60  # Maximum 60 seconds to wait
    wait_start = time.time()
    while not os.path.exists("details_completed.txt"):
        # Check if process is still running
        if process.poll() is not None:
            # Process finished but file doesn't exist - might have failed
            elapsed = time.time() - wait_start
            if elapsed > max_wait_time:
                logging.error(f"data.py script did not create details_completed.txt within {max_wait_time} seconds")
                raise TimeoutError(f"data.py script timeout: file not created within {max_wait_time} seconds")
        # Check timeout
        if time.time() - wait_start > max_wait_time:
            logging.error(f"Timeout waiting for details_completed.txt (waited {max_wait_time} seconds)")
            raise TimeoutError(f"Timeout waiting for details_completed.txt after {max_wait_time} seconds")
        time.sleep(1)
    global document_id
    # Open the file and read the ObjectId, converting it to an ObjectId instance
    with open("details_completed.txt", "r") as f:
        document_id = ObjectId(f.read().strip())
    # Remove the file after reading the ObjectId
    os.remove("details_completed.txt")

@time_process
def update_mongo_document(role, content):
    """
    Append a new conversation entry to a MongoDB document.
    
    Args:
        role (str): The role of the conversation entry (e.g., 'prompt', 'response', or 'followup').
        content (str): The text content of the conversation entry.
    
    Only entries with role 'prompt', 'response', or 'followup' are stored.
    """
    try:
        if role in ["prompt", "response", "followup"]:
            # Construct the conversation entry document with a timestamp.
            conversation_entry = {
                "role": role,
                "content": content,
                "timestamp": datetime.utcnow()
            }
            # Update the MongoDB document by pushing the new entry into the conversation array.
            collection.update_one({"_id": document_id}, {"$push": {"conversation": conversation_entry}})
    except Exception as e:
        # Log any error that occurs during the database update.
        logging.error(f"Error updating MongoDB document: {str(e)}")

# -------------------------
# Logging Setup
# -------------------------

@time_process
def setup_logging():
    """
    Set up logging for the Eagle assistant.
    
    Logs are saved to a file inside a "logs/system_logs" directory, with the filename containing the current date and time.
    """
    # Ensure the logs/system_logs directory exists
    logs_dir = Path.cwd() / "logs" / "system_logs"
    logs_dir.mkdir(parents=True, exist_ok=True)
    # Generate a timestamped filename for the log file
    current_time = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    log_filename = logs_dir / f"{current_time}_eagle.log"
    # Configure the logging settings: file, log level, and format.
    logging.basicConfig(
        filename=log_filename,
        level=logging.INFO,
        format='%(asctime)s - %(levelname)s - %(message)s',
    )
    logging.info("Logging started for Eagle assistant.")

# -------------------------
# Token Counting and Allocation
# -------------------------

def count_tokens(text):
    """
    A simple approximation of token count by counting the number of words in the text.
    
    Args:
        text (str): The input text to count tokens.
    
    Returns:
        int: The approximate token count.
    """
    return len(text.split())

def dynamic_token_allocation(prompt, conversation_history, min_response_tokens=150, max_model_tokens=4096, buffer_tokens=50):
    """
    Calculate the remaining tokens available for OpenAI responses based on the conversation history.
    
    Args:
        prompt (str): The latest user prompt.
        conversation_history (list): The history of conversation messages.
        min_response_tokens (int): Minimum tokens to reserve for a response.
        max_model_tokens (int): Maximum tokens allowed by the model.
        buffer_tokens (int): Buffer tokens reserved to avoid hitting the token limit.
    
    Returns:
        int: The adjusted token limit for the OpenAI response.
    """
    # Sum the token counts from all previous messages in the conversation history.
    history_tokens = sum(count_tokens(message["content"]) for message in conversation_history)
    # Count tokens in the current prompt.
    prompt_tokens = count_tokens(prompt)
    # Calculate remaining tokens by subtracting used tokens and buffer from the maximum limit.
    remaining_tokens = max_model_tokens - (history_tokens + prompt_tokens + buffer_tokens)
    # Ensure that the token limit is at least the minimum required tokens.
    adjusted_token_limit = max(min_response_tokens, remaining_tokens)
    return adjusted_token_limit

# -------------------------
# GUI Update Helper
# -------------------------

def _get_tagged_text(widget, tag):
    """Collect all text with a given tag from a Text widget."""
    parts = []
    ranges = list(widget.tag_ranges(tag))
    for i in range(0, len(ranges), 2):
        if i + 1 < len(ranges):
            parts.append(widget.get(ranges[i], ranges[i + 1]))
    return " ".join(p.strip() for p in parts if p and p.strip()).strip()


def _clear_tag(gui, tag):
    """Remove all text with the given tag from the dialog box."""
    box = gui.dialog_box
    while True:
        ranges = box.tag_ranges(tag)
        if not ranges:
            break
        box.delete(ranges[0], ranges[1])


def commit_chat_message(gui, role, content):
    """Save a user/assistant turn to permanent chat history."""
    text = (content or "").strip()
    if not text:
        return
    if not hasattr(gui, "chat_messages"):
        gui.chat_messages = []
    gui.chat_messages.append({"role": role, "content": text})


def get_draft_text(gui):
    """Current unsent speech (live transcription)."""
    return get_draft_text_from_widget(gui.dialog_box)


def get_draft_text_from_widget(widget):
    return _get_tagged_text(widget, "draft")


def get_pending_utterance(gui):
    """Exact text detected this turn (not committed history)."""
    return (getattr(gui, "pending_utterance", "") or "").strip()


def set_pending_utterance(gui, text):
    gui.pending_utterance = (text or "").strip()


def set_pending_utterance_best(gui, text):
    """Keep best final for this turn — Google discussion accepts cleaned long speech."""
    from whisper_stt import (
        collapse_word_loops,
        is_acceptable_transcript,
        prepare_discussion_utterance,
        transcript_quality_score,
    )
    engine = os.getenv("STT_ENGINE", "whisper").strip().lower()
    raw = (text or "").strip()
    if not raw:
        return
    if engine == "google":
        chunk = prepare_discussion_utterance(collapse_word_loops(raw))
        if not chunk:
            return
        # Prefer newer cleaned finals; allow long discussion turns
        gui.pending_utterance = chunk
        return
    chunk = raw
    if not chunk or not is_acceptable_transcript(chunk):
        return
    prev = get_pending_utterance(gui)
    new_score = transcript_quality_score(chunk)
    if prev:
        if not is_acceptable_transcript(prev):
            gui.pending_utterance = ""
            prev = ""
        elif new_score <= transcript_quality_score(prev):
            return
    gui.pending_utterance = chunk


def clear_pending_utterance(gui):
    gui.pending_utterance = ""


def set_activity_status(gui, status):
    """Update top-bar activity indicator on the main thread."""
    try:
        if hasattr(gui, "set_activity_status") and hasattr(gui, "root") and gui.root.winfo_exists():
            gui.root.after(0, lambda s=status: gui.set_activity_status(s))
    except Exception:
        pass


def stop_listening_prompts(gui=None):
    """Cancel periodic 'I am listening…' lines."""
    global _listen_prompt_token
    _listen_prompt_token += 1


def _eagle_is_busy(gui):
    busy = bool(getattr(gui, "_eagle_response_busy", False))
    if busy:
        return True
    try:
        from enhanced_tts_openai import is_tts_playing
        return bool(is_tts_playing())
    except Exception:
        return False


def _stt_worker_entry(gui):
    """Run STT once; if still listening when it exits, schedule an immediate restart."""
    try:
        live_transcribe(gui)
    except Exception as e:
        logging.error(f"STT worker crashed: {e}")
        import traceback
        logging.error(traceback.format_exc())
    finally:
        if not listening:
            return
        logging.warning("STT worker exited while listening=True — restarting soon")
        def _restart():
            if listening:
                ensure_continuous_listening(gui)
        try:
            if hasattr(gui, "root") and gui.root.winfo_exists():
                gui.root.after(250, _restart)
            else:
                _restart()
        except Exception:
            try:
                _restart()
            except Exception:
                pass


def _spawn_stt_worker(gui):
    """Start STT worker if none is alive. Returns True if a new worker was started."""
    global _stt_worker_thread
    with _stt_worker_lock:
        if _stt_worker_thread is not None and _stt_worker_thread.is_alive():
            return False
        t = threading.Thread(
            target=_stt_worker_entry, args=(gui,), daemon=True, name="stt-worker"
        )
        _stt_worker_thread = t
        t.start()
        logging.info("STT worker started (continuous listen)")
        return True


def start_listening_prompts(gui, first_delay_sec=None):
    """
    Watchdog while continuous listen is on: keep STT alive every STT_WATCHDOG_SEC.
    Activity bar shows Listening… only when Eagle is not busy.
    """
    global _listen_prompt_token
    stop_listening_prompts()
    my_token = _listen_prompt_token
    interval_ms = int(max(0.5, STT_WATCHDOG_SEC) * 1000)
    if first_delay_sec is None:
        first_ms = interval_ms
    else:
        first_ms = int(max(0.0, float(first_delay_sec)) * 1000)

    def _tick():
        if my_token != _listen_prompt_token:
            return
        if not listening:
            return
        # Always keep mic/STT alive — even while Thinking/Speaking
        ensure_continuous_listening(gui)
        if not _eagle_is_busy(gui):
            set_activity_status(gui, "Listening…")
        try:
            if hasattr(gui, "root") and gui.root.winfo_exists() and listening:
                gui.root.after(interval_ms, _tick)
        except Exception:
            pass

    try:
        if hasattr(gui, "root") and gui.root.winfo_exists():
            gui.root.after(first_ms, _tick)
    except Exception:
        pass


def ensure_continuous_listening(gui):
    """
    Keep mic/STT running from Start until Stop.
    If listening=True but worker died, restart it (multi-lang Google config unchanged).
    """
    if not listening:
        return
    if not _eagle_is_busy(gui):
        set_activity_status(gui, "Listening…")
    if _spawn_stt_worker(gui):
        logging.info("Continuous listening (re)started — mic stays open until Stop")


def resume_after_eagle_speech(gui):
    """Response speech finished → keep listening (mic never stopped)."""
    try:
        from enhanced_tts_openai import clear_tts_listen_gate
        clear_tts_listen_gate()
    except Exception:
        pass
    try:
        gui._eagle_response_busy = False
    except Exception:
        pass
    begin_user_listen_turn(gui)
    ensure_continuous_listening(gui)
    set_activity_status(gui, "Listening…")
    # Keep watchdog running (do not cancel) — restart tick if needed
    start_listening_prompts(gui, first_delay_sec=0)


def refresh_chat_display(gui):
    """Rebuild dialog from committed history; show pending utterance below."""
    if not hasattr(gui, "chat_messages"):
        gui.chat_messages = []
    pending = get_pending_utterance(gui)
    box = gui.dialog_box
    box.delete("1.0", tk.END)
    for i, msg in enumerate(gui.chat_messages):
        if i > 0:
            box.insert(tk.END, "\n")
        tag = "hist_user" if msg["role"] == "user" else "hist_assistant"
        box.insert(tk.END, msg["content"] + "\n", tag)
    if pending:
        if gui.chat_messages:
            box.insert(tk.END, "\n")
        box.insert(tk.END, pending + " ", "draft")
    box.see(tk.END)


def google_complete_words_only(raw_transcript):
    """
    Live Google STT often revises the unfinished word letter-by-letter
    (e.g. "n" → "number" while saying "Nuremberg"). Show only finished words:
    keep text ending in whitespace as-is; otherwise drop the trailing partial word.
    """
    raw = raw_transcript or ""
    if not raw.strip():
        return ""
    # Trailing space = Google finished the last word
    if raw.endswith((" ", "\n", "\t")):
        return raw.strip()
    parts = raw.strip().split()
    if len(parts) <= 1:
        return ""  # still dictating first/only word — wait for final or word boundary
    return " ".join(parts[:-1])


def set_live_transcript_display(gui, text, is_final=False):
    """
    Stream speech to screen as draft (no ellipsis). Replace in place as Google updates.
    Ignored while Eagle is thinking/speaking so conversation turns stay clean.
    """
    if getattr(gui, "_eagle_response_busy", False):
        return
    text = (text or "").strip()
    if not text:
        return
    if is_final:
        set_pending_utterance_best(gui, text)
    else:
        set_pending_utterance(gui, text)
    try:
        if hasattr(gui, "root") and gui.root.winfo_exists():
            gui.root.after(0, lambda: refresh_chat_display(gui))
        else:
            refresh_chat_display(gui)
    except Exception:
        try:
            refresh_chat_display(gui)
        except Exception:
            pass


def begin_user_listen_turn(gui):
    """Fresh capture turn after Eagle finishes — clears prior draft so submit is this turn only."""
    clear_pending_utterance(gui)
    gui._stt_final_accum = ""
    try:
        if hasattr(gui, "root") and gui.root.winfo_exists():
            gui.root.after(0, lambda: refresh_chat_display(gui))
    except Exception:
        pass


def update_dialog_box(text, tag, gui):
    """
    Update the dialog box in the GUI with new text.
    
    Args:
        text (str): The text to be inserted in the dialog box.
        tag (str): The tag that determines the text color/style (e.g., 'status', 'user', 'assistant', 'follow_up').
        gui (object): The GUI instance that contains the dialog box widget.
    
    This function inserts text, scrolls the dialog to the end, and applies color based on the tag.
    """
    gui.dialog_box.insert(tk.END, text, tag)
    gui.dialog_box.see(tk.END)
    # Configure text color based on the tag.
    if tag == "status":
        gui.dialog_box.tag_config("status", foreground="blue")
    elif tag == "user":
        gui.dialog_box.tag_config("user", foreground="orange")
    elif tag == "assistant":
        gui.dialog_box.tag_config("assistant", foreground="green")
    elif tag == "follow_up":
        gui.dialog_box.tag_config("follow_up", foreground="red")
    elif tag == "draft":
        gui.dialog_box.tag_config("draft", foreground="#CC7700")

# -------------------------
# Live Transcription (STT)
# -------------------------

def live_transcribe(gui):
    """
    Live microphone transcription.
    Default: OpenAI Whisper (GPT-assistant style) — auto language + native script.
    Fallback: STT_ENGINE=google for Google Cloud streaming.
    """
    stt_start_time = time.time()
    global listening

    engine = os.getenv("STT_ENGINE", "whisper").strip().lower()
    if engine != "google":
        if not openai_api_key:
            update_dialog_box("\n[Error] OPENAI_API_KEY required for Whisper STT.\n", "status", gui)
            return
        try:
            from whisper_stt import run_whisper_live_session

            if not getattr(gui, "detected_language", None):
                gui.detected_language = "English"
                gui._language_priority = 0
            gui._last_stt_language_code = ""
            run_whisper_live_session(
                gui,
                openai_client,
                lambda: listening,
                update_dialog_box,
                set_detected_language,
            )
        except Exception as e:
            logging.error(f"Whisper STT failed: {e}")
            import traceback
            logging.error(traceback.format_exc())
            update_dialog_box(f"\n[Whisper STT error] {e}\n", "status", gui)
        stt_total = time.time() - stt_start_time
        print(f"*** Total STT (Whisper) time: {stt_total:.2f} seconds. ***")
        return

    _live_transcribe_google(gui, stt_start_time)


def _live_transcribe_google(gui, stt_start_time=None):
    """
    Google Cloud Speech-to-Text streaming (legacy fallback).
    """
    if stt_start_time is None:
        stt_start_time = time.time()
    global listening
    RATE = 16000  # Sample rate in Hz
    CHUNK = 1024  # Buffer size
    CHANNELS = 1  # Mono audio
    FORMAT = pyaudio.paInt16  # 16-bit audio
    
    try:
        # Load credentials from key.json
        # Use BASE_DIR instead of os.getcwd() to work correctly when launched from desktop icon
        credentials_path = os.path.join(BASE_DIR, "utility", "key.json")
        if not os.path.exists(credentials_path):
            update_dialog_box("\n[Error] key.json file not found. Please ensure credentials file exists.\n", "status", gui)
            logging.error(f"key.json file not found at: {credentials_path}")
            return
        
        credentials = service_account.Credentials.from_service_account_file(credentials_path)
        client = speech.SpeechClient(credentials=credentials)
        
        def on_whisper_locked(lang_name):
            set_detected_language(gui, lang_name, source="whisper-lock", priority=LANG_PRIORITY_WHISPER)
            # Only tear Google stream if primary/alts would actually change
            # (UI English lock used to force renew every Whisper lock → ~60s dead mic)
            if request_stt_restart_if_config_changed(
                gui, lang_name, reason="whisper-lock"
            ):
                if lang_name and is_indian_language(lang_name):
                    logging.info(f"Whisper locked Indian → STT primary will switch to: {lang_name}")
            else:
                logging.info(
                    f"Whisper locked {lang_name} — Google STT config unchanged, no renew"
                )

        gui.language_lock = LanguageLockManager(openai_client, on_locked=on_whisper_locked)
        gui.language_lock.reset()
        # Fresh Multi session must not inherit a stuck English priority lock
        if not get_ui_language_lock(gui):
            gui._language_priority = 0
        else:
            gui._language_priority = 0
        gui._last_stt_language_code = ""

        lock = gui.language_lock
        # Language dropdown lock (English/Hindi) → single. Multi Language → always multi pool
        ui_lock = get_ui_language_lock(gui)
        if ui_lock:
            detected_lang = ui_lock
            single = True
            set_detected_language(gui, ui_lock, source="ui-language-lock", priority=999)
        else:
            # Multi: keep alternatives always — Whisper English must not single-lock
            indian_locked = (
                lock.is_enabled()
                and lock.is_locked()
                and is_indian_language(lock.get_language() or "")
            )
            single = False
            detected_lang = (
                lock.get_language()
                if indian_locked
                else (getattr(gui, "detected_language", "English") or "English")
            )
            if detected_lang == "English" or not is_indian_language(detected_lang):
                # Discovery: start with en primary but hi+te+ta alts (see get_stt_config)
                detected_lang = "English"
        primary_stt, alt_stt = get_stt_config(detected_lang, single_language=single)
        gui._active_stt_codes = {primary_stt, *alt_stt} if alt_stt else {primary_stt}
        gui._active_stt_fingerprint = (primary_stt, tuple(alt_stt))
        gui._stt_restart_requested = False
        mode = "single-language (locked)" if single else "multi-language discovery"
        logging.info(f"STT [{mode}]: primary={primary_stt}, alternatives={alt_stt}")
        config = speech.RecognitionConfig(
            encoding=speech.RecognitionConfig.AudioEncoding.LINEAR16,
            sample_rate_hertz=RATE,
            language_code=primary_stt,
            alternative_language_codes=alt_stt,
            enable_automatic_punctuation=True,
            enable_word_time_offsets=True,
            model="latest_long",
        )
        
        # Configure streaming recognition
        streaming_config = speech.StreamingRecognitionConfig(
            config=config,
            interim_results=True,  # Get interim results for real-time display
            single_utterance=False,  # Continue listening
        )
        
        # Initialize PyAudio
        audio_interface = pyaudio.PyAudio()
        audio_stream = audio_interface.open(
            format=FORMAT,
            channels=CHANNELS,
            rate=RATE,
            input=True,
            frames_per_buffer=CHUNK
        )
        
        set_activity_status(gui, "Listening…")
        logging.info("Started listening for speech input using Google Cloud Speech-to-Text streaming.")
        
        silence_start = None
        last_transcript = ""  # Track last complete transcript to avoid duplicates
        last_displayed_text = ""  # Track what was actually displayed to GUI (to prevent skipping first sentence)
        last_speech_at = time.time()  # last Google STT result — used to avoid mid-speech renew
        last_stt_event_at = time.time()  # any STT response — idle watchdog
        if not hasattr(gui, "_stt_final_accum"):
            gui._stt_final_accum = ""
        
        # Shared audio buffer queue to prevent concurrent PyAudio access (fixes core dump)
        audio_buffer_queue = queue.Queue(maxsize=100)
        audio_generator_active = threading.Event()
        audio_generator_active.set()
        # Only one Google STT stream may consume mic audio at a time
        active_stream_id = [0]
        stream_needs_renewal = threading.Event()
        
        def audio_reader_thread():
            """Thread that reads from PyAudio and feeds the buffer queue."""
            chunk_count = 0
            logging.info("Starting audio reader thread...")
            try:
                while listening and audio_generator_active.is_set():
                    try:
                        if not audio_stream.is_active():
                            logging.warning("Audio stream is not active, stopping reader thread")
                            break
                        data = audio_stream.read(CHUNK, exception_on_overflow=False)
                        chunk_count += 1
                        lid = getattr(gui, "language_lock", None)
                        if lid and lid.is_enabled():
                            lid.feed_pcm(data)
                        if chunk_count % 100 == 0:  # Log every 100 chunks
                            logging.debug(f"Read {chunk_count} audio chunks from PyAudio")
                        # Put in queue (non-blocking, drop if full to prevent memory issues)
                        try:
                            audio_buffer_queue.put_nowait(data)
                        except queue.Full:
                            # Queue full, skip this chunk to prevent memory buildup
                            logging.debug("Audio buffer queue full, dropping chunk")
                    except Exception as e:
                        logging.error(f"Error reading audio stream: {e}")
                        import traceback
                        logging.error(traceback.format_exc())
                        break
            except Exception as e:
                logging.error(f"Fatal error in audio reader thread: {e}")
                import traceback
                logging.error(traceback.format_exc())
            finally:
                logging.info(f"Audio reader thread finished. Total chunks read: {chunk_count}")
        
        def generate_audio_stream(stream_id):
            """Yield mic chunks only while this stream_id is the active Google STT stream."""
            chunk_count = 0
            logging.info(f"Starting audio stream generator (id={stream_id})...")
            while listening and audio_generator_active.is_set() and active_stream_id[0] == stream_id:
                try:
                    try:
                        data = audio_buffer_queue.get(timeout=0.1)
                        chunk_count += 1
                        if chunk_count % 100 == 0:
                            logging.debug(f"Sent {chunk_count} audio chunks to Google Cloud")
                        yield speech.StreamingRecognizeRequest(audio_content=data)
                    except queue.Empty:
                        continue
                except Exception as e:
                    logging.error(f"Error in audio stream generator: {e}")
                    import traceback
                    logging.error(traceback.format_exc())
                    break
            logging.info(f"Audio stream generator finished (id={stream_id}). Total chunks sent: {chunk_count}")        
        # Start the audio reader thread (single thread reads from PyAudio)
        audio_reader = threading.Thread(target=audio_reader_thread, daemon=True)
        audio_reader.start()
        logging.info("Audio reader thread started")
        
        # Start streaming recognition (stream id 1)
        active_stream_id[0] = 1
        requests_generator = generate_audio_stream(active_stream_id[0])
        
        # Call streaming_recognize with both config and requests as keyword arguments
        # Method signature: streaming_recognize(config, requests, ...)
        logging.info("Calling streaming_recognize...")
        try:
            # The correct way: pass both config and requests as keyword arguments
            responses = client.streaming_recognize(
                config=streaming_config,
                requests=requests_generator
            )
            logging.info("streaming_recognize called successfully - waiting for responses...")
            # FIX: Check if GUI is still valid before accessing
            try:
                if hasattr(gui, 'root') and gui.root.winfo_exists():
                    gui.root.after(0, lambda: update_dialog_box("Connected to server\n", "status", gui))
            except Exception:
                pass  # GUI no longer available, skip update
        except Exception as e:
            error_msg = str(e)
            import traceback
            full_error = f"{error_msg}\n{traceback.format_exc()}"
            logging.error(f"Failed to call streaming_recognize: {full_error}")
            # FIX: Check if GUI is still valid before accessing
            try:
                if hasattr(gui, 'root') and gui.root.winfo_exists():
                    gui.root.after(0, lambda msg=error_msg: update_dialog_box(f"\n[Error starting speech recognition] {msg}\n", "status", gui))
            except Exception:
                pass  # GUI no longer available, skip update
            try:
                audio_stream.stop_stream()
                audio_stream.close()
                audio_interface.terminate()
            except:
                pass
            return
        
        # Wrapper function to process responses from a specific stream
        # Defined early so it can be used in threads
        def process_responses_for_stream(stream_responses, stream_id):
            """Process Google STT stream — keep listening alive; renew if Google closes stream."""
            nonlocal last_transcript, silence_start, last_displayed_text, last_speech_at, last_stt_event_at

            logging.info(f"Starting to process streaming responses (id={stream_id})...")
            first_response_time = None
            try:
                response_count = 0
                for response in stream_responses:
                    if active_stream_id[0] != stream_id:
                        logging.info(f"STT stream {stream_id} superseded, stopping processor")
                        break
                    last_stt_event_at = time.time()
                    if first_response_time is None:
                        first_response_time = time.time()
                        elapsed = first_response_time - stt_start_time
                        logging.info(f"First response received after {elapsed:.2f} seconds")
                    response_count += 1
                    if not listening:
                        logging.info("Listening stopped, exiting response processing.")
                        break

                    if not response.results:
                        continue

                    # Mic stays open; ignore transcripts while Eagle thinks/speaks (avoid TTS echo)
                    if _eagle_is_busy(gui):
                        continue

                    for result in response.results:
                        if not result.alternatives:
                            continue

                        transcript = result.alternatives[0].transcript or ""
                        stability = getattr(result, "stability", 1.0)
                        is_final = getattr(result, "is_final", False)
                        stt_lang = getattr(result, "language_code", "") or ""
                        current_transcript = transcript.strip()

                        if current_transcript:
                            last_speech_at = time.time()
                            apply_detected_language(gui, current_transcript, stt_bcp47=stt_lang)
                            lid = getattr(gui, "language_lock", None)
                            if lid and lid.is_enabled():
                                lid.maybe_identify()
                        elif stt_lang:
                            apply_detected_language_from_stt(gui, stt_lang, "")
                            continue
                        else:
                            continue

                        # Live display: finals full; interim = complete words only (no letter stream)
                        from whisper_stt import collapse_word_loops
                        google_final_accum = getattr(gui, "_stt_final_accum", "") or ""
                        if is_final:
                            cleaned_final = collapse_word_loops(current_transcript)
                            if not cleaned_final:
                                continue
                            if google_final_accum:
                                if cleaned_final.startswith(google_final_accum):
                                    google_final_accum = cleaned_final
                                elif google_final_accum.endswith(cleaned_final):
                                    pass
                                else:
                                    google_final_accum = (google_final_accum + " " + cleaned_final).strip()
                                    google_final_accum = collapse_word_loops(google_final_accum)
                            else:
                                google_final_accum = cleaned_final
                            gui._stt_final_accum = google_final_accum
                            display_text = google_final_accum
                            set_live_transcript_display(gui, display_text, is_final=True)
                        else:
                            # Skip unstable alphabet-level guesses
                            if stability < 0.5:
                                continue
                            finished = google_complete_words_only(transcript)
                            if google_final_accum:
                                prefix = google_final_accum[: max(8, len(google_final_accum) // 3)]
                                if finished.startswith(prefix):
                                    display_text = finished
                                elif finished:
                                    display_text = (google_final_accum + " " + finished).strip()
                                else:
                                    display_text = google_final_accum  # hold prior words; hide partial
                            else:
                                display_text = finished
                            if not display_text:
                                last_transcript = current_transcript
                                silence_start = None
                                continue
                            if display_text == last_displayed_text:
                                last_transcript = current_transcript
                                silence_start = None
                                continue
                            set_live_transcript_display(gui, display_text, is_final=False)

                        last_transcript = current_transcript
                        last_displayed_text = display_text
                        silence_start = None
                        if response_count <= 8 or is_final:
                            logging.info(
                                f"Google live ({'final' if is_final else 'interim'}, "
                                f"stab={stability:.2f}): '{display_text[:100]}'"
                            )

                logging.info(f"Finished processing responses (id={stream_id}). Total: {response_count}")
            except Exception as e:
                error_msg = str(e)
                if "Exceeded maximum allowed stream duration" not in error_msg:
                    import traceback
                    logging.error(f"Error processing streaming responses: {error_msg}\n{traceback.format_exc()}")
                else:
                    logging.info("Stream expired (expected during renewal)")
            finally:
                # Critical: if Google closed the stream but user is still speaking / listening,
                # renew immediately — do not wait ~280s (that felt like "listening stopped").
                if listening and active_stream_id[0] == stream_id:
                    logging.warning(f"STT stream {stream_id} ended while still listening — requesting renew")
                    stream_needs_renewal.set()

        # Process responses in a separate thread (using wrapper for stream renewal compatibility)
        response_thread = threading.Thread(
            target=lambda: process_responses_for_stream(responses, active_stream_id[0]),
            daemon=True
        )
        response_thread.start()
        logging.info("Response processing thread started")
        
        # Add a timeout check to see if we're receiving responses
        def check_responses():
            time.sleep(5)  # Wait 5 seconds
            if listening:
                # Check if we've received any responses by checking the log or response count
                logging.info("System check: 5 seconds elapsed. If no transcripts appear, check microphone and network.")
                # FIX: Check if GUI is still valid before accessing
                try:
                    if hasattr(gui, 'root') and gui.root.winfo_exists():
                        gui.root.after(0, lambda: update_dialog_box("\n[System] Speak clearly into the microphone. Text will appear here when detected.\n", "status", gui))
                except Exception:
                    pass  # GUI no longer available, skip update
        
        check_thread = threading.Thread(target=check_responses, daemon=True)
        check_thread.start()
        
        # Stream renewal: only on stream death or proactive ~3.5 min (never idle-kick mid talk)
        STREAM_RENEWAL_TIME = 220
        SPEECH_RENEW_GUARD_SEC = 2.0  # never tear stream while user is mid-utterance
        stream_start_time = time.time()
        current_responses = responses  # Keep reference to current stream
        stream_lock = threading.Lock()  # Lock for thread-safe stream updates
        
        def _rebuild_stt_config():
            """Rebuild Google STT config; Multi Language always keeps hi/te/ta/en pool."""
            nonlocal config, streaming_config
            primary_stt, alt_stt = desired_stt_fingerprint(gui)
            alt_list = list(alt_stt)
            single = len(alt_list) == 0
            gui._active_stt_codes = {primary_stt, *alt_list} if alt_list else {primary_stt}
            gui._active_stt_fingerprint = (primary_stt, alt_stt)
            gui._stt_restart_requested = False
            config = speech.RecognitionConfig(
                encoding=speech.RecognitionConfig.AudioEncoding.LINEAR16,
                sample_rate_hertz=RATE,
                language_code=primary_stt,
                alternative_language_codes=alt_list,
                enable_automatic_punctuation=True,
                enable_word_time_offsets=True,
                model="latest_long",
            )
            streaming_config = speech.StreamingRecognitionConfig(
                config=config,
                interim_results=True,
                single_utterance=False,
            )
            mode = "LOCKED single-lang" if single else "multi-lang"
            logging.info(f"STT reconfigured [{mode}]: primary={primary_stt}, alternatives={alt_list}")
        
        def renew_stream():
            """Renew on stream death, real config change, or duration limit.
            Idle renew only while user should be listening (never count TTS/busy time).
            Open-stream call has a hard timeout so mic cannot hang for minutes.
            """
            nonlocal current_responses, response_thread, stream_start_time, client, streaming_config, last_stt_event_at
            STT_IDLE_RENEW_SEC = float(os.getenv("STT_IDLE_RENEW_SEC", "90"))
            STT_RENEW_OPEN_TIMEOUT_SEC = float(os.getenv("STT_RENEW_OPEN_TIMEOUT_SEC", "8"))
            
            while listening:
                # While Eagle thinks/speaks, freeze idle clock — do not zombie-renew after TTS
                if _eagle_is_busy(gui):
                    last_stt_event_at = time.time()
                    time.sleep(0.4)
                    continue

                elapsed = time.time() - stream_start_time
                time_until_renewal = STREAM_RENEWAL_TIME - elapsed
                lang_restart = bool(getattr(gui, "_stt_restart_requested", False))
                force_renew = stream_needs_renewal.is_set()
                need_time_renew = time_until_renewal <= 0
                resp_dead = response_thread is not None and not response_thread.is_alive()
                idle_sec = time.time() - last_stt_event_at
                # True zombie only: listening, not busy, no Google events for a long time
                need_idle_renew = idle_sec >= STT_IDLE_RENEW_SEC and listening

                # Skip language renew if fingerprint already matches (Whisper false alarms)
                if lang_restart and not force_renew:
                    if not request_stt_restart_if_config_changed(gui, reason="renew-check"):
                        lang_restart = False

                if resp_dead and listening:
                    force_renew = True
                    stream_needs_renewal.set()

                # Defer planned renew while user is mid-utterance (not for dead stream)
                if (lang_restart or need_time_renew or need_idle_renew) and not force_renew:
                    if (time.time() - last_speech_at) < SPEECH_RENEW_GUARD_SEC:
                        time.sleep(0.2)
                        continue

                if not force_renew and not lang_restart and not need_time_renew and not need_idle_renew:
                    time.sleep(0.5)
                    continue

                if not listening:
                    break

                if force_renew:
                    reason = "response thread dead" if resp_dead else "stream ended"
                elif need_idle_renew:
                    reason = f"idle {idle_sec:.0f}s (listen zombie)"
                elif lang_restart:
                    reason = "language change"
                else:
                    reason = f"duration {elapsed:.0f}s"
                logging.info(f"Renewing STT stream ({reason}) — silent, keep draft")
                renew_ok = False
                new_id = None
                try:
                    # 1) Stop old stream quickly (short critical section)
                    with stream_lock:
                        stream_needs_renewal.clear()
                        do_rebuild = lang_restart
                        if do_rebuild:
                            _rebuild_stt_config()
                        else:
                            gui._stt_restart_requested = False
                        active_stream_id[0] += 1
                        new_id = active_stream_id[0]
                        old_response_thread = response_thread

                    # 2) Let old processor finish without holding lock
                    if old_response_thread is not None and old_response_thread.is_alive():
                        old_response_thread.join(timeout=1.5)

                    # Drop stale mic backlog so new stream starts clean
                    drained = 0
                    try:
                        while drained < 50:
                            audio_buffer_queue.get_nowait()
                            drained += 1
                    except queue.Empty:
                        pass

                    # 3) Open new Google stream with hard timeout (prevents multi-minute hangs)
                    new_requests_generator = generate_audio_stream(new_id)
                    open_box = {"responses": None, "err": None}

                    def _open_google_stream():
                        try:
                            open_box["responses"] = client.streaming_recognize(
                                config=streaming_config,
                                requests=new_requests_generator,
                            )
                        except Exception as open_err:
                            open_box["err"] = open_err

                    open_thread = threading.Thread(
                        target=_open_google_stream, daemon=True, name=f"stt-open-{new_id}"
                    )
                    open_thread.start()
                    open_thread.join(timeout=STT_RENEW_OPEN_TIMEOUT_SEC)

                    if open_thread.is_alive() or open_box["responses"] is None:
                        logging.error(
                            f"STT renew open timed out/failed after "
                            f"{STT_RENEW_OPEN_TIMEOUT_SEC:.0f}s (id={new_id}) — retry soon"
                        )
                        # Kill this generator so a hung open thread stops consuming mic
                        with stream_lock:
                            if active_stream_id[0] == new_id:
                                active_stream_id[0] += 1
                        stream_needs_renewal.set()
                        last_stt_event_at = time.time()
                        time.sleep(1.0)
                        continue

                    if open_box["err"] is not None:
                        raise open_box["err"]

                    new_responses = open_box["responses"]
                    with stream_lock:
                        if active_stream_id[0] != new_id:
                            logging.warning(f"STT renew id={new_id} superseded before start")
                        else:
                            current_responses = new_responses
                            stream_start_time = time.time()
                            last_stt_event_at = time.time()
                            new_response_thread = threading.Thread(
                                target=lambda r=new_responses, sid=new_id: process_responses_for_stream(r, sid),
                                daemon=True,
                            )
                            new_response_thread.start()
                            response_thread = new_response_thread
                            renew_ok = True
                    if renew_ok:
                        logging.info(f"STT stream renewed successfully (id={new_id}).")
                    else:
                        stream_needs_renewal.set()
                except Exception as e:
                    logging.error(f"Error renewing stream: {e}")
                    import traceback
                    logging.error(traceback.format_exc())
                    stream_needs_renewal.set()
                    last_stt_event_at = time.time()
                    time.sleep(0.5)
                    continue
                time.sleep(0.3)
        
        # Start stream renewal thread
        renewal_thread = threading.Thread(target=renew_stream, daemon=True)
        renewal_thread.start()
        logging.info("Stream renewal thread started (death / duration / listen-idle only)")
        
        # Keep the main loop running while listenings
        while listening:
            time.sleep(0.1)
        
        # Signal audio generator to stop
        audio_generator_active.clear()
        
        # Wait for response thread to finish (short timeout since it should be quick)
        response_thread.join(timeout=5)  # Increased to 5 seconds for safety
        if response_thread.is_alive():
            logging.warning("Response thread did not finish within timeout, continuing anyway")
        
        # Wait for audio reader thread to finish
        audio_reader.join(timeout=2)
        if audio_reader.is_alive():
            logging.warning("Audio reader thread did not finish within timeout, continuing anyway")
        
        # Cleanup - FIX: Add proper error handling to prevent crashes
        try:
            if audio_stream.is_active():
                audio_stream.stop_stream()
        except Exception as e:
            logging.warning(f"Error stopping audio stream: {e}")
        try:
            audio_stream.close()
        except Exception as e:
            logging.warning(f"Error closing audio stream: {e}")
        try:
            audio_interface.terminate()
        except Exception as e:
            logging.warning(f"Error terminating audio interface: {e}")
        
    except FileNotFoundError:
        update_dialog_box("\n[Error] key.json file not found. Please ensure credentials file exists.\n", "status", gui)
        logging.error("key.json file not found.")
    except Exception as e:
        update_dialog_box(f"\n[Error with speech recognition service] {str(e)}\n", "status", gui)
        logging.error(f"Speech recognition service error: {str(e)}")
        try:
            audio_stream.stop_stream()
            audio_stream.close()
            audio_interface.terminate()
        except:
            pass
    
    stt_end_time = time.time()  # End time for transcription session
    stt_total = stt_end_time - stt_start_time
    print("*** Total STT (live transcription) time: {:.2f} seconds. ***".format(stt_total))

def remove_bullet_asterisks(text):
    """
    Remove asterisks from bullet points at the beginning of lines in the provided text.
    
    Args:
        text (str): The input text that may contain bullet point asterisks.
    
    Returns:
        str: The modified text without asterisks at the start of lines.
    """
    return re.sub(r"(?m)^\*\s+", "", text)

# Per-file, per-language cache for engaging phrases
phrase_cache = {}

def load_engaging_phrases(json_filename="phrase.json", language_name="English"):
    """
    Load engaging phrases for the selected language.
    File format: {"en": [...], "hi": [...]} or legacy list (English only).
    """
    global phrase_cache
    lang = get_language_config(language_name)
    lang_code = lang["code"]
    cache_key = (json_filename, lang_code)
    if cache_key in phrase_cache:
        return phrase_cache[cache_key]

    if os.path.dirname(json_filename) == "":
        json_file_path = os.path.join(BASE_DIR, "utility", json_filename)
    else:
        json_file_path = os.path.join(BASE_DIR, json_filename)

    try:
        with open(json_file_path, "r", encoding="utf-8") as f:
            phrases_data = json.load(f)
        if isinstance(phrases_data, dict):
            phrases = phrases_data.get(lang_code) or phrases_data.get("en", [])
        elif isinstance(phrases_data, list):
            phrases = phrases_data if lang_code == "en" else []
        else:
            phrases = []
        if isinstance(phrases, list) and phrases:
            phrase_cache[cache_key] = phrases
            return phrases
        raise ValueError(f"No phrases for language '{lang_code}'")
    except Exception as e:
        logging.error(f"Error loading {json_filename} for {language_name}: {e}")
        phrase_cache[cache_key] = []
        return phrase_cache[cache_key]

# -------------------------
# OpenAI Response Processing and Follow-up Generation
# -------------------------

def count_words(text):
    """Count the number of words in a text string."""
    if not text or not text.strip():
        return 0
    return len(text.split())

def enforce_statement_response(text):
    """
    Ensure assistant output is statement-style (no question sentences).
    If all sentences are questions, convert question marks to periods.
    """
    if not text or not text.strip():
        return ""

    cleaned = remove_bullet_asterisks(text).strip()
    cleaned = re.sub(r"\s+", " ", cleaned)

    # Remove participant labels for simpler direct response style.
    cleaned = re.sub(r"\bStudent\s+[A-Za-z0-9]+\b", "", cleaned, flags=re.IGNORECASE)
    cleaned = re.sub(r"\bteacher\b", "", cleaned, flags=re.IGNORECASE)
    cleaned = re.sub(r"\bparticipant(s)?\b", "", cleaned, flags=re.IGNORECASE)
    cleaned = re.sub(r"\s{2,}", " ", cleaned).strip()

    parts = re.split(r"(?<=[.!?])\s+", cleaned)
    non_question_parts = [p.strip() for p in parts if p.strip() and not p.strip().endswith("?")]

    if non_question_parts:
        result = " ".join(non_question_parts)
    else:
        result = re.sub(r"\?+", ".", cleaned)

    result = re.sub(r"\s+", " ", result).strip()
    if result and result[-1] not in ".!":
        result += "."
    return result

def process_openai_response(adjusted_token_limit, gui):
    """
    Process the streaming response from OpenAI, update the GUI in real time,
    perform speech synthesis (TTS) with prefetching, and update the conversation history
    in both memory and MongoDB.
    
    Args:
        adjusted_token_limit (int): The maximum number of tokens allowed for the response.
        gui (object): The GUI instance used to update dialog boxes.
        
    This function:
      - Streams the response from OpenAI.
      - Updates the GUI with incoming text.
      - Monitors word count and stops at complete sentences when limit is reached.
      - Enqueues text for TTS playback using a separate thread.
      - Appends the response to conversation history and MongoDB.
      - Triggers follow-up question generation if applicable.
    """
    total_start_time = time.time()  # Start timing for the whole process
    global conversation_history
    
    word_limit = getattr(gui, 'word_limit', 75)
    interaction_mode = getattr(gui, 'interaction_mode', 'Discussion')
    user_text = next(
        (m["content"] for m in reversed(conversation_history) if m.get("role") == "user"),
        None,
    )
    language_name = resolve_response_language(gui, user_text)
    lang_cfg = get_language_config(language_name)
    logging.info(f"Response language (auto-detected, latest): {language_name}")

    memory_context = ""
    if interaction_mode == "Conversational":
        try:
            from modes.conversation_memory import build_memory_context
            memory_context = build_memory_context(gui)
            if memory_context:
                logging.info(f"Session memory: {memory_context[:120]}...")
        except Exception as exc:
            logging.debug(f"Memory context skipped: {exc}")

    # Update system message with current word limit, language, and memory
    system_msg = get_system_message(
        word_limit, language_name, interaction_mode, memory_context
    )
    if is_realtime_mode(gui):
        system_msg = {
            "role": "system",
            "content": system_msg["content"] + realtime_system_addon(word_limit),
        }
    conversation_history[0] = system_msg
    logging.info(
        f"Mode: {interaction_mode}, Language: {language_name}, "
        f"Model: {'Real Time' if is_realtime_mode(gui) else 'Legacy'} "
        f"({get_realtime_model() if is_realtime_mode(gui) else get_legacy_model()})"
    )
    if is_realtime_mode(gui):
        logging.warning(
            "Real Time + web_search adds latency — use Legacy (gpt-4o-mini) for 2–3s replies"
        )

    set_activity_status(gui, "Thinking…")
    try:
        gui._eagle_response_busy = True
    except Exception:
        pass
    set_tts_mode(interaction_mode)
    configure_tts(locale=lang_cfg["stt_code"])
    
    try:
        logging.info(f"Waiting for response from OpenAI... (Word limit: {word_limit})")
        
        # Prepare Text-to-Speech (TTS)
        speech_queue = queue.Queue()
        selected_phrase = resolve_engaging_phrase(
            openai_client, language_name, user_text, interaction_mode, load_engaging_phrases
        )
        
        # Create event to signal when response TTS finishes (before follow-up starts)
        response_end_event = threading.Event()
        
        # Start TTS thread with intro phrase + main response
        tts_thread = threading.Thread(
            target=parallel_speak, 
            args=(speech_queue, response_end_event, selected_phrase),
            kwargs={
                "on_speak_start": lambda: set_activity_status(gui, "Speaking…"),
                "on_speak_end": lambda: resume_after_eagle_speech(gui),
            },
        )
        tts_thread.daemon = True
        tts_thread.start()
        logging.info("TTS: main response only (no separate intro phrase)")
        
        # Start timing the GPT response generation separately.
        gpt_start_time = time.time()
        gpt_response = ""
        gui_stream_started = False

        def _append_assistant_chunk(chunk):
            nonlocal gui_stream_started
            if not chunk:
                return
            first = not gui_stream_started
            gui_stream_started = True
            try:
                def _do(c=chunk, is_first=first):
                    if is_first:
                        box = gui.dialog_box
                        cur = box.get("1.0", "end-1c")
                        if cur and not cur.endswith("\n"):
                            box.insert(tk.END, "\n")
                    update_dialog_box(c, "assistant", gui)

                if hasattr(gui, "root") and gui.root.winfo_exists():
                    gui.root.after(0, _do)
                else:
                    _do()
            except Exception as exc:
                logging.debug(f"GUI stream append skipped: {exc}")

        # Stream text to screen only — do NOT speak mid-stream (avoids extra/missing last sentence)
        for text_chunk in stream_assistant_reply(
            openai_client,
            gui,
            conversation_history,
            adjusted_token_limit,
            interaction_mode,
        ):
            if not text_chunk:
                continue
            gpt_response += text_chunk
            _append_assistant_chunk(text_chunk)

        # Discussion mode: statements only. Conversational: keep questions — real chat needs them.
        if interaction_mode != "Conversational":
            gpt_response = enforce_statement_response(gpt_response)
        else:
            from modes.conversation_memory import (
                is_greeting_or_how_are_you,
                is_simple_greeting_only,
                sanitize_conversational_reply,
                user_asked_how_eagle_is,
            )
            from modes.conversation_state import get_conversation_state
            last_user = (getattr(gui, "last_submitted_user_text", "") or "").strip()
            state = get_conversation_state(gui)
            gpt_response = sanitize_conversational_reply(
                last_user,
                gpt_response,
                force_no_questions=state.should_skip_question(
                    is_simple_greeting=is_simple_greeting_only(last_user),
                    user_asked_wellbeing=user_asked_how_eagle_is(last_user),
                ),
            )

        gpt_response, tts_text = prepare_assistant_reply(
            gpt_response,
            word_limit,
            is_realtime=is_realtime_mode(gui),
        )
        # Single source of truth: screen and speech use the same final string
        final_text = (gpt_response or "").strip()
        tts_text = (tts_text or final_text).strip()
        if tts_text != final_text:
            tts_text = final_text
        print(f"\n{'='*60}\n[GPT RESPONSE — GUI + TTS SAME]\n{final_text}\n{'='*60}\n")
        logging.info(f"GPT response ({len(final_text)} chars): {final_text}")

        # Commit COMPLETE response to history + refresh screen
        if final_text:
            commit_chat_message(gui, "assistant", final_text)
            try:
                if hasattr(gui, "root") and gui.root.winfo_exists():
                    gui.root.after(0, lambda: refresh_chat_display(gui))
                else:
                    refresh_chat_display(gui)
            except Exception:
                refresh_chat_display(gui)

        # Speak ONLY the final on-screen text — no early/extra/truncated chunks
        spoken_count = 0
        if tts_text:
            for sent in split_into_sentences(tts_text):
                sent = (sent or "").strip()
                if not sent:
                    continue
                spoken_count += 1
                print(f"[TTS QUEUE FINAL #{spoken_count}] '{sent}'")
                speech_queue.put(sent)
                logging.info(f"Queued final sentence #{spoken_count}: '{sent[:80]}...'")
            if spoken_count == 0:
                spoken_count = 1
                speech_queue.put(tts_text)
        print(f"[TTS PROCESSING SUMMARY] Spoken sentences: {spoken_count} (exact final reply)")

        # Mark end of response TTS with a special sentinel
        response_end_marker = "__RESPONSE_END__"
        speech_queue.put(response_end_marker)
        logging.info("Response TTS queuing complete, sent end marker")

        gpt_response = final_text
        final_word_count = count_words(gpt_response)
        if final_word_count > word_limit:
            logging.warning(
                f"Response received: {final_word_count} words "
                f"(EXCEEDED limit of {word_limit} words - AI should have stayed within limit)"
            )
        else:
            logging.info(
                f"Response received: {final_word_count} words (within limit of {word_limit} words)"
            )
        gpt_end_time = time.time()
        gpt_generation_time = gpt_end_time - gpt_start_time
        print("*** GPT response generation time: {:.2f} seconds. ***".format(gpt_generation_time))

        if not gpt_response.strip():
            update_dialog_box("\n[Error] Empty response from Eagle.\n", "status", gui)
            logging.error("Received empty response from OpenAI.")
            speech_queue.put(None)
            tts_thread.join(timeout=60)
            if tts_thread.is_alive():
                logging.warning("TTS thread did not finish within timeout after error, continuing anyway")
            resume_after_eagle_speech(gui)
            return

        # Append the assistant's response to the conversation history.
        assistant_message = {"role": "assistant", "content": gpt_response.strip()}
        conversation_history.append(assistant_message)
        logging.info(f"Eagle response: {gpt_response.strip()}")
        # Update the MongoDB document with the assistant's response.
        update_mongo_document("response", gpt_response.strip())
        
        # OPTIMIZATION: Start follow-up question generation in parallel thread
        # while response TTS is still playing - this eliminates latency
        follow_up_question_result = [None]  # Use list to store result from thread
        
        if should_run_follow_up(gui, interaction_mode):
            # Start follow-up generation in background thread (don't wait for it)
            # display_immediately=False means don't show on screen until response TTS finishes
            def generate_and_store():
                result = generate_follow_up_question(gui, speech_queue, display_immediately=False)
                follow_up_question_result[0] = result
            
            follow_up_thread = threading.Thread(target=generate_and_store, daemon=True)
            follow_up_thread.start()
            logging.info("Started follow-up question generation in parallel with response TTS")
        else:
            # Show disabled message in follow-up question box
            gui.follow_up_box.delete("1.0", tk.END)
            gui.follow_up_box.insert("1.0", "Follow-up question is disabled", "status")
            update_dialog_box("\n(Follow-up questions are disabled.)\n", "status", gui)
            logging.info("Follow-up question generation is disabled by user.")
            # Send termination signal to TTS thread since no follow-up will be queued
            speech_queue.put(None)
        
        # Wait for TTS to finish playing (response + follow-up if generated)
        tts_wait_start = time.time()
        if should_run_follow_up(gui, interaction_mode):
            # Wait for follow-up generation to complete and queue itself
            # This ensures follow-up is queued before TTS thread processes all items
            follow_up_thread.join(timeout=30)  # Max 30 seconds for follow-up generation
            if follow_up_thread.is_alive():
                logging.warning("Follow-up generation timed out, continuing without it")
            # Send termination signal after follow-up is queued (or timed out)
            speech_queue.put(None)
        else:
            # Send termination signal if follow-up is disabled
            speech_queue.put(None)
        
        # Wait for response TTS to finish (not all TTS, just response)
        # This allows us to display follow-up question as soon as response speech finishes
        response_end_event.wait(timeout=60)  # Wait max 60 seconds for response to finish
        if response_end_event.is_set():
            logging.info("Response TTS finished, follow-up question will start after 3-second delay")
            # NOW display the follow-up question on screen (after response speech finished)
            # Note: 3-second delay is handled by TTS thread via __DELAY_3_SECONDS__ marker
            if should_run_follow_up(gui, interaction_mode):
                follow_up_question = follow_up_question_result[0]
                if follow_up_question:
                    # Display follow-up question now that response speech is finished
                    gui.follow_up_box.delete("1.0", tk.END)
                    gui.follow_up_box.insert(tk.END, follow_up_question, "follow_up")
                    logging.info("Follow-up question displayed after response speech finished")
        
        # Wait for all TTS to finish (response + follow-up if generated)
        # Use timeout to prevent hanging if TTS thread gets stuck
        tts_thread.join(timeout=300)  # Max 5 minutes for TTS (should be enough for any response)
        if tts_thread.is_alive():
            logging.warning("TTS thread did not finish within timeout, continuing anyway")
            resume_after_eagle_speech(gui)
        tts_wait_end = time.time()
        tts_processing_time = tts_wait_end - tts_wait_start
        print("*** Total TTS processing time (response + follow-up): {:.2f} seconds. ***".format(tts_processing_time))
        
    except Exception as e:
        # Handle exceptions and update the GUI and logs with error details.
        update_dialog_box(f"\n[Error] {str(e)}\n", "status", gui)
        logging.error(f"OpenAI API Error: {str(e)}")
        # Never leave mic dead after an error — keep listening until Stop
        resume_after_eagle_speech(gui)
    total_end_time = time.time()
    total_time = total_end_time - total_start_time
    print("*** Total process_openai_response time: {:.2f} seconds. ***".format(total_time))

def contains_inappropriate_content(text):
    """
    Check if the text contains adult, explicit, or inappropriate content.
    Uses OpenAI's moderation API to detect inappropriate content.
    
    Args:
        text (str): The text to check for inappropriate content.
    
    Returns:
        bool: True if inappropriate content is detected, False otherwise.
    """
    if not text or not text.strip():
        return False
    
    try:
        # Use OpenAI moderation API to check for inappropriate content
        moderation_response = openai_client.moderations.create(input=text)
        result = moderation_response.results[0]
        
        # Check if content is flagged as inappropriate
        is_flagged = result.flagged
        
        if is_flagged:
            # Log which categories were flagged
            # Access category flags directly from the categories object
            flagged_categories = []
            if hasattr(result, 'categories'):
                categories = result.categories
                # Check each category flag
                category_names = ['hate', 'hate/threatening', 'self-harm', 'sexual', 'sexual/minors', 
                                'violence', 'violence/graphic']
                for cat_name in category_names:
                    if hasattr(categories, cat_name) and getattr(categories, cat_name):
                        flagged_categories.append(cat_name)
            logging.warning(f"Inappropriate content detected. Flagged categories: {flagged_categories}")
            print(f"[CONTENT CHECK] Inappropriate content detected - skipping follow-up question")
            if flagged_categories:
                print(f"  Flagged categories: {flagged_categories}")
            return True
        
        return False
    except Exception as e:
        # If moderation API fails, use a simple keyword check as fallback
        logging.warning(f"Moderation API error: {e}. Using fallback keyword check.")
        inappropriate_keywords = [
            'adult', 'explicit', 'nsfw', 'porn', 'sex', 'sexual', 'nude', 'naked',
            'xxx', 'erotic', 'mature content', '18+', 'adult content'
        ]
        text_lower = text.lower()
        for keyword in inappropriate_keywords:
            if keyword in text_lower:
                logging.warning(f"Inappropriate keyword detected: {keyword}")
                print(f"[CONTENT CHECK] Inappropriate keyword detected - skipping follow-up question")
                return True
        return False

def should_generate_follow_up(response_text):
    """
    Determine if the previous assistant response warrants a follow-up question.
    
    This function calls the OpenAI API with a prompt to decide whether the answer is complete or if
    it should be further explored.
    
    Args:
        response_text (str): The text of the assistant's previous response.
    
    Returns:
        bool: True if a follow-up question is needed, False otherwise.
    """
    # Construct a prompt asking the API to classify the response.
    prompt = (
        "Based on the following answer, decide if it is complete and conclusive, or if it is open-ended "
        "enough to benefit from a follow-up question. Answer only 'Yes' if a follow-up is needed, or 'No' if it is conclusive.\n\n"
        "Answer: \"" + response_text + "\""
    )
    # Call the OpenAI API with a strict system message instructing a 'Yes' or 'No' answer.
    classification_response = openai_client.chat.completions.create(
        model="gpt-4o-mini",
        messages=[
            {"role": "system", "content": "You are an assistant that judges if an answer is open-ended. Answer only 'Yes' or 'No'."},
            {"role": "user", "content": prompt}
        ],
        max_tokens=5
    )
    # Get the decision, normalize to lowercase, and return True if it starts with 'yes'.
    decision = classification_response.choices[0].message.content.strip().lower()
    return decision.startswith("yes")

def generate_follow_up_question(gui, speech_queue=None, display_immediately=True):
    """
    Generate a follow-up question based on the previous discussion if the assistant's response is deemed open-ended.
    
    The function updates the GUI's follow-up question box, stores the follow-up in the conversation history and MongoDB,
    and queues the question for TTS playback (or uses text-to-speech directly if speech_queue is not provided).
    
    Args:
        gui (object): The GUI instance used for updating the follow-up question display.
        speech_queue (queue.Queue, optional): If provided, queues the follow-up question TTS to this queue
                                            for seamless playback after response. If None, uses text_to_speech_realtime.
        display_immediately (bool): If True, displays the question in GUI immediately. If False, only generates
                                   and queues TTS (display happens later). Default True for backward compatibility.
    
    Returns:
        str: The generated follow-up question, or None if generation failed or was skipped.
    """
    global conversation_history
    # Check if there is sufficient conversation history to generate a follow-up.
    if not conversation_history or len(conversation_history) < 2:
        update_dialog_box("\n[Error] Not enough context for follow-up question generation.\n", "status", gui)
        logging.error("Not enough conversation history for follow-up generation.")
        return None

    # CRITICAL: Check if user's question contains adult/explicit content
    # Find the last user message (the question/prompt)
    user_prompt = None
    for message in reversed(conversation_history):
        if message.get("role") == "user":
            user_prompt = message.get("content", "")
            break
    
    if user_prompt:
        if contains_inappropriate_content(user_prompt):
            if display_immediately:
                gui.follow_up_box.delete("1.0", tk.END)
                gui.follow_up_box.insert("1.0", "Follow-up question not generated due to content policy.", "status")
            update_dialog_box("\n(Follow-up question not generated due to content policy.)\n", "status", gui)
            logging.warning("Follow-up question generation skipped due to inappropriate content in user prompt.")
            return None

    # Extract the assistant's most recent response.
    assistant_response = conversation_history[-1]['content']
    
    # Also check assistant's response for inappropriate content
    if contains_inappropriate_content(assistant_response):
        if display_immediately:
            gui.follow_up_box.delete("1.0", tk.END)
            gui.follow_up_box.insert("1.0", "Follow-up question not generated due to content policy.", "status")
        update_dialog_box("\n(Follow-up question not generated due to content policy.)\n", "status", gui)
        logging.warning("Follow-up question generation skipped due to inappropriate content in assistant response.")
        return None
    
    # Use the helper function to decide if a follow-up is needed.
    if not should_generate_follow_up(assistant_response):
        if display_immediately:
            update_dialog_box("\n(No follow-up question generated.)\n", "status", gui)
        logging.info("Response deemed conclusive; no follow-up question generated by AI.")
        return None

    # Define a prompt to instruct the API to generate a critical follow-up question with maximum 50-word limit.
    follow_up_word_limit = 50
    follow_up_prompt = (
        f"Based on the previous discussion, generate a critical follow-up question that challenges the topic further. "
        f"The question should encourage deeper analysis and require the assistant to provide its own insights and reasoning when answering. "
        f"Keep your question concise and focused. Do not include any appreciation or acknowledgment."
    )
    # Copy the current conversation history and add the follow-up prompt as a new user message.
    temp_history = conversation_history.copy()
    temp_history.append({"role": "user", "content": follow_up_prompt})
    # Dynamically calculate the token allocation for generating the follow-up question.
    adjusted_token_limit = dynamic_token_allocation(follow_up_prompt, temp_history, min_response_tokens=80)
    follow_up_token_limit = min(adjusted_token_limit, 80)

    try:
        # Request the follow-up question from the OpenAI API using streaming mode.
        response = openai_client.chat.completions.create(
            model="gpt-4o-mini",
            messages=temp_history,
            max_tokens=follow_up_token_limit,
            stream=True
        )
        follow_up_question = ""
        
        # Stream the response chunks and build the full follow-up question - let it complete fully
        for chunk in response:
            if hasattr(chunk, "choices") and chunk.choices:
                text_chunk = chunk.choices[0].delta.content or ""
                if text_chunk:
                    # Add chunk to response - let it complete fully
                    follow_up_question += text_chunk
        
        follow_up_question = follow_up_question.strip()
        
        # Final word count check and logging (complete response received)
        final_word_count = count_words(follow_up_question)
        # Log the word count but don't truncate - response is complete
        logging.info(f"Complete follow-up question received: {final_word_count} words (word limit setting: {follow_up_word_limit})")
        
        # Remove any bullet point asterisks from the generated question.
        follow_up_question = remove_bullet_asterisks(follow_up_question)

        if follow_up_question:
            # Only display immediately if requested (otherwise display after response TTS finishes)
            if display_immediately:
                gui.follow_up_box.delete("1.0", tk.END)
                gui.follow_up_box.insert(tk.END, follow_up_question, "follow_up")
            logging.info(f"Eagle follow-up question: {follow_up_question}")
            # Update the conversation history in MongoDB with the follow-up.
            update_mongo_document("followup", follow_up_question)
            
            # OPTIMIZATION: Queue follow-up question to speech_queue for seamless playback
            # Add 3-second delay marker before follow-up question sentences
            if speech_queue is not None:
                # Queue delay marker first - TTS thread will wait 3 seconds when it sees this
                speech_queue.put("__DELAY_3_SECONDS__")
                logging.info("3-second delay marker queued before follow-up question")
                # Split follow-up question into sentences and queue them
                sentences = re.split(r'([.!?]+)', follow_up_question)
                sentences = [i + j for i, j in zip(sentences[::2], sentences[1::2])]
                for sentence in sentences:
                    if sentence.strip():
                        speech_queue.put(sentence.strip())
                logging.info("Follow-up question queued to speech queue for playback")
            else:
                # Fallback: use direct TTS if speech_queue not provided (backward compatibility)
                text_to_speech_realtime(follow_up_question)
            
            return follow_up_question
        else:
            if display_immediately:
                update_dialog_box("\n[Error] No follow-up question generated.\n", "status", gui)
            logging.error("No valid follow-up question was returned by GPT.")
            return None

    except Exception as e:
        # Handle any errors during follow-up generation.
        update_dialog_box(f"\n[Error generating follow-up question] {str(e)}\n", "status", gui)
        logging.error(f"OpenAI API Error (Follow-up Generation): {str(e)}")
