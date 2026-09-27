"""Official Piper voice catalog and Windows SAPI language metadata."""

from __future__ import annotations

import json
import unicodedata
import uuid
import urllib.request
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VOICES_JSON_URL = "https://huggingface.co/rhasspy/piper-voices/resolve/main/voices.json"
HF = "https://huggingface.co/rhasspy/piper-voices/resolve/main/"
APP_NAMESPACE = uuid.UUID("6f1c9a20-4b7e-4d19-9a30-c5e8b1d47f02")
VERSION = "1.3.0"
# Keep each language installer under GitHub's 2 GB release-asset limit.
MAX_PACK_BYTES = 1_050_000_000

# Windows LANGIDs (hex, no 0x). Chromium matches voices by this value.
LANGIDS = {
    "ar_JO": 0x2C01,
    "bg_BG": 0x0402,
    "bn_BD": 0x0845,
    "ca_ES": 0x0403,
    "cs_CZ": 0x0405,
    "cy_GB": 0x0452,
    "da_DK": 0x0406,
    "de_DE": 0x0407,
    "el_GR": 0x0408,
    "en_GB": 0x0809,
    "en_US": 0x0409,
    "es_AR": 0x2C0A,
    "es_ES": 0x0C0A,
    "es_MX": 0x080A,
    "et_EE": 0x0425,
    "eu_ES": 0x042D,
    "fa_IR": 0x0429,
    "fi_FI": 0x040B,
    "fr_FR": 0x040C,
    "he_IL": 0x040D,
    "hi_IN": 0x0439,
    "hu_HU": 0x040E,
    "hy_AM": 0x042B,
    "id_ID": 0x0421,
    "is_IS": 0x040F,
    "it_IT": 0x0410,
    "ja_JP": 0x0411,
    "ka_GE": 0x0437,
    "kk_KZ": 0x043F,
    "ko_KR": 0x0412,
    "ku_TR": 0x0492,
    "lb_LU": 0x046E,
    "lt_LT": 0x0427,
    "lv_LV": 0x0426,
    "ml_IN": 0x044C,
    "mr_IN": 0x044E,
    "ne_NP": 0x0461,
    "nl_BE": 0x0813,
    "nl_NL": 0x0413,
    "no_NO": 0x0414,
    "pl_PL": 0x0415,
    "pt_BR": 0x0416,
    "pt_PT": 0x0816,
    "ro_RO": 0x0418,
    "ru_RU": 0x0419,
    "sk_SK": 0x041B,
    "sl_SI": 0x0424,
    "sq_AL": 0x041C,
    "sr_RS": 0x281A,
    "sv_SE": 0x041D,
    "sw_CD": 0x0441,
    "te_IN": 0x044A,
    "th_TH": 0x041E,
    "tr_TR": 0x041F,
    "uk_UA": 0x0422,
    "ur_PK": 0x0420,
    "vi_VN": 0x042A,
    "zh_CN": 0x0804,
}

SAMPLES = {
    "ar_JO": "مرحبا. هذا اختبار لصوت بايبر.",
    "bg_BG": "Здравейте. Това е тест на глас Пайпър.",
    "bn_BD": "হ্যালো। এটি একটি পাইপার ভয়েস পরীক্ষা।",
    "ca_ES": "Hola. Aquesta és una prova de la veu Piper.",
    "cs_CZ": "Dobrý den. Toto je test hlasu Piper.",
    "cy_GB": "Helo. Prawf llais Piper yw hwn.",
    "da_DK": "Hej. Dette er en test af Piper-stemmen.",
    "de_DE": "Guten Tag. Dies ist ein Test der Piper-Stimme.",
    "el_GR": "Γεια σας. Αυτή είναι μια δοκιμή της φωνής Piper.",
    "en_GB": "Hello. This is a test of the Piper voice.",
    "en_US": "Hello. This is a test of the Piper voice.",
    "es_AR": "Hola. Esta es una prueba de la voz Piper.",
    "es_ES": "Hola. Esta es una prueba de la voz Piper.",
    "es_MX": "Hola. Esta es una prueba de la voz Piper.",
    "et_EE": "Tere. See on Piperi hääle test.",
    "eu_ES": "Kaixo. Hau Piper ahotsaren proba bat da.",
    "fa_IR": "سلام. این یک آزمایش صدای پایپر است.",
    "fi_FI": "Hei. Tämä on Piper-äänen testi.",
    "fr_FR": "Bonjour. Ceci est un test de la voix Piper.",
    "he_IL": "שלום. זו בדיקה של קול פייפר.",
    "hi_IN": "नमस्ते। यह पाइपर आवाज़ का परीक्षण है।",
    "hu_HU": "Jó napot. Ez a Piper hang tesztje.",
    "hy_AM": "Բարև։ Սա Piper ձայնի փորձարկում է։",
    "id_ID": "Halo. Ini adalah tes suara Piper.",
    "is_IS": "Halló. Þetta er próf á Piper röddinni.",
    "it_IT": "Buongiorno. Questa è una prova della voce Piper.",
    "ja_JP": "こんにちは。これはパイパー音声のテストです。",
    "ka_GE": "გამარჯობა. ეს არის Piper ხმის ტესტი.",
    "kk_KZ": "Сәлеметсіз бе. Бұл Piper дауысының сынағы.",
    "ko_KR": "안녕하세요. 이것은 파이퍼 음성 테스트입니다.",
    "ku_TR": "Silav. Ev ceribandineke dengê Piper e.",
    "lb_LU": "Moien. Dëst ass en Test vun der Piper-Stëmm.",
    "lt_LT": "Sveiki. Tai Piper balso testas.",
    "lv_LV": "Sveiki. Šis ir Piper balss tests.",
    "ml_IN": "ഹലോ. ഇത് പൈപ്പർ ശബ്ദത്തിന്റെ പരീക്ഷണമാണ്.",
    "mr_IN": "नमस्कार. ही पायपर आवाजाची चाचणी आहे.",
    "ne_NP": "नमस्ते। यो पाइपर आवाजको परीक्षण हो।",
    "nl_BE": "Hallo. Dit is een test van de Piper-stem.",
    "nl_NL": "Hallo. Dit is een test van de Piper-stem.",
    "no_NO": "Hei. Dette er en test av Piper-stemmen.",
    "pl_PL": "Dzień dobry. To jest test głosu Piper.",
    "pt_BR": "Olá. Este é um teste da voz Piper.",
    "pt_PT": "Olá. Este é um teste da voz Piper.",
    "ro_RO": "Bună ziua. Acesta este un test al vocii Piper.",
    "ru_RU": "Здравствуйте. Это тест голоса Piper.",
    "sk_SK": "Dobrý deň. Toto je test hlasu Piper.",
    "sl_SI": "Dober dan. To je preizkus glasu Piper.",
    "sq_AL": "Përshëndetje. Ky është një test i zërit Piper.",
    "sr_RS": "Здраво. Ово је тест гласа Пајпер.",
    "sv_SE": "Hej. Det här är ett test av Piper-rösten.",
    "sw_CD": "Habari. Hii ni jaribio la sauti ya Piper.",
    "te_IN": "హలో. ఇది పైపర్ వాయిస్ పరీక్ష.",
    "th_TH": "สวัสดี นี่คือการทดสอบเสียงไพเพอร์",
    "tr_TR": "Merhaba. Bu bir Piper sesi denemesidir.",
    "uk_UA": "Добрий день. Це тест голосу Piper.",
    "ur_PK": "سلام۔ یہ پائپر آواز کا ٹیسٹ ہے۔",
    "vi_VN": "Xin chào. Đây là bài kiểm tra giọng Piper.",
    "zh_CN": "你好。这是 Piper 语音测试。",
}

# Speaker ids that should be labeled Female in the Windows voice list.
FEMALE = {
    "aegis_female",
    "alba",
    "alma",
    "amy",
    "anna",
    "aru",
    "berta",
    "cori",
    "daniela",
    "eva_k",
    "gosia",
    "hfc_female",
    "huayan",
    "irina",
    "jenny_dioco",
    "joy",
    "kasandra",
    "kathleen",
    "kerstin",
    "kristin",
    "lada",
    "lili",
    "lisa",
    "ljspeech",
    "maider",
    "marylux",
    "maya",
    "meera",
    "mls",
    "mls_10246",
    "mls_1840",
    "mls_5809",
    "mls_6892",
    "mls_7432",
    "mls_9972",
    "natia",
    "nathalie",
    "padmavathi",
    "paola",
    "priyamvada",
    "ramona",
    "rapunzelina",
    "raya",
    "reginute1",
    "salka",
    "serena",
    "siwis",
    "southern_english_female",
    "tetiana",
    "ugla",
    "upc_ona",
    "xiao_ya",
}

POLISH_DISPLAY = {
    "pl_PL-bass-high": "BASS HIGH",
    "pl_PL-darkman-medium": "DARKMAN MEDIUM",
    "pl_PL-gosia-medium": "GOSIA MEDIUM",
    "pl_PL-mc_speech-medium": "MC SPEECH MEDIUM",
    "pl_PL-mls_6892-low": "MLS 6892 LOW",
}


@dataclass
class Voice:
    source_key: str
    voice_id: str
    speaker: str
    quality: str
    locale_code: str
    language_name: str
    country_name: str
    display_name: str
    gender: str
    lang_id: str
    locale: str
    sample: str
    size_bytes: int
    files: dict


@dataclass
class Pack:
    pack_id: str
    locale_code: str
    title: str
    file_stem: str
    app_id: str
    sample: str
    voices: list[Voice]

    @property
    def setup_name(self) -> str:
        return self.file_stem + "-Setup"


def voices_json_path() -> Path:
    return ROOT / "build" / "voices.json"


def ensure_voices_json() -> Path:
    dest = voices_json_path()
    if dest.exists() and dest.stat().st_size > 1000:
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(VOICES_JSON_URL, headers={"User-Agent": "PiperSAPI5"})
    with urllib.request.urlopen(request, timeout=120) as response:
        dest.write_bytes(response.read())
    return dest


def ascii_slug(text: str) -> str:
    folded = unicodedata.normalize("NFKD", text)
    folded = "".join(ch for ch in folded if not unicodedata.combining(ch))
    out = []
    for ch in folded:
        if ch.isascii() and (ch.isalnum() or ch in "_-"):
            out.append(ch)
    return "".join(out)


def file_slug(text: str) -> str:
    parts = []
    for word in text.replace("(", " ").replace(")", " ").replace(",", " ").split():
        clean = "".join(ch for ch in word if ch.isalnum())
        if clean:
            parts.append(clean)
    return "-".join(parts)


def load_voices() -> list[Voice]:
    data = json.loads(ensure_voices_json().read_text(encoding="utf-8"))
    prelim = []
    for key, entry in data.items():
        language = entry["language"]
        code = language["code"]
        if code not in LANGIDS:
            raise SystemExit(f"No Windows language id for {code}")
        speaker = entry["name"]
        quality = entry["quality"]
        voice_id = ascii_slug(key)
        if not voice_id:
            raise SystemExit(f"Could not make a safe id for {key}")
        size = sum(item.get("size_bytes", 0) for item in entry["files"].values() if isinstance(item, dict))
        base = POLISH_DISPLAY.get(voice_id)
        if not base:
            base = f"{speaker} {quality}".replace("_", " ").upper()
        gender = "Female" if speaker.lower() in FEMALE or "female" in speaker.lower() else "Male"
        if voice_id in POLISH_DISPLAY:
            gender = {
                "pl_PL-bass-high": "Male",
                "pl_PL-darkman-medium": "Male",
                "pl_PL-gosia-medium": "Female",
                "pl_PL-mc_speech-medium": "Male",
                "pl_PL-mls_6892-low": "Female",
            }[voice_id]
        prelim.append(
            {
                "source_key": key,
                "voice_id": voice_id,
                "speaker": speaker,
                "quality": quality,
                "locale_code": code,
                "language_name": language["name_english"],
                "country_name": language.get("country_english") or "",
                "base": base,
                "gender": gender,
                "lang_id": format(LANGIDS[code], "X"),
                "locale": code.replace("_", "-"),
                "sample": SAMPLES[code],
                "size_bytes": size,
                "files": entry["files"],
            }
        )
    counts: dict[str, int] = {}
    for item in prelim:
        counts[item["base"]] = counts.get(item["base"], 0) + 1
    voices = []
    for item in prelim:
        display = item["base"]
        if counts[display] > 1:
            display = f"{display} ({item['language_name'].upper()})"
        item["display_name"] = display
        del item["base"]
        voices.append(Voice(**item))
    voices.sort(key=lambda voice: (voice.language_name, voice.country_name, voice.display_name))
    return voices


def language_title(language_name: str, country_name: str, shared_name: bool) -> str:
    if shared_name and country_name:
        return f"Piper {language_name} ({country_name}) Language"
    return f"Piper {language_name} Language"


def build_packs(voices: list[Voice] | None = None) -> list[Pack]:
    voices = voices if voices is not None else load_voices()
    by_code: dict[str, list[Voice]] = {}
    for voice in voices:
        by_code.setdefault(voice.locale_code, []).append(voice)
    name_counts: dict[str, int] = {}
    for group in by_code.values():
        name_counts[group[0].language_name] = name_counts.get(group[0].language_name, 0) + 1
    packs: list[Pack] = []
    for code in sorted(by_code, key=lambda item: (by_code[item][0].language_name, by_code[item][0].country_name)):
        group = by_code[code]
        title = language_title(group[0].language_name, group[0].country_name, name_counts[group[0].language_name] > 1)
        bins: list[dict] = []
        for voice in sorted(group, key=lambda item: item.size_bytes, reverse=True):
            placed = False
            for bin_ in bins:
                if bin_["size"] + voice.size_bytes <= MAX_PACK_BYTES:
                    bin_["voices"].append(voice)
                    bin_["size"] += voice.size_bytes
                    placed = True
                    break
            if not placed:
                bins.append({"voices": [voice], "size": voice.size_bytes})
        several = len(bins) > 1
        for index, bin_ in enumerate(bins, start=1):
            pack_title = f"{title} (part {index} of {len(bins)})" if several else title
            stem = file_slug(title)
            if several:
                stem = f"{stem}-{index}"
                pack_key = f"{code}-{index}"
            else:
                pack_key = code
            ordered = sorted(bin_["voices"], key=lambda item: item.display_name)
            packs.append(
                Pack(
                    pack_id=pack_key,
                    locale_code=code,
                    title=pack_title,
                    file_stem=stem,
                    app_id="{" + str(uuid.uuid5(APP_NAMESPACE, "piper-sapi5/" + pack_key)).upper() + "}",
                    sample=group[0].sample,
                    voices=ordered,
                )
            )
    return packs


def find_pack(language: str, packs: list[Pack] | None = None) -> list[Pack]:
    packs = packs if packs is not None else build_packs()
    key = language.strip().lower().replace("-", "_")
    matched = [
        pack
        for pack in packs
        if pack.pack_id.lower() == key
        or pack.locale_code.lower() == key
        or pack.file_stem.lower() == key
        or pack.title.lower() == language.strip().lower()
    ]
    if not matched:
        known = ", ".join(pack.pack_id for pack in packs)
        raise SystemExit(f"Unknown language '{language}'. Known packs: {known}")
    return matched
