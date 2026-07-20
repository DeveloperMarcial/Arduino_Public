"""Generate all uploader narration WAVs with Piper's LibriTTS high model."""

from __future__ import annotations

import argparse
import ast
import hashlib
import re
import urllib.request
import wave
from pathlib import Path

from piper import PiperVoice, SynthesisConfig


NARRATION_DIR = Path(__file__).resolve().parent
TEXT_PATH = NARRATION_DIR / "VoiceText.txt"
MODEL_PATH = NARRATION_DIR / "en_US-libritts-high.onnx"
CONFIG_PATH = NARRATION_DIR / "en_US-libritts-high.onnx.json"

MODEL_URL = (
    "https://huggingface.co/rhasspy/piper-voices/resolve/main/"
    "en/en_US/libritts/high/en_US-libritts-high.onnx?download=true"
)
CONFIG_URL = (
    "https://huggingface.co/rhasspy/piper-voices/resolve/main/"
    "en/en_US/libritts/high/en_US-libritts-high.onnx.json?download=true"
)
MODEL_SHA256 = "9127a559e11603f10b366d1a20ac7426826081dbc521de4c2420c57728d73f0f"
CONFIG_SHA256 = "2efdc6d7f954588b8180132cbd9b8001933fdd00932c92bc92fd0d2028a9eb3d"

DEFAULT_SPEAKER_NAME = "p3922"
DEFAULT_PREFIX = ""
OUTPUT_FILES = {
    "0": "Voice00_codex_and_gpt.wav",
    "1": "Voice01_setup_and_command.wav",
    "2": "Voice02_uploader_and_staging.wav",
    "2a": "Voice02a_chunk_resume_and_verification.wav",
    "3": "Voice03_start_flash.wav",
    "4": "Voice04_flash_done..wav",
}
HEADER_PATTERN = re.compile(r"^#VOICE\s+#(0|1|2a|2|3|4)\s*$", re.IGNORECASE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate uploader narration with Piper's LibriTTS high model."
    )
    parser.add_argument(
        "--speaker",
        default=DEFAULT_SPEAKER_NAME,
        help=f"speaker key from the model config (default: {DEFAULT_SPEAKER_NAME})",
    )
    parser.add_argument(
        "--prefix",
        default=DEFAULT_PREFIX,
        help="text to prepend to each output WAV filename",
    )
    parser.add_argument(
        "--section",
        choices=OUTPUT_FILES,
        help="generate only this VoiceText.txt section (default: generate all)",
    )
    parser.add_argument(
        "--noise-scale",
        type=float,
        help="voice variation; lower values produce flatter delivery",
    )
    parser.add_argument(
        "--noise-w-scale",
        type=float,
        help="phoneme-duration variation; lower values produce steadier timing",
    )
    parser.add_argument(
        "--length-scale",
        type=float,
        default=1.0,
        help="speaking duration multiplier (default: 1.0)",
    )
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ensure_download(path: Path, url: str, expected_sha256: str) -> None:
    if path.is_file() and sha256(path) == expected_sha256:
        return

    temporary_path = path.with_suffix(path.suffix + ".part")
    print(f"Downloading: {path.name}")
    urllib.request.urlretrieve(url, temporary_path)
    actual_sha256 = sha256(temporary_path)
    if actual_sha256 != expected_sha256:
        temporary_path.unlink(missing_ok=True)
        raise RuntimeError(
            f"SHA-256 mismatch for {path.name}: "
            f"expected {expected_sha256}, received {actual_sha256}"
        )
    temporary_path.replace(path)


def load_voice_text(path: Path) -> dict[str, str]:
    sections: dict[str, str] = {}
    current_key: str | None = None
    current_lines: list[str] = []

    def save_section() -> None:
        if current_key is None:
            return
        source = " ".join(line.strip() for line in current_lines if line.strip())
        if not source:
            raise ValueError(f"VOICE #{current_key} has no narration text")
        value = ast.literal_eval(source)
        if not isinstance(value, str):
            raise ValueError(f"VOICE #{current_key} must contain a quoted string")
        sections[current_key] = value

    for line in path.read_text(encoding="utf-8-sig").splitlines():
        match = HEADER_PATTERN.match(line.strip())
        if match:
            save_section()
            current_key = match.group(1).lower()
            current_lines = []
        elif current_key is not None:
            current_lines.append(line)
    save_section()

    missing = set(OUTPUT_FILES) - set(sections)
    if missing:
        raise ValueError(f"VoiceText.txt is missing sections: {sorted(missing)}")
    return sections


def wav_duration(path: Path) -> float:
    with wave.open(str(path), "rb") as wav_file:
        return wav_file.getnframes() / wav_file.getframerate()


def main() -> None:
    args = parse_args()
    ensure_download(MODEL_PATH, MODEL_URL, MODEL_SHA256)
    ensure_download(CONFIG_PATH, CONFIG_URL, CONFIG_SHA256)
    sections = load_voice_text(TEXT_PATH)
    voice = PiperVoice.load(MODEL_PATH, config_path=CONFIG_PATH)
    try:
        speaker_id = voice.config.speaker_id_map[args.speaker]
    except KeyError as error:
        raise ValueError(f"Unknown LibriTTS speaker: {args.speaker}") from error
    config = SynthesisConfig(
        speaker_id=speaker_id,
        length_scale=args.length_scale,
        noise_scale=args.noise_scale,
        noise_w_scale=args.noise_w_scale,
    )

    print(f"Using speaker: {args.speaker} (ID {speaker_id})")
    output_files = OUTPUT_FILES.items()
    if args.section:
        output_files = ((args.section, OUTPUT_FILES[args.section]),)
    for section, filename in output_files:
        output_path = NARRATION_DIR / f"{args.prefix}{filename}"
        with wave.open(str(output_path), "wb") as wav_file:
            voice.synthesize_wav(
                sections[section],
                wav_file,
                syn_config=config,
            )
        print(
            f"Created: {output_path.name} "
            f"({wav_duration(output_path):.2f} seconds)"
        )


if __name__ == "__main__":
    main()
