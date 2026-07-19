#!/usr/bin/env python3
"""Judge-friendly HTTP simulator for the Portenta H7 ESP32 programmer."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import sys
import tempfile
import threading
import time
import uuid
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlsplit


API_PREFIX = ("api", "v1", "session")
SESSION_RE = re.compile(r"^sess-[0-9a-f]{8}$")
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
MAX_IMAGES = 8
MAX_IMAGE_NAME = 32
MAX_CHUNK_SIZE = 1536
MAX_CHUNKS = 8192
MAX_FLASH_SIZE = 128 * 1024 * 1024


class ProtocolError(Exception):
    def __init__(self, status: int, message: str, *, fail_session: bool = False) -> None:
        super().__init__(message)
        self.status = status
        self.message = message
        self.fail_session = fail_session


def _atomic_json(path: Path, value: dict[str, Any]) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True), encoding="utf-8")
    os.replace(temporary, path)


def _digest(path: Path, algorithm: str, size: int) -> str:
    digest = hashlib.new(algorithm)
    remaining = size
    with path.open("rb") as handle:
        while remaining:
            block = handle.read(min(65536, remaining))
            if not block:
                raise OSError("staged file ended before its declared size")
            digest.update(block)
            remaining -= len(block)
    return digest.hexdigest()


def _safe_image_name(value: Any) -> str:
    if not isinstance(value, str) or not value or len(value) > MAX_IMAGE_NAME:
        raise ProtocolError(409, f"image name must contain 1-{MAX_IMAGE_NAME} characters")
    if value in {".", ".."} or "/" in value or "\\" in value or "\x00" in value:
        raise ProtocolError(409, "image name contains an unsafe path component")
    return value


def _integer(value: Any, field: str, *, minimum: int = 0, maximum: int = 0xFFFFFFFF) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise ProtocolError(409, f"{field} must be an integer from {minimum} to {maximum}")
    return value


@dataclass(frozen=True)
class SimulatorConfig:
    storage: Path
    flash_delay: float = 0.05
    progress_steps: int = 4
    fail_verification: str | None = None
    session_max_age: float = 7 * 24 * 60 * 60


class PortentaSimulator:
    """Persistent protocol and flash model shared by HTTP handler threads."""

    def __init__(self, config: SimulatorConfig) -> None:
        self.config = config
        self.storage = config.storage.resolve()
        self.sessions_dir = self.storage / "sessions"
        self.flash_dir = self.storage / "flash"
        self.lock = threading.RLock()
        self.sessions: dict[str, dict[str, Any]] = {}
        self.storage.mkdir(parents=True, exist_ok=True)
        self.sessions_dir.mkdir(exist_ok=True)
        self.flash_dir.mkdir(exist_ok=True)
        self._load_sessions()
        self.cleanup_expired()

    def _session_dir(self, session_id: str) -> Path:
        return self.sessions_dir / session_id

    def _state_path(self, session_id: str) -> Path:
        return self._session_dir(session_id) / "state.json"

    def _staged_path(self, session_id: str, image_name: str) -> Path:
        return self._session_dir(session_id) / "staged" / image_name

    def _flash_path(self, target: str) -> Path:
        safe_target = re.sub(r"[^a-zA-Z0-9_.-]", "_", target) or "esp32"
        return self.flash_dir / f"{safe_target}.bin"

    def _load_sessions(self) -> None:
        for state_path in self.sessions_dir.glob("sess-*/state.json"):
            try:
                state = json.loads(state_path.read_text(encoding="utf-8"))
                session_id = state["session_id"]
                if SESSION_RE.fullmatch(session_id):
                    if state.get("state") == "flashing":
                        state.update(
                            state="failed",
                            progress=0,
                            detail="Simulation restarted during flash; upload remains staged",
                        )
                        _atomic_json(state_path, state)
                    self.sessions[session_id] = state
            except (OSError, ValueError, KeyError, TypeError):
                print(f"SIMULATION WARNING: ignored invalid session metadata: {state_path}", file=sys.stderr)

    def cleanup_expired(self) -> int:
        if self.config.session_max_age <= 0:
            return 0
        cutoff = time.time() - self.config.session_max_age
        removed = 0
        with self.lock:
            for session_id, state in list(self.sessions.items()):
                if float(state.get("updated_at", 0)) >= cutoff or state.get("state") == "flashing":
                    continue
                session_dir = self._session_dir(session_id).resolve()
                if session_dir.parent == self.sessions_dir:
                    shutil.rmtree(session_dir)
                    del self.sessions[session_id]
                    removed += 1
        return removed

    def _save(self, state: dict[str, Any]) -> None:
        state["updated_at"] = time.time()
        _atomic_json(self._state_path(state["session_id"]), state)

    def _get(self, session_id: str) -> dict[str, Any]:
        if not SESSION_RE.fullmatch(session_id) or session_id not in self.sessions:
            raise ProtocolError(404, "Unknown session")
        return self.sessions[session_id]

    def create_session(self) -> dict[str, Any]:
        with self.lock:
            while True:
                session_id = f"sess-{uuid.uuid4().hex[:8]}"
                if session_id not in self.sessions:
                    break
            session_dir = self._session_dir(session_id)
            (session_dir / "staged").mkdir(parents=True)
            state = {
                "session_id": session_id,
                "state": "created",
                "progress": 0,
                "detail": "SIMULATION: session created",
                "created_at": time.time(),
                "updated_at": time.time(),
                "manifest": None,
                "images": [],
            }
            self.sessions[session_id] = state
            self._save(state)
            return self._brief(state)

    def apply_manifest(self, session_id: str, payload: Any) -> dict[str, Any]:
        with self.lock:
            state = self._get(session_id)
            if not isinstance(payload, dict):
                raise ProtocolError(400, "Manifest JSON must be an object")
            if state["state"] == "flashing":
                raise ProtocolError(409, "Cannot replace a manifest while flashing")

            target = payload.get("target", "esp32")
            if not isinstance(target, str) or not target or len(target) > 15:
                raise ProtocolError(409, "target must contain 1-15 characters")
            flash_size = _integer(payload.get("flash_size", 0), "flash_size", maximum=MAX_FLASH_SIZE)
            baud = _integer(payload.get("baud", 460800), "baud", minimum=1)
            chunk_size = _integer(
                payload.get("chunk_size", 1024),
                "chunk_size",
                minimum=1,
                maximum=MAX_CHUNK_SIZE,
            )
            erase = payload.get("erase", False)
            if not isinstance(erase, bool):
                raise ProtocolError(409, "erase must be a boolean")
            source_images = payload.get("images")
            if not isinstance(source_images, list) or not source_images:
                raise ProtocolError(409, "Manifest contained no images")
            if len(source_images) > MAX_IMAGES:
                raise ProtocolError(409, "Too many images in manifest")

            images: list[dict[str, Any]] = []
            names: set[str] = set()
            flashable = 0
            for source in source_images:
                if not isinstance(source, dict):
                    raise ProtocolError(409, "each manifest image must be an object")
                name = _safe_image_name(source.get("name"))
                if name in names:
                    raise ProtocolError(409, f"duplicate image name: {name}")
                names.add(name)
                sha256 = source.get("sha256")
                if not isinstance(sha256, str) or not SHA256_RE.fullmatch(sha256):
                    raise ProtocolError(409, f"image {name} has an invalid sha256")
                size = _integer(source.get("size"), f"{name}.size", minimum=1)
                offset = _integer(source.get("offset", 0), f"{name}.offset")
                should_flash = source.get("flash", True)
                if not isinstance(should_flash, bool):
                    raise ProtocolError(409, f"{name}.flash must be a boolean")
                if should_flash and flash_size and offset + size > flash_size:
                    raise ProtocolError(409, f"image {name} exceeds declared flash_size")
                total_chunks = (size + chunk_size - 1) // chunk_size
                if total_chunks > MAX_CHUNKS:
                    raise ProtocolError(409, f"image {name} requires too many chunks")
                flashable += int(should_flash)
                images.append(
                    {
                        "name": name,
                        "sha256": sha256.lower(),
                        "offset": offset,
                        "size": size,
                        "flash": should_flash,
                        "received": [False] * total_chunks,
                        "received_bytes": 0,
                        "complete": False,
                        "verified": False,
                        "staged_md5": "",
                        "flash_md5": "",
                        "flash_verified": False,
                    }
                )
            if not flashable:
                raise ProtocolError(409, "Manifest contained no flashable images")
            stub = payload.get("stub")
            if erase and (not isinstance(stub, dict) or not stub.get("enabled")):
                raise ProtocolError(409, "erase=true requires stub metadata in manifest")

            for old_file in (self._session_dir(session_id) / "staged").iterdir():
                if old_file.is_file():
                    old_file.unlink()
            state.update(
                state="manifest_received",
                progress=5,
                detail="SIMULATION: manifest accepted",
                manifest={
                    "target": target,
                    "flash_size": flash_size,
                    "baud": baud,
                    "chunk_size": chunk_size,
                    "erase": erase,
                    "stub": stub,
                },
                images=images,
            )
            self._save(state)
            return self._brief(state)

    @staticmethod
    def _find_image(state: dict[str, Any], image_name: str) -> dict[str, Any]:
        for image in state["images"]:
            if image["name"] == image_name:
                return image
        raise ProtocolError(404, "Unknown session or image")

    def upload_chunk(
        self, session_id: str, image_name: str, chunk_index: int, data: bytes
    ) -> dict[str, Any]:
        with self.lock:
            state = self._get(session_id)
            if state["manifest"] is None:
                raise ProtocolError(409, "Session has no manifest")
            if state["state"] in {"flashing", "completed"}:
                raise ProtocolError(409, f"Session is already {state['state']}")
            image = self._find_image(state, image_name)
            received = image["received"]
            if chunk_index < 0 or chunk_index >= len(received):
                raise ProtocolError(409, "Chunk rejected: wrong index or size")
            chunk_size = state["manifest"]["chunk_size"]
            offset = chunk_index * chunk_size
            expected = min(chunk_size, image["size"] - offset)
            if len(data) != expected:
                raise ProtocolError(
                    409,
                    f"Chunk rejected: expected {expected} bytes at offset {offset}, received {len(data)}",
                )

            path = self._staged_path(session_id, image_name)
            mode = "r+b" if path.exists() else "w+b"
            with path.open(mode) as handle:
                handle.seek(offset)
                handle.write(data)
                handle.flush()
                os.fsync(handle.fileno())
            if not received[chunk_index]:
                received[chunk_index] = True
                image["received_bytes"] += len(data)
            image["complete"] = all(received)
            image["verified"] = False
            state.update(state="uploading", progress=25, detail="SIMULATION: chunk staged")

            if all(candidate["complete"] for candidate in state["images"]):
                for candidate in state["images"]:
                    path = self._staged_path(session_id, candidate["name"])
                    candidate["verified"] = (
                        _digest(path, "sha256", candidate["size"]) == candidate["sha256"]
                    )
                if all(candidate["verified"] for candidate in state["images"]):
                    state.update(
                        state="ready_to_flash",
                        progress=100,
                        detail="SIMULATION: all images uploaded and verified",
                    )
                else:
                    state.update(
                        state="failed",
                        progress=0,
                        detail="Staged image hash verification failed (SIMULATION)",
                    )
                    self._save(state)
                    raise ProtocolError(409, state["detail"])
            self._save(state)
            return self._brief(state)

    def chunk_map(self, session_id: str, image_name: str) -> dict[str, Any]:
        with self.lock:
            state = self._get(session_id)
            image = self._find_image(state, image_name)
            received = list(image["received"])
            return {
                "session_id": session_id,
                "image": image_name,
                "chunk_size": state["manifest"]["chunk_size"],
                "total_chunks": len(received),
                "received_chunks": sum(received),
                "received": received,
            }

    def start_flash(self, session_id: str) -> dict[str, Any]:
        with self.lock:
            state = self._get(session_id)
            if state["state"] != "ready_to_flash":
                state.update(
                    state="failed",
                    progress=0,
                    detail="Session is not ready to flash (SIMULATION)",
                )
                self._save(state)
                raise ProtocolError(409, state["detail"])
            state.update(state="flashing", progress=30, detail="SIMULATION: starting flash sequence")
            self._save(state)
            worker = threading.Thread(
                target=self._flash_worker,
                args=(session_id,),
                name=f"flash-{session_id}",
                daemon=True,
            )
            worker.start()
            return self._brief(state)

    def _flash_worker(self, session_id: str) -> None:
        try:
            with self.lock:
                state = self._get(session_id)
                manifest = dict(state["manifest"])
                images = [dict(image) for image in state["images"] if image["flash"]]
            flash_path = self._flash_path(manifest["target"])
            if not flash_path.exists():
                flash_path.touch()
            if manifest["erase"]:
                with flash_path.open("wb") as flash:
                    remaining = manifest["flash_size"]
                    erased = b"\xff" * 65536
                    while remaining:
                        flash.write(erased[: min(len(erased), remaining)])
                        remaining -= min(len(erased), remaining)

            total_steps = max(1, len(images) * 2)
            for index, image in enumerate(images):
                self._progress(
                    session_id,
                    30 + int(60 * (index * 2) / total_steps),
                    f"SIMULATION: flashing {image['name']} at 0x{image['offset']:x}",
                )
                self._delay()
                staged_path = self._staged_path(session_id, image["name"])
                with staged_path.open("rb") as source, flash_path.open("r+b") as flash:
                    flash.seek(image["offset"])
                    remaining = image["size"]
                    while remaining:
                        block = source.read(min(65536, remaining))
                        if not block:
                            raise OSError("staged image ended unexpectedly")
                        flash.write(block)
                        remaining -= len(block)
                    flash.flush()
                    os.fsync(flash.fileno())

                should_fail = self.config.fail_verification in {"*", image["name"]}
                if should_fail:
                    with flash_path.open("r+b") as flash:
                        flash.seek(image["offset"])
                        first = flash.read(1)
                        flash.seek(image["offset"])
                        flash.write(bytes([(first[0] if first else 0) ^ 0x01]))

                self._progress(
                    session_id,
                    35 + int(60 * (index * 2 + 1) / total_steps),
                    f"SIMULATION: verifying {image['name']} against ESP flash model",
                )
                self._delay()
                staged_md5 = _digest(staged_path, "md5", image["size"])
                flash_md5 = self._flash_digest(flash_path, image["offset"], image["size"])
                with self.lock:
                    current = self._find_image(self._get(session_id), image["name"])
                    current["staged_md5"] = staged_md5
                    current["flash_md5"] = flash_md5
                    current["flash_verified"] = staged_md5 == flash_md5
                    state = self._get(session_id)
                    if not current["flash_verified"]:
                        state.update(
                            state="failed",
                            progress=0,
                            detail="ESP flash verification mismatch (SIMULATION)",
                        )
                        self._save(state)
                        return
                    self._save(state)
            with self.lock:
                state = self._get(session_id)
                state.update(
                    state="completed",
                    progress=100,
                    detail="SIMULATION: flash and readback verification successful",
                )
                self._save(state)
        except Exception as error:
            with self.lock:
                state = self.sessions.get(session_id)
                if state is not None:
                    state.update(
                        state="failed",
                        progress=0,
                        detail=f"SIMULATION: flash model failed: {error}",
                    )
                    self._save(state)

    def _delay(self) -> None:
        steps = max(1, self.config.progress_steps)
        for _ in range(steps):
            if self.config.flash_delay > 0:
                time.sleep(self.config.flash_delay / steps)

    def _progress(self, session_id: str, progress: int, detail: str) -> None:
        with self.lock:
            state = self._get(session_id)
            state.update(progress=progress, detail=detail)
            self._save(state)

    @staticmethod
    def _flash_digest(path: Path, offset: int, size: int) -> str:
        digest = hashlib.md5()
        with path.open("rb") as handle:
            handle.seek(offset)
            remaining = size
            while remaining:
                block = handle.read(min(65536, remaining))
                if not block:
                    block = b"\x00" * min(65536, remaining)
                digest.update(block)
                remaining -= len(block)
        return digest.hexdigest()

    def status(self, session_id: str) -> dict[str, Any]:
        with self.lock:
            state = self._get(session_id)
            result = self._brief(state)
            manifest = state["manifest"] or {}
            result.update(
                target=manifest.get("target", ""),
                flash_size=manifest.get("flash_size", 0),
                baud=manifest.get("baud", 0),
                chunk_size=manifest.get("chunk_size", 0),
                erase=manifest.get("erase", False),
                images=[],
                simulation=True,
            )
            for source in state["images"]:
                image = {key: value for key, value in source.items() if key != "received"}
                image["received_chunks"] = sum(source["received"])
                image["total_chunks"] = len(source["received"])
                result["images"].append(image)
            return result

    def known_sessions(self) -> dict[str, Any]:
        with self.lock:
            sessions = []
            for state in sorted(
                self.sessions.values(),
                key=lambda item: float(item.get("updated_at", 0)),
                reverse=True,
            ):
                sessions.append(
                    {
                        **self._brief(state),
                        "created_at": state.get("created_at"),
                        "updated_at": state.get("updated_at"),
                    }
                )
            return {
                "simulation": True,
                "session_ids": [item["session_id"] for item in sessions],
                "sessions": sessions,
            }

    @staticmethod
    def _brief(state: dict[str, Any]) -> dict[str, Any]:
        return {
            "session_id": state["session_id"],
            "state": state["state"],
            "progress": state["progress"],
            "detail": state["detail"],
        }

    def device_info(self) -> dict[str, Any]:
        return {
            "simulation": True,
            "device": "Portenta H7 ESP32 Programmer Simulator",
            "hardware_present": False,
            "api_version": "v1",
            "persistent_storage": str(self.storage),
            "sessions": len(self.sessions),
        }


class SimulatorHttpServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address: tuple[str, int], simulator: PortentaSimulator) -> None:
        self.simulator = simulator
        super().__init__(address, SimulatorRequestHandler)


class SimulatorRequestHandler(BaseHTTPRequestHandler):
    server: SimulatorHttpServer
    protocol_version = "HTTP/1.1"

    def log_message(self, message: str, *args: Any) -> None:
        print(f"SIMULATION HTTP {self.address_string()} - {message % args}")

    def _send_json(self, status: int, payload: dict[str, Any]) -> None:
        data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def _send_text(self, status: int, text: str) -> None:
        data = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def _body(self) -> bytes:
        length_text = self.headers.get("Content-Length")
        if length_text is None:
            raise ProtocolError(411, "Chunk uploads require Content-Length")
        try:
            length = int(length_text)
        except ValueError:
            raise ProtocolError(400, "Invalid Content-Length")
        if length < 0 or length > MAX_CHUNK_SIZE * 4:
            raise ProtocolError(413, "Request body is too large")
        return self.rfile.read(length)

    def _segments(self) -> list[str]:
        path = urlsplit(self.path).path
        try:
            return [unquote(item, errors="strict") for item in path.split("/") if item]
        except UnicodeDecodeError as error:
            raise ProtocolError(400, "Invalid URL encoding") from error

    def do_GET(self) -> None:
        try:
            path = urlsplit(self.path).path
            if path == "/":
                self._send_text(
                    200,
                    "SIMULATION ONLY: Portenta ESP32 programmer simulator is online; no hardware was flashed.\n",
                )
                return
            if path in {"/health", "/api/v1/device"}:
                self._send_json(200, self.server.simulator.device_info())
                return
            segments = self._segments()
            if segments == ["api", "v1", "sessions"]:
                self._send_json(200, self.server.simulator.known_sessions())
                return
            if len(segments) == 5 and tuple(segments[:3]) == API_PREFIX and segments[4] == "status":
                self._send_json(200, self.server.simulator.status(segments[3]))
                return
            if len(segments) == 6 and tuple(segments[:3]) == API_PREFIX and segments[4] == "chunks":
                self._send_json(200, self.server.simulator.chunk_map(segments[3], segments[5]))
                return
            raise ProtocolError(404, "Unknown API route")
        except ProtocolError as error:
            self._send_json(error.status, {"error": error.message})

    def do_POST(self) -> None:
        try:
            segments = self._segments()
            if tuple(segments) == API_PREFIX:
                self._send_json(200, self.server.simulator.create_session())
                return
            if len(segments) == 5 and tuple(segments[:3]) == API_PREFIX:
                session_id = segments[3]
                if segments[4] == "manifest":
                    try:
                        payload = json.loads(self._body())
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        raise ProtocolError(400, "Manifest JSON parse failed") from None
                    self._send_json(200, self.server.simulator.apply_manifest(session_id, payload))
                    return
                if segments[4] == "flash":
                    self._send_json(200, self.server.simulator.start_flash(session_id))
                    return
            if len(segments) == 7 and tuple(segments[:3]) == API_PREFIX and segments[4] == "chunk":
                try:
                    chunk_index = int(segments[6], 10)
                except ValueError:
                    raise ProtocolError(400, "Chunk index must be an unsigned integer") from None
                if chunk_index < 0:
                    raise ProtocolError(400, "Chunk index must be an unsigned integer")
                result = self.server.simulator.upload_chunk(
                    segments[3], _safe_image_name(segments[5]), chunk_index, self._body()
                )
                self._send_json(200, result)
                return
            raise ProtocolError(404, "Unknown API route")
        except ProtocolError as error:
            payload: dict[str, Any] = {"error": error.message}
            segments = urlsplit(self.path).path.split("/")
            if error.status == 409 and len(segments) > 4:
                try:
                    with self.server.simulator.lock:
                        state = self.server.simulator._get(segments[4])
                        state.update(state="failed", progress=0, detail=error.message)
                        self.server.simulator._save(state)
                        payload = self.server.simulator._brief(state)
                except ProtocolError:
                    pass
            self._send_json(error.status, payload)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument(
        "--storage",
        type=Path,
        help="Persistent simulator directory; omitted uses temporary storage removed on exit",
    )
    parser.add_argument(
        "--flash-delay",
        type=float,
        default=0.15,
        help="Simulated delay per write and verify phase in seconds (default: 0.15)",
    )
    parser.add_argument("--progress-steps", type=int, default=4)
    parser.add_argument(
        "--fail-verification",
        metavar="IMAGE",
        help="Force readback failure for this image name, or * for every image",
    )
    parser.add_argument(
        "--session-max-age",
        type=float,
        default=7 * 24 * 60 * 60,
        help="Remove persisted sessions older than this many seconds; 0 disables cleanup",
    )
    return parser


def main() -> None:
    args = build_parser().parse_args()
    if not 0 <= args.port <= 65535:
        raise SystemExit("--port must be from 0 to 65535")
    if args.flash_delay < 0 or args.progress_steps < 1 or args.session_max_age < 0:
        raise SystemExit("delays/ages must be non-negative and progress steps must be positive")

    temporary: tempfile.TemporaryDirectory[str] | None = None
    if args.storage is None:
        temporary = tempfile.TemporaryDirectory(prefix="portenta-sim-")
        storage = Path(temporary.name)
    else:
        storage = args.storage
    simulator = PortentaSimulator(
        SimulatorConfig(
            storage=storage,
            flash_delay=args.flash_delay,
            progress_steps=args.progress_steps,
            fail_verification=args.fail_verification,
            session_max_age=args.session_max_age,
        )
    )
    server = SimulatorHttpServer((args.host, args.port), simulator)
    host, port = server.server_address[:2]
    print("=" * 72)
    print("SIMULATION ONLY: no Portenta or ESP32 hardware will be accessed or flashed")
    print(f"Listening at http://{host}:{port}")
    print(f"Storage: {simulator.storage}")
    print("=" * 72)

    def stop_server(_signum: int, _frame: Any) -> None:
        threading.Thread(target=server.shutdown, daemon=True).start()

    signal.signal(signal.SIGINT, stop_server)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, stop_server)
    try:
        server.serve_forever()
    finally:
        server.server_close()
        if temporary is not None:
            temporary.cleanup()
        print("SIMULATION stopped")


if __name__ == "__main__":
    main()
