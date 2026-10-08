"""
Supported languages for Eagle App (from ELEVENLABS_LANGUAGE_SUPPORT.md).
ElevenLabs eleven_v3 speaks all of these; GPT + auto-detect use this registry.
"""

# name -> config
SUPPORTED_LANGUAGES = {
    # Indian & South Asian regional
    "Assamese": {"code": "as", "stt_code": "as-IN", "prompt_language": "Assamese"},
    "Bengali": {"code": "bn", "stt_code": "bn-IN", "prompt_language": "Bengali"},
    "Gujarati": {"code": "gu", "stt_code": "gu-IN", "prompt_language": "Gujarati"},
    "Hindi": {"code": "hi", "stt_code": "hi-IN", "prompt_language": "Hindi in Devanagari script"},
    "Kannada": {"code": "kn", "stt_code": "kn-IN", "prompt_language": "Kannada in Kannada script (ಕನ್ನಡ)"},
    "Malayalam": {"code": "ml", "stt_code": "ml-IN", "prompt_language": "Malayalam"},
    "Marathi": {"code": "mr", "stt_code": "mr-IN", "prompt_language": "Marathi"},
    "Nepali": {"code": "ne", "stt_code": "ne-NP", "prompt_language": "Nepali"},
    "Punjabi": {"code": "pa", "stt_code": "pa-IN", "prompt_language": "Punjabi"},
    "Tamil": {"code": "ta", "stt_code": "ta-IN", "prompt_language": "Tamil"},
    "Telugu": {"code": "te", "stt_code": "te-IN", "prompt_language": "Telugu in Telugu script (తెలుగు)"},
    "Urdu": {"code": "ur", "stt_code": "ur-IN", "prompt_language": "Urdu"},
    # International
    "Afrikaans": {"code": "af", "stt_code": "af-ZA", "prompt_language": "Afrikaans"},
    "Arabic": {"code": "ar", "stt_code": "ar-SA", "prompt_language": "Arabic"},
    "Bulgarian": {"code": "bg", "stt_code": "bg-BG", "prompt_language": "Bulgarian"},
    "Danish": {"code": "da", "stt_code": "da-DK", "prompt_language": "Danish"},
    "German": {"code": "de", "stt_code": "de-DE", "prompt_language": "German"},
    "English": {"code": "en", "stt_code": "en-US", "prompt_language": "English"},
    "Spanish": {"code": "es", "stt_code": "es-ES", "prompt_language": "Spanish"},
    "Persian": {"code": "fa", "stt_code": "fa-IR", "prompt_language": "Persian (Farsi)"},
    "Finnish": {"code": "fi", "stt_code": "fi-FI", "prompt_language": "Finnish"},
    "French": {"code": "fr", "stt_code": "fr-FR", "prompt_language": "French"},
    "Irish": {"code": "ga", "stt_code": "en-IE", "prompt_language": "Irish"},
    "Hungarian": {"code": "hu", "stt_code": "hu-HU", "prompt_language": "Hungarian"},
    "Armenian": {"code": "hy", "stt_code": "hy-AM", "prompt_language": "Armenian"},
    "Indonesian": {"code": "id", "stt_code": "id-ID", "prompt_language": "Indonesian"},
    "Italian": {"code": "it", "stt_code": "it-IT", "prompt_language": "Italian"},
    "Japanese": {"code": "ja", "stt_code": "ja-JP", "prompt_language": "Japanese"},
    "Georgian": {"code": "ka", "stt_code": "ka-GE", "prompt_language": "Georgian"},
    "Korean": {"code": "ko", "stt_code": "ko-KR", "prompt_language": "Korean"},
    "Dutch": {"code": "nl", "stt_code": "nl-NL", "prompt_language": "Dutch"},
    "Norwegian": {"code": "no", "stt_code": "nb-NO", "prompt_language": "Norwegian"},
    "Polish": {"code": "pl", "stt_code": "pl-PL", "prompt_language": "Polish"},
    "Pashto": {"code": "ps", "stt_code": "ps-AF", "prompt_language": "Pashto"},
    "Portuguese": {"code": "pt", "stt_code": "pt-PT", "prompt_language": "Portuguese"},
    "Romanian": {"code": "ro", "stt_code": "ro-RO", "prompt_language": "Romanian"},
    "Russian": {"code": "ru", "stt_code": "ru-RU", "prompt_language": "Russian"},
    "Slovak": {"code": "sk", "stt_code": "sk-SK", "prompt_language": "Slovak"},
    "Slovenian": {"code": "sl", "stt_code": "sl-SI", "prompt_language": "Slovenian"},
    "Somali": {"code": "so", "stt_code": "so-SO", "prompt_language": "Somali"},
    "Serbian": {"code": "sr", "stt_code": "sr-RS", "prompt_language": "Serbian"},
    "Swedish": {"code": "sv", "stt_code": "sv-SE", "prompt_language": "Swedish"},
    "Thai": {"code": "th", "stt_code": "th-TH", "prompt_language": "Thai"},
    "Turkish": {"code": "tr", "stt_code": "tr-TR", "prompt_language": "Turkish"},
    "Ukrainian": {"code": "uk", "stt_code": "uk-UA", "prompt_language": "Ukrainian"},
    "Vietnamese": {"code": "vi", "stt_code": "vi-VN", "prompt_language": "Vietnamese"},
    "Chinese": {"code": "zh", "stt_code": "zh-CN", "prompt_language": "Chinese (Simplified)"},
}

# Indian regional languages (prioritized for detection + STT)
INDIAN_LANGUAGE_NAMES = {
    "Assamese", "Bengali", "Gujarati", "Hindi", "Kannada", "Malayalam",
    "Marathi", "Nepali", "Punjabi", "Tamil", "Telugu", "Urdu",
}

CODE_TO_NAME = {cfg["code"]: name for name, cfg in SUPPORTED_LANGUAGES.items()}

# langdetect ISO 639-1 -> display name
ISO_TO_NAME = {
    "af": "Afrikaans", "ar": "Arabic", "as": "Assamese", "bg": "Bulgarian", "bn": "Bengali",
    "da": "Danish", "de": "German", "en": "English", "es": "Spanish",
    "fa": "Persian", "fi": "Finnish", "fr": "French", "gu": "Gujarati",
    "hi": "Hindi", "hu": "Hungarian", "hy": "Armenian", "id": "Indonesian", "it": "Italian",
    "ja": "Japanese", "ka": "Georgian", "kn": "Kannada", "ko": "Korean", "ml": "Malayalam",
    "mr": "Marathi", "ne": "Nepali", "nl": "Dutch", "no": "Norwegian",
    "pa": "Punjabi", "pl": "Polish", "ps": "Pashto", "pt": "Portuguese", "ro": "Romanian",
    "ru": "Russian", "sk": "Slovak", "sl": "Slovenian", "so": "Somali", "sr": "Serbian",
    "sv": "Swedish", "ta": "Tamil", "te": "Telugu", "th": "Thai",
    "tr": "Turkish", "uk": "Ukrainian", "ur": "Urdu", "vi": "Vietnamese",
    "zh-cn": "Chinese", "zh-tw": "Chinese", "zh": "Chinese",
}

# Google STT BCP-47 -> display name
STT_CODE_TO_NAME = {}
for _name, _cfg in SUPPORTED_LANGUAGES.items():
    STT_CODE_TO_NAME[_cfg["stt_code"].lower()] = _name
STT_CODE_TO_NAME["en-in"] = "English"

# Indian-first STT (Google: primary + 3 alts — Alexa/GPT detect language from AUDIO)
INDIAN_STT_CODES = [
    "ml-IN", "hi-IN", "ta-IN", "te-IN", "kn-IN", "bn-IN", "mr-IN", "gu-IN",
    "pa-IN", "ur-IN", "as-IN", "ne-NP", "en-IN",
]

STT_PRIORITY_POOL = INDIAN_STT_CODES + [
    "en-US", "es-ES", "fr-FR", "de-DE", "fi-FI", "ar-SA", "ja-JP", "zh-CN",
    "ko-KR", "ru-RU", "pt-PT", "it-IT", "th-TH", "vi-VN", "tr-TR",
]

# Romanized Indian hints — before langdetect (prevents Malayalam → Indonesian errors)
INDIAN_KEYWORD_HINTS = {
    "Malayalam": (
        r"\b(ente|ningal|njan|aanu|aan|illa|undo|entha|namukku|namaskaram|nanni|"
        r"evide|engane|parayu|ariyamo|kollam|sheri|alle|mathi|venda|chetta|chechi|"
        r"mone|mole|enthina|ippol|nale|innu|avide|ivide|ariyilla|sukham|"
        r"malayalam|kerala|ningalude|paranjhu|kelkamo|enth|parayamo)\b"
    ),
    "Tamil": (
        r"\b(enna|epdi|eppadi|nandri|vanakkam|illai|unga|naan|ungaluku|paathi|"
        r"sollu|romba|nalla|inge|ange|sari|illaya|theriyuma|tamil)\b"
    ),
    "Telugu": (
        r"\b(nenu|meeru|ela|emiti|emaiti|em|ledu|undhi|undi|cheppu|cheppandi|dhanyavadalu|"
        r"namaskaram|bagunnara|telugu|ikkada|akkada|avunu|kaadu|baga|enti|chustunna|"
        r"vastunnanu|undandi|mundu|taruvata|enduku|eppudu|ekkada|chala|koncem|"
        r"matladutunnanu|ardham|kavali|ivvandi|chestunnanu|chestanu|unnaru|"
        r"unnaanu|mana|vadiki|ame|atanu)\b"
    ),
    "Kannada": (
        r"\b(nanu|neenu|hege|yaake|illa|ide|helu|helri|dhanyavadagalu|namaskara|"
        r"kannada|illi|alli|houdu|sari|hesaru|madbek|barutte|nodi|matadu|"
        r"swalpa|bahala|chennagide|gottu|gottilla)\b"
    ),
    "Bengali": (
        r"\b(ami|tumi|apni|kemon|ki|na|hoy|bolo|dhonnobad|namaskar|bangla|"
        r"ekhane|okhane|bhalo|kharap)\b"
    ),
    "Gujarati": (
        r"\b(hu|tu|tame|kem|shu|nathi|che|kaho|aabhar|namaste|gujarati|"
        r"ahiya|tya|saru|kharab)\b"
    ),
    "Marathi": (
        r"\b(mi|tu|tumhi|kasa|kay|nahi|aahe|sanga|dhanyavaad|namaskar|"
        r"marathi|ithe|tithe|changa|vait)\b"
    ),
    "Punjabi": (
        r"\b(main|tu|tusi|ki|hai|nahi|daso|shukriya|sat|sri|akal|punjabi|"
        r"ithe|othe|vadiya)\b"
    ),
    "Hindi": (
        r"\b(kaun|kya|kyun|kaise|hai|hain|nahin|nahi|pradhan|mantri|kahan|kab|"
        r"kyaa|hoon|mera|tera|aap|tum|naam|bharat|desh|kyu|kaunsa|kaunsi|"
        r"dhanyavaad|namaste|accha|theek|haan|ji|mujhe|hum|aapke|bahut|"
        r"samajh|batao|bolo|sunao|haanji|matlb|kyunki|phir|abhi|"
        r"chahiye|karna|karte|karo|rahe|rahi|tha|thi|the)\b"
    ),
    "Urdu": (
        r"\b(main|aap|tum|kya|kaise|hai|nahin|nahi|shukriya|salam|urdu|"
        r"theek|acha|kahan|kab|kyun)\b"
    ),
    "Assamese": r"\b(moi|tumi|apuni|kene|ki|nai|ase|kobo|dhonyobad|namaskar)\b",
    "Nepali": r"\b(ma|timi|tapai|kasto|ke|chaina|cha|bhannu|dhanyabaad|namaste)\b",
}

LANGUAGE_KEYWORD_HINTS = {
    "French": r"\b(bonjour|merci|comment|vous|nous|une|les|des|je|est|pas|avec|pour)\b",
    "Spanish": r"\b(hola|gracias|como|cómo|donde|dónde|porque|buenos|está|muy|señor)\b",
    "German": r"\b(hallo|danke|wie|nicht|ich|sie|ein|das|und|ist|guten)\b",
    "Italian": r"\b(ciao|grazie|come|sono|questo|bene|perché|molto)\b",
    "Portuguese": r"\b(olá|obrigado|como|você|não|sim|bom|dia)\b",
    "Dutch": r"\b(hallo|dank|hoe|niet|van|het|een|goed)\b",
    "Russian": r"\b(привет|спасибо|как|да|нет|хорошо)\b",
    "Japanese": r"\b(こんにちは|ありがとう|です|ます)\b",
    "Korean": r"\b(안녕|감사|합니다|입니다)\b",
    "Arabic": r"\b(مرحبا|شكرا|كيف|نعم|لا)\b",
    "Turkish": r"\b(merhaba|teşekkür|nasıl|evet|hayır)\b",
    "Vietnamese": r"\b(xin chào|cảm ơn|không|vâng)\b",
    "Thai": r"\b(สวัสดี|ขอบคุณ|ครับ|ค่ะ)\b",
    "Chinese": r"\b(你好|谢谢|什么|吗|的)\b",
    "Swedish": r"\b(hej|tack|hur|inte|jag|är)\b",
    "Polish": r"\b(cześć|dziękuję|jak|nie|tak)\b",
}


def is_indian_language(name):
    return name in INDIAN_LANGUAGE_NAMES


def get_stt_config(detected_language_name="English", single_language=False):
    """
    Primary + up to 3 STT codes (Google streaming limit).
    Multi discovery always keeps Hindi + Telugu + English in the set so
    Indian speech is not forced through English-only recognition.
    """
    if detected_language_name == "English":
        primary = "en-IN"
    else:
        primary = get_language_config(detected_language_name)["stt_code"]
    if single_language:
        return primary, []
    # Prefer hi/te/ta/kn before less-common codes (old order put ml first and dropped te)
    preferred = [
        "hi-IN", "te-IN", "ta-IN", "en-IN", "kn-IN", "ml-IN", "bn-IN",
        "mr-IN", "gu-IN", "pa-IN", "ur-IN",
    ]
    alts = []
    for c in preferred:
        if c != primary and c not in alts:
            alts.append(c)
        if len(alts) >= 3:
            break
    if len(alts) < 3:
        for c in STT_PRIORITY_POOL:
            if c != primary and c not in alts:
                alts.append(c)
            if len(alts) >= 3:
                break
    return primary, alts


def name_from_stt_code(bcp47):
    """Map Google STT BCP-47 tag to our language display name."""
    if not bcp47:
        return None
    key = bcp47.strip().lower().replace("_", "-")
    if key in STT_CODE_TO_NAME:
        return STT_CODE_TO_NAME[key]
    base = key.split("-")[0]
    return CODE_TO_NAME.get(base)


def get_language_config(language_name="English"):
    return SUPPORTED_LANGUAGES.get(language_name, SUPPORTED_LANGUAGES["English"])

