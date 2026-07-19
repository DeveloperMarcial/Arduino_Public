#!/usr/bin/env python3
"""Upload ESP32 flash images to the Portenta relay and trigger flashing."""

from __future__ import annotations

import argparse
import base64
import importlib.util
import hashlib
import json
import re
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable
from urllib.parse import quote

import requests


DEFAULT_TIMEOUT      = 10
FLASH_TIMEOUT        = 120
DEFAULT_CHUNK_SIZE   = 1024
DEFAULT_TARGET       = "arduino_nano_esp32"
REPO_ROOT            = Path(__file__).resolve().parents[1]
STUB_TEXT_IMAGE_NAME = "__stub_text.bin"
STUB_DATA_IMAGE_NAME = "__stub_data.bin"
SESSION_ID_PATTERN   = re.compile(r"sess-[0-9a-fA-F]{8}")


@dataclass(frozen=True)
class TargetConfig:
    manifest_target: str
    stub_name: str
    flash_size: int
    fixed_offsets: dict[str, int]
    partition_csv: Path | None = None


@dataclass(frozen=True)
class UploadImage:
    name: str
    offset: int
    data: bytes
    flash: bool = True

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.data).hexdigest()


class HttpJsonError(RuntimeError):
    def __init__(self, method: str, url: str, response: requests.Response) -> None:
        self.method = method
        self.url = url
        self.response = response
        super().__init__(self._format_message())

    def _format_message(self) -> str:
        message = f"{self.method} {self.url} failed: HTTP {self.response.status_code}"
        try:
            payload = self.response.json()
        except ValueError:
            text = self.response.text.strip()
            if text:
                return f"{message}: {text}"
            return message

        detail = payload.get("detail") or payload.get("error")
        if detail:
            return f"{message}: {detail}"
        return f"{message}: {json.dumps(payload, sort_keys=True)}"


def parse_int(value: int | str) -> int:
    if isinstance(value, int):
        return value
    return int(value, 0)


def normalize_session_id(value: object) -> str:
    text = str(value)
    match = SESSION_ID_PATTERN.search(text)
    if match is None:
        raise SystemExit(f"invalid session id received: {text!r}")
    return match.group(0).lower()


TARGETS: dict[str, TargetConfig] = {
    "esp32": TargetConfig(
        manifest_target="esp32",
        stub_name="esp32",
        flash_size=4 * 1024 * 1024,
        fixed_offsets={
            "bootloader": 0x1000,
            "partitions": 0x8000,
            "partition_table": 0x8000,
            "app": 0x10000,
            "app0": 0x10000,
        },
    ),
    "esp32s3": TargetConfig(
        manifest_target="esp32s3",
        stub_name="esp32s3",
        flash_size=16 * 1024 * 1024,
        fixed_offsets={
            "bootloader": 0x0,
            "partitions": 0x8000,
            "partition_table": 0x8000,
            "app": 0x10000,
        },
        partition_csv=REPO_ROOT / "partitions_arduino_nano_esp32.csv",
    ),
    "arduino_nano_esp32": TargetConfig(
        manifest_target="esp32s3",
        stub_name="esp32s3",
        flash_size=16 * 1024 * 1024,
        fixed_offsets={
            "bootloader": 0x0,
            "partitions": 0x8000,
            "partition_table": 0x8000,
            "app": 0x10000,
        },
        partition_csv=REPO_ROOT / "partitions_arduino_nano_esp32.csv",
    ),
}

TARGET_ALIASES = {
    "arduino-nano-esp32": "arduino_nano_esp32",
    "nano_esp32": "arduino_nano_esp32",
    "nano-esp32": "arduino_nano_esp32",
}


def normalize_target(target: str) -> str:
    normalized = target.strip().lower()
    normalized = TARGET_ALIASES.get(normalized, normalized)
    if normalized not in TARGETS:
        valid_targets = ", ".join(sorted(TARGETS))
        raise SystemExit(f"unknown target {target!r}; expected one of: {valid_targets}")
    return normalized


def parse_partition_offsets(csv_path: Path | None) -> dict[str, int]:
    if csv_path is None:
        return {}
    if not csv_path.exists():
        raise SystemExit(f"target partition CSV not found: {csv_path}")

    offsets: dict[str, int] = {}
    for raw_line in csv_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) < 5:
            continue
        name, offset_text = fields[0], fields[3]
        if not name or not offset_text:
            continue
        offsets[name] = parse_int(offset_text)
    return offsets


def target_offsets(config: TargetConfig) -> dict[str, int]:
    offsets = dict(config.fixed_offsets)
    offsets.update(parse_partition_offsets(config.partition_csv))
    if "app" not in offsets and "app0" in offsets:
        offsets["app"] = offsets["app0"]
    return offsets


def parse_image_offset(offset_text: str, offsets: dict[str, int]) -> int:
    try:
        return int(offset_text, 0)
    except ValueError:
        pass

    offset_key = offset_text.strip().lower()
    if offset_key in offsets:
        return offsets[offset_key]

    valid_names = ", ".join(sorted(offsets))
    raise SystemExit(
        f"unknown image offset/partition {offset_text!r}; use a numeric offset or one of: {valid_names}"
    )


def parse_image_specs(image_args: Iterable[str], config: TargetConfig) -> list[UploadImage]:
    images: list[UploadImage] = []
    offsets = target_offsets(config)
    for item in image_args:
        offset_text, file_name = item.split(":", 1)
        image_path = Path(file_name).resolve()
        images.append(
            UploadImage(
                name=image_path.name,
                offset=parse_image_offset(offset_text, offsets),
                data=image_path.read_bytes(),
                flash=True,
            )
        )
    return images


def find_stub_json_path(stub_name: str) -> Path | None:
    spec = importlib.util.find_spec("esptool")
    if spec is None or spec.origin is None:
        return None

    package_root = Path(spec.origin).resolve().parent
    candidates = [
        package_root / "targets" / "stub_flasher" / "2" / f"{stub_name}.json",
        package_root / "targets" / "stub_flasher" / "1" / f"{stub_name}.json",
    ]

    for candidate in candidates:
        if candidate.exists():
            return candidate

    return None


def decode_stub_blob(value: str | list[int]) -> bytes:
    if isinstance(value, list):
        return bytes(value)
    return base64.b64decode(value)


def build_stub_images(config: TargetConfig) -> tuple[list[UploadImage], dict]:
    stub_json_path = find_stub_json_path(config.stub_name)
    if stub_json_path is None:
        raise SystemExit(
            f"--erase requires a local esptool install with the {config.stub_name} stub flasher files. "
            "Install it with: python -m pip install esptool"
        )

    stub = json.loads(stub_json_path.read_text(encoding="utf-8"))
    text_data = decode_stub_blob(stub["text"])
    data_data = decode_stub_blob(stub["data"])
    text_start = parse_int(stub["text_start"])
    data_start = parse_int(stub["data_start"])
    entry = parse_int(stub["entry"])

    stub_images = [
        UploadImage(
            name=STUB_TEXT_IMAGE_NAME,
            offset=text_start,
            data=text_data,
            flash=False,
        ),
        UploadImage(
            name=STUB_DATA_IMAGE_NAME,
            offset=data_start,
            data=data_data,
            flash=False,
        ),
    ]
    stub_manifest = {
        "enabled": True,
        "text_image": STUB_TEXT_IMAGE_NAME,
        "text_start": text_start,
        "text_size": len(text_data),
        "data_image": STUB_DATA_IMAGE_NAME,
        "data_start": data_start,
        "data_size": len(data_data),
        "entry": entry,
    }
    return stub_images, stub_manifest


def build_upload_images(args: argparse.Namespace, config: TargetConfig) -> tuple[list[UploadImage], dict | None]:
    images = parse_image_specs(args.image, config)
    if not args.erase:
        return images, None

    reserved_names = {image.name for image in images}
    if {STUB_TEXT_IMAGE_NAME, STUB_DATA_IMAGE_NAME} & reserved_names:
        raise SystemExit(
            f"image names {STUB_TEXT_IMAGE_NAME} and {STUB_DATA_IMAGE_NAME} are reserved for erase mode"
        )

    stub_images, stub_manifest = build_stub_images(config)
    return images + stub_images, stub_manifest


def build_manifest(args: argparse.Namespace, config: TargetConfig, images: list[UploadImage], stub: dict | None) -> dict:
    manifest = {
        "target": config.manifest_target,
        "flash_size": config.flash_size,
        "baud": args.baud,
        "chunk_size": args.chunk_size,
        "erase": args.erase,
        "images": [
            {
                "name": image.name,
                "sha256": image.sha256,
                "offset": image.offset,
                "size": len(image.data),
                "flash": image.flash,
            }
            for image in images
        ],
    }
    if stub is not None:
        manifest["stub"] = stub
    return manifest


def request_json(method: str, url: str, *, timeout: float = DEFAULT_TIMEOUT, **kwargs) -> dict:
    response = requests.request(method, url, timeout=timeout, **kwargs)
    if not response.ok:
        raise HttpJsonError(method, url, response)
    return response.json()


def raise_for_response(method: str, url: str, response: requests.Response) -> None:
    if not response.ok:
        raise HttpJsonError(method, url, response)


def create_session(base_url: str) -> str:
    data = request_json("POST", f"{base_url}/api/v1/session")
    return normalize_session_id(data["session_id"])


def print_session(session_id: str, *, existing: bool = False) -> None:
    suffix = " (existing)" if existing else ""
    print(f"session: {normalize_session_id(session_id)}{suffix}", flush=True)


def upload_manifest(base_url: str, session_id: str, manifest: dict) -> None:
    request_json(
        "POST",
        f"{base_url}/api/v1/session/{session_id}/manifest",
        json=manifest,
    )


def upload_image_chunks(base_url: str, session_id: str, image: UploadImage, chunk_size: int) -> None:
    chunk_index = 0
    offset = 0
    while offset < len(image.data):
        chunk = image.data[offset : offset + chunk_size]
        url = (
            f"{base_url}/api/v1/session/{session_id}/chunk/"
            f"{quote(image.name, safe='')}/{chunk_index}"
        )
        response = requests.post(url, data=chunk, timeout=DEFAULT_TIMEOUT)
        raise_for_response("POST", url, response)
        offset += len(chunk)
        chunk_index += 1


def upload_images(base_url: str, session_id: str, images: list[UploadImage], chunk_size: int) -> None:
    for image in images:
        upload_image_chunks(base_url, session_id, image, chunk_size)


def fetch_chunk_map(base_url: str, session_id: str, image_name: str) -> dict:
    return request_json(
        "GET",
        f"{base_url}/api/v1/session/{session_id}/chunks/{quote(image_name, safe='')}",
    )


def print_known_sessions(base_url: str) -> None:
    try:
        payload = request_json("GET", f"{base_url}/api/v1/sessions")
    except (HttpJsonError, requests.RequestException) as error:
        print(f"Known session IDs unavailable: {error}", flush=True)
        return

    sessions = payload.get("sessions", [])
    if not isinstance(sessions, list):
        sessions = []
    if not sessions:
        session_ids = payload.get("session_ids", [])
        if isinstance(session_ids, list):
            sessions = [{"session_id": session_id} for session_id in session_ids]

    print("Known session IDs:", flush=True)
    if not sessions:
        print("  (none)", flush=True)
        return

    for session in sessions:
        if not isinstance(session, dict) or not session.get("session_id"):
            continue
        state = session.get("state")
        suffix = f" state={state}" if state else ""
        print(f"  {session['session_id']}{suffix}", flush=True)


def validate_resume_session(base_url: str, session_id: str, manifest: dict) -> None:
    try:
        status = poll_status(base_url, session_id)
    except HttpJsonError as error:
        if error.response.status_code == 404:
            print(f"Requested resume session was not found: {session_id}", flush=True)
            print_known_sessions(base_url)
        raise
    if status.get("target") != manifest["target"]:
        raise SystemExit(
            f"resume target mismatch: server has {status.get('target')}, "
            f"local manifest has {manifest['target']}"
        )

    if status.get("flash_size") != manifest["flash_size"]:
        raise SystemExit(
            f"resume flash size mismatch: server has {status.get('flash_size')}, "
            f"local manifest has {manifest['flash_size']}"
        )

    if status["chunk_size"] != manifest["chunk_size"]:
        raise SystemExit(
            f"resume chunk size mismatch: server has {status['chunk_size']}, "
            f"local manifest has {manifest['chunk_size']}"
        )

    if status.get("baud") != manifest["baud"]:
        raise SystemExit(
            f"resume baud mismatch: server has {status.get('baud')}, "
            f"local manifest has {manifest['baud']}"
        )

    if bool(status.get("erase")) != bool(manifest["erase"]):
        raise SystemExit(
            f"resume erase mismatch: server has {status.get('erase')}, "
            f"local manifest has {manifest['erase']}"
        )

    server_images = {
        image["name"]: (
            int(image["offset"]),
            int(image["size"]),
            str(image["sha256"]),
            bool(image.get("flash", True)),
        )
        for image in status["images"]
    }
    local_images = {
        image["name"]: (
            int(image["offset"]),
            int(image["size"]),
            str(image["sha256"]),
            bool(image.get("flash", True)),
        )
        for image in manifest["images"]
    }

    if server_images != local_images:
        raise SystemExit(
            "resume manifest mismatch: the existing session does not match the local image set"
        )


def upload_images_resumable(base_url: str, session_id: str, images: list[UploadImage], chunk_size: int) -> None:
    for image in images:
        chunk_map = fetch_chunk_map(base_url, session_id, image.name)
        if chunk_map["chunk_size"] != chunk_size:
            raise SystemExit(
                f"chunk size mismatch for {image.name}: "
                f"server expects {chunk_map['chunk_size']}, client has {chunk_size}"
            )

        received = chunk_map["received"]
        for chunk_index, already_present in enumerate(received):
            if already_present:
                continue

            offset = chunk_index * chunk_size
            chunk = image.data[offset : offset + chunk_size]
            if not chunk:
                raise SystemExit(
                    f"missing local data for {image.name} chunk {chunk_index}"
                )

            url = (
                f"{base_url}/api/v1/session/{session_id}/chunk/"
                f"{quote(image.name, safe='')}/{chunk_index}"
            )
            response = requests.post(url, data=chunk, timeout=DEFAULT_TIMEOUT)
            raise_for_response("POST", url, response)


def trigger_flash(base_url: str, session_id: str) -> None:
    request_json("POST", f"{base_url}/api/v1/session/{session_id}/flash", timeout=FLASH_TIMEOUT)


def poll_status(base_url: str, session_id: str) -> dict:
    return request_json("GET", f"{base_url}/api/v1/session/{session_id}/status")


def print_flash_verification(status: dict) -> None:
    for image in status.get("images", []):
        if not image.get("flash", True):
            continue

        flash_md5 = image.get("flash_md5") or "n/a"
        staged_md5 = image.get("staged_md5") or "n/a"
        flash_verified = image.get("flash_verified")
        print(
            f"image={image['name']} flash_verified={flash_verified} "
            f"staged_md5={staged_md5} flash_md5={flash_md5}"
        )


def powershell_quote(value: object) -> str:
    return "'" + str(value).replace("'", "''") + "'"


def build_recovery_command(
    args: argparse.Namespace,
    *,
    session_id: str | None = None,
    resume: bool = False,
) -> str:
    lines = [
        "python tools/esp32_uploader.py `",
        f"  --host {powershell_quote(args.host)} `",
        f"  --port {args.port} `",
        f"  --target {powershell_quote(args.target)} `",
        f"  --baud {args.baud} `",
        f"  --chunk-size {args.chunk_size} `",
        f"  --poll-interval {args.poll_interval} `",
    ]
    if args.erase:
        lines.append("  --erase `")
    if session_id is not None:
        lines.append(f"  --session-id {normalize_session_id(session_id)} `")
    if resume:
        lines.append("  --resume `")
    for index, image_spec in enumerate(args.image):
        continuation = " `" if index + 1 < len(args.image) else ""
        lines.append(f"  --image {powershell_quote(image_spec)}{continuation}")
    return "\n".join(lines)


def build_resume_command(args: argparse.Namespace, session_id: str) -> str:
    return build_recovery_command(args, session_id=session_id, resume=True)


def print_connection_recovery(
    args: argparse.Namespace,
    session_id: str | None,
    error: requests.RequestException,
    *,
    resume: bool,
) -> None:
    print(f"\nPortenta connection lost: {error}", flush=True)
    if session_id is None:
        print(
            "No session ID was received. Once the Portenta is back online, retry:",
            flush=True,
        )
    elif resume:
        print("Once the Portenta is back online, resume with:", flush=True)
    else:
        print(
            "The manifest was not acknowledged. Once the Portenta is back online, retry with:",
            flush=True,
        )
    print(
        build_recovery_command(args, session_id=session_id, resume=resume),
        flush=True,
    )


def print_interrupted_recovery(
    args: argparse.Namespace,
    session_id: str,
    *,
    resume: bool,
) -> None:
    active_session_id = normalize_session_id(session_id)
    print(f"\nUpload interrupted. Active session: {active_session_id}", flush=True)
    if resume:
        print("To continue this partial upload, run:", flush=True)
    else:
        print("The manifest was not acknowledged. To retry this session, run:", flush=True)
    print(
        build_recovery_command(
            args,
            session_id=active_session_id,
            resume=resume,
        ),
        flush=True,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="Portenta hostname or IP address")
    parser.add_argument("--port", type=int, default=8080, help="Portenta HTTP port")
    parser.add_argument("--baud", type=int, default=460800, help="ESP flash baud")
    parser.add_argument(
        "--target",
        default=DEFAULT_TARGET,
        help=(
            f"ESP target profile, default {DEFAULT_TARGET}. "
            "Use arduino_nano_esp32/esp32s3 for Arduino Nano ESP32, or esp32 for classic ESP32."
        ),
    )
    parser.add_argument(
        "--erase",
        action="store_true",
        help="Request full erase before writing; requires local esptool stub files",
    )
    parser.add_argument(
        "--image",
        action="append",
        required=True,
        metavar="OFFSET:FILE",
        help="Flash image spec, for example bootloader:bootloader.bin, partitions:partitions.bin, app0:firmware.bin, or 0x10000:firmware.bin",
    )
    parser.add_argument("--chunk-size", type=int, default=DEFAULT_CHUNK_SIZE)
    parser.add_argument("--poll-interval", type=float, default=0.5)
    parser.add_argument("--manifest-out", type=Path, help="Optional path to write the generated manifest JSON")
    parser.add_argument("--session-id", help="Reuse an existing session instead of creating a new one")
    parser.add_argument("--resume", action="store_true", help="Resume an existing session by uploading only missing chunks")
    args = parser.parse_args()

    target_name = normalize_target(args.target)
    target_config = TARGETS[target_name]
    base_url = f"http://{args.host}:{args.port}"
    upload_images_list, stub_manifest = build_upload_images(args, target_config)
    manifest = build_manifest(args, target_config, upload_images_list, stub_manifest)

    if args.manifest_out is not None:
        args.manifest_out.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    if args.resume and not args.session_id:
        raise SystemExit("--resume requires --session-id")

    session_id: str | None = None
    if args.session_id:
        session_id = normalize_session_id(args.session_id)
        print_session(session_id, existing=True)
    else:
        try:
            session_id = create_session(base_url)
        except requests.RequestException as error:
            print_connection_recovery(args, None, error, resume=False)
            raise
        print_session(session_id)

    recovery_uses_resume = bool(args.resume)
    try:
        if args.session_id and args.resume:
            validate_resume_session(base_url, session_id, manifest)
            print("existing session matches local manifest")
        else:
            manifest_started = time.perf_counter()
            print("manifest upload started...", flush=True)
            upload_manifest(base_url, session_id, manifest)
            print(
                f"manifest upload finished: {time.perf_counter() - manifest_started:.2f} seconds",
                flush=True,
            )
            recovery_uses_resume = True

        chunks_started = time.perf_counter()
        print("image chunk upload started...", flush=True)
        if args.resume:
            upload_images_resumable(base_url, session_id, upload_images_list, args.chunk_size)
        else:
            upload_images(base_url, session_id, upload_images_list, args.chunk_size)
        print(
            f"image chunk upload finished: {time.perf_counter() - chunks_started:.2f} seconds",
            flush=True,
        )

        flash_started = time.perf_counter()
        print("flash started...", flush=True)
        try:
            trigger_flash(base_url, session_id)
        except requests.Timeout:
            status = poll_status(base_url, session_id)
            state = status.get("state")
            detail = status.get("detail")
            if state not in {"flashing", "completed", "failed"}:
                raise SystemExit(
                    f"flash request timed out; state={state} detail={detail}"
                ) from None
            print(
                f"flash request timed out; continuing to poll state={state} detail={detail}"
            )

        while True:
            status = poll_status(base_url, session_id)
            print(
                f"state={status['state']} progress={status['progress']} detail={status['detail']}"
            )
            if status["state"] in {"completed", "failed"}:
                print_flash_verification(status)
                print(
                    f"flash finished: {time.perf_counter() - flash_started:.2f} seconds",
                    flush=True,
                )
                if status["state"] != "completed":
                    raise SystemExit(1)
                break
            time.sleep(args.poll_interval)
    except KeyboardInterrupt:
        print_interrupted_recovery(
            args,
            session_id,
            resume=recovery_uses_resume,
        )
        raise SystemExit(130) from None
    except requests.RequestException as error:
        print_connection_recovery(
            args,
            session_id,
            error,
            resume=recovery_uses_resume,
        )
        raise


if __name__ == "__main__":
    try:
        main()
    except HttpJsonError as error:
        raise SystemExit(str(error)) from None
    except requests.RequestException as error:
        raise SystemExit(f"HTTP request failed: {error}") from None
