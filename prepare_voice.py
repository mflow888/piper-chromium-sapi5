"""Download Piper voices and write the files the SAPI engine loads."""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

import onnx

from piper_catalog import HF, ROOT, Voice, find_pack, load_voices

THIRD = ROOT / "third_party"
SHERPA_LIB = THIRD / "sherpa" / "sherpa-onnx-v1.13.7-win-x64-shared-MD-Release" / "lib"
ESPEAK = THIRD / "espeak" / "espeak-ng-data"
PAYLOAD = ROOT / "payload"
VOICES = PAYLOAD / "voices"
CARDS = ROOT / "licenses" / "karty"


def log(message: str) -> None:
    encoding = getattr(sys.stdout, "encoding", None) or "utf-8"
    print(message.encode(encoding, "backslashreplace").decode(encoding))


def download(url: str, dest: Path, expected: int | None = None) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() and dest.stat().st_size > 0 and (expected is None or dest.stat().st_size == expected):
        log("already have " + dest.name)
        return
    if dest.exists():
        dest.unlink()
    quoted = urllib.parse.quote(url, safe=":/?&=#%")
    last_error: Exception | None = None
    for attempt in range(5):
        log("downloading " + dest.name)
        try:
            request = urllib.request.Request(quoted, headers={"User-Agent": "PiperSAPI5"})
            with urllib.request.urlopen(request, timeout=300) as response:
                data = response.read()
            if expected is not None and len(data) != expected:
                raise OSError(f"size {len(data)} != {expected}")
            if not data:
                raise OSError("empty download")
            dest.write_bytes(data)
            return
        except Exception as exc:
            last_error = exc
            log(f"retry {attempt + 1} {dest.name} {exc}")
            if dest.exists():
                dest.unlink()
            time.sleep(2 * (attempt + 1))
    raise SystemExit(f"Could not download {url}: {last_error}")


def source_files(voice: Voice) -> tuple[Path, Path, Path | None]:
    folder = THIRD / "voices" / voice.voice_id
    folder.mkdir(parents=True, exist_ok=True)
    local_onnx = THIRD / f"{voice.voice_id}.onnx"
    local_json = THIRD / f"{voice.voice_id}.onnx.json"
    onnx_name = f"{voice.source_key}.onnx"
    json_name = f"{voice.source_key}.onnx.json"
    card_name = "MODEL_CARD"
    onnx_rel = next(name for name in voice.files if name.endswith(".onnx") and not name.endswith(".onnx.json"))
    json_rel = next(name for name in voice.files if name.endswith(".onnx.json"))
    card_rel = next((name for name in voice.files if name.endswith("MODEL_CARD")), None)
    if local_onnx.exists() and local_json.exists():
        card = folder / card_name
        if card_rel and not card.exists():
            download(HF + card_rel, card)
        return local_onnx, local_json, card if card.exists() else None
    onnx_path = folder / Path(onnx_name).name
    json_path = folder / Path(json_name).name
    card_path = folder / card_name
    download(HF + onnx_rel, onnx_path, voice.files[onnx_rel].get("size_bytes"))
    download(HF + json_rel, json_path, voice.files[json_rel].get("size_bytes"))
    if card_rel:
        download(HF + card_rel, card_path, voice.files[card_rel].get("size_bytes"))
    return onnx_path, json_path, card_path if card_path.exists() else None


def generate_tokens(config: dict, dest: Path) -> None:
    id_map = config["phoneme_id_map"]
    lines: list[str] = []
    seen_symbols: set[str] = set()
    for symbol, value in id_map.items():
        if symbol == "\n" or symbol in seen_symbols:
            continue
        phoneme_id = value[0] if isinstance(value, list) else int(value)
        seen_symbols.add(symbol)
        lines.append(f"{symbol} {phoneme_id}\n")
    dest.write_bytes("".join(lines).encode("utf-8"))


def add_metadata(src_onnx: Path, dest_onnx: Path, config: dict, voice: Voice) -> None:
    sample_rate = int(config["audio"]["sample_rate"])
    if sample_rate == 22500:
        sample_rate = 22050
    meta = {
        "model_type": "vits",
        "comment": "piper",
        "language": voice.language_name,
        "voice": config["espeak"]["voice"],
        "version": "1",
        "has_espeak": "1",
        "has_g2pw": "0",
        "n_speakers": str(config["num_speakers"]),
        "sample_rate": str(sample_rate),
    }
    if dest_onnx.exists() and dest_onnx.stat().st_size > 0:
        log("model already prepared " + dest_onnx.parent.name)
        return
    log("loading " + src_onnx.name)
    model = onnx.load(str(src_onnx))
    while len(model.metadata_props):
        model.metadata_props.pop()
    for key, value in meta.items():
        prop = model.metadata_props.add()
        prop.key = key
        prop.value = str(value)
    dest_onnx.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, str(dest_onnx))


def write_voice_txt(dest: Path, config: dict, voice: Voice) -> None:
    sample_rate = int(config["audio"]["sample_rate"])
    if sample_rate == 22500:
        sample_rate = 22050
    inference = config["inference"]
    settings = (
        f"name={voice.display_name}\n"
        f"gender={voice.gender}\n"
        f"sample_rate={sample_rate}\n"
        f"language={voice.language_name}\n"
        f"langid={voice.lang_id}\n"
        f"locale={voice.locale}\n"
        f"noise_scale={inference['noise_scale']}\n"
        f"noise_scale_w={inference['noise_w']}\n"
        f"length_scale={inference['length_scale']}\n"
    )
    dest.write_text(settings, encoding="utf-8", newline="\n")


def prepare_voice(voice: Voice) -> None:
    dest = VOICES / voice.voice_id
    dest.mkdir(parents=True, exist_ok=True)
    onnx_path, config_path, card_path = source_files(voice)
    config = json.loads(config_path.read_text(encoding="utf-8"))
    if not (dest / "tokens.txt").exists():
        generate_tokens(config, dest / "tokens.txt")
    write_voice_txt(dest / "voice.txt", config, voice)
    add_metadata(onnx_path, dest / "model.onnx", config, voice)
    if card_path and card_path.exists():
        CARDS.mkdir(parents=True, exist_ok=True)
        target = CARDS / f"{voice.voice_id}.txt"
        shutil.copyfile(card_path, target)
    log("voice ready " + voice.voice_id)


def copy_runtime() -> None:
    dlls = [
        "sherpa-onnx-c-api.dll",
        "onnxruntime.dll",
        "onnxruntime_providers_shared.dll",
    ]
    PAYLOAD.mkdir(parents=True, exist_ok=True)
    for name in dlls:
        src = SHERPA_LIB / name
        if not src.exists():
            raise SystemExit(f"Missing {src}")
        shutil.copy2(src, PAYLOAD / name)
    runtime = PAYLOAD / "espeak-ng-data"
    if not runtime.exists():
        shutil.copytree(ESPEAK, runtime)
    (PAYLOAD / "engine.version").write_text("1.4.0\n", encoding="utf-8", newline="\n")


def selected_voices(language: str | None, all_languages: bool) -> list[Voice]:
    voices = load_voices()
    if all_languages:
        return voices
    if not language:
        language = "pl_PL"
    wanted = {voice.voice_id for pack in find_pack(language) for voice in pack.voices}
    return [voice for voice in voices if voice.voice_id in wanted]


def main() -> None:
    parser = argparse.ArgumentParser(description="Prepare Piper voice files for the SAPI engine.")
    parser.add_argument("--language", default="pl_PL", help="Locale code, for example pl_PL or de_DE")
    parser.add_argument("--all", action="store_true", help="Prepare every language in the Piper catalog")
    args = parser.parse_args()
    copy_runtime()
    for voice in selected_voices(args.language, args.all):
        prepare_voice(voice)
    log("payload ready: " + str(PAYLOAD))


if __name__ == "__main__":
    main()
