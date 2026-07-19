from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from urllib.error import HTTPError
from urllib.parse import quote
from urllib.request import Request, urlopen


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from portenta_sim import PortentaSimulator, SimulatorConfig, SimulatorHttpServer  # noqa: E402


class RunningSimulator:
    def __init__(self, storage: Path, *, fail_verification: str | None = None) -> None:
        config = SimulatorConfig(
            storage=storage,
            flash_delay=0.01,
            progress_steps=1,
            fail_verification=fail_verification,
            session_max_age=0,
        )
        self.simulator = PortentaSimulator(config)
        self.server = SimulatorHttpServer(("127.0.0.1", 0), self.simulator)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base_url = f"http://127.0.0.1:{self.server.server_port}"

    def close(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)


class PortentaSimulatorHttpTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.storage = Path(self.temporary.name)
        self.running = RunningSimulator(self.storage)

    def tearDown(self) -> None:
        self.running.close()
        self.temporary.cleanup()

    def request(
        self,
        method: str,
        path: str,
        *,
        body: bytes | None = None,
        payload: object | None = None,
        expected: int = 200,
    ) -> dict:
        if payload is not None:
            body = json.dumps(payload).encode()
        request = Request(self.running.base_url + path, data=body, method=method)
        if payload is not None:
            request.add_header("Content-Type", "application/json")
        try:
            with urlopen(request, timeout=2) as response:
                status = response.status
                data = response.read()
        except HTTPError as error:
            status = error.code
            data = error.read()
        self.assertEqual(expected, status, data.decode(errors="replace"))
        return json.loads(data)

    def create(self) -> str:
        return self.request("POST", "/api/v1/session")["session_id"]

    @staticmethod
    def manifest(data: bytes, *, offset: int = 0x10000, chunk_size: int = 4) -> dict:
        return {
            "target": "esp32s3",
            "flash_size": 16 * 1024 * 1024,
            "baud": 460800,
            "chunk_size": chunk_size,
            "erase": False,
            "images": [
                {
                    "name": "firmware.bin",
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "offset": offset,
                    "size": len(data),
                    "flash": True,
                }
            ],
        }

    def upload(self, session_id: str, data: bytes, chunk_size: int = 4) -> None:
        for index, offset in enumerate(range(0, len(data), chunk_size)):
            self.request(
                "POST",
                f"/api/v1/session/{session_id}/chunk/firmware.bin/{index}",
                body=data[offset : offset + chunk_size],
            )

    def wait_terminal(self, session_id: str) -> dict:
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            status = self.request("GET", f"/api/v1/session/{session_id}/status")
            if status["state"] in {"completed", "failed"}:
                return status
            time.sleep(0.01)
        self.fail("simulated flash did not finish")

    def test_successful_upload_and_flash_preserves_offset_and_hashes(self) -> None:
        data = b"judge-demo-firmware"
        session_id = self.create()
        self.request("POST", f"/api/v1/session/{session_id}/manifest", payload=self.manifest(data))
        self.upload(session_id, data)
        ready = self.request("GET", f"/api/v1/session/{session_id}/status")
        self.assertEqual("ready_to_flash", ready["state"])

        started = self.request("POST", f"/api/v1/session/{session_id}/flash")
        self.assertEqual("flashing", started["state"])
        final = self.wait_terminal(session_id)

        image = final["images"][0]
        expected_md5 = hashlib.md5(data).hexdigest()
        self.assertEqual("completed", final["state"])
        self.assertEqual(expected_md5, image["staged_md5"])
        self.assertEqual(expected_md5, image["flash_md5"])
        self.assertTrue(image["flash_verified"])
        flash = self.storage / "flash" / "esp32s3.bin"
        with flash.open("rb") as handle:
            handle.seek(0x10000)
            self.assertEqual(data, handle.read(len(data)))

    def test_corrupted_final_chunk_fails_staged_sha256(self) -> None:
        data = b"abcdefgh"
        session_id = self.create()
        self.request("POST", f"/api/v1/session/{session_id}/manifest", payload=self.manifest(data))
        self.request(
            "POST",
            f"/api/v1/session/{session_id}/chunk/firmware.bin/0",
            body=data[:4],
        )
        rejected = self.request(
            "POST",
            f"/api/v1/session/{session_id}/chunk/firmware.bin/1",
            body=b"BAD!",
            expected=409,
        )
        self.assertEqual("failed", rejected["state"])
        self.assertIn("hash verification failed", rejected["detail"])

    def test_missing_chunk_is_reported_and_flash_is_rejected(self) -> None:
        data = b"abcdefghij"
        session_id = self.create()
        self.request("POST", f"/api/v1/session/{session_id}/manifest", payload=self.manifest(data))
        self.request(
            "POST",
            f"/api/v1/session/{session_id}/chunk/firmware.bin/0",
            body=data[:4],
        )
        chunks = self.request(
            "GET", f"/api/v1/session/{session_id}/chunks/{quote('firmware.bin')}"
        )
        self.assertEqual([True, False, False], chunks["received"])
        rejected = self.request(
            "POST", f"/api/v1/session/{session_id}/flash", expected=409
        )
        self.assertEqual("failed", rejected["state"])
        self.assertIn("not ready", rejected["detail"])

    def test_resume_survives_simulator_restart(self) -> None:
        data = b"abcdefghij"
        session_id = self.create()
        self.request("POST", f"/api/v1/session/{session_id}/manifest", payload=self.manifest(data))
        self.request(
            "POST",
            f"/api/v1/session/{session_id}/chunk/firmware.bin/1",
            body=data[4:8],
        )
        self.running.close()
        self.running = RunningSimulator(self.storage)

        chunks = self.request(
            "GET", f"/api/v1/session/{session_id}/chunks/firmware.bin"
        )
        self.assertEqual([False, True, False], chunks["received"])
        self.request(
            "POST",
            f"/api/v1/session/{session_id}/chunk/firmware.bin/0",
            body=data[:4],
        )
        self.request(
            "POST",
            f"/api/v1/session/{session_id}/chunk/firmware.bin/2",
            body=data[8:],
        )
        status = self.request("GET", f"/api/v1/session/{session_id}/status")
        self.assertEqual("ready_to_flash", status["state"])
        self.assertEqual(len(data), status["images"][0]["received_bytes"])

    def test_invalid_manifest_is_rejected_with_clear_error(self) -> None:
        session_id = self.create()
        invalid = self.manifest(b"data")
        invalid["images"][0]["sha256"] = "not-a-sha256"
        response = self.request(
            "POST",
            f"/api/v1/session/{session_id}/manifest",
            payload=invalid,
            expected=409,
        )
        self.assertEqual("failed", response["state"])
        self.assertIn("invalid sha256", response["detail"])

    def test_known_sessions_lists_all_session_ids(self) -> None:
        first_session = self.create()
        second_session = self.create()

        payload = self.request("GET", "/api/v1/sessions")
        self.assertEqual(
            {first_session, second_session},
            set(payload["session_ids"]),
        )
        self.assertEqual(
            {first_session, second_session},
            {session["session_id"] for session in payload["sessions"]},
        )
        self.assertTrue(all(session["state"] == "created" for session in payload["sessions"]))

    def test_configured_flash_verification_failure(self) -> None:
        self.running.close()
        self.running = RunningSimulator(self.storage, fail_verification="firmware.bin")
        data = b"verification-must-fail"
        session_id = self.create()
        self.request("POST", f"/api/v1/session/{session_id}/manifest", payload=self.manifest(data))
        self.upload(session_id, data)
        self.request("POST", f"/api/v1/session/{session_id}/flash")
        final = self.wait_terminal(session_id)

        image = final["images"][0]
        self.assertEqual("failed", final["state"])
        self.assertFalse(image["flash_verified"])
        self.assertNotEqual(image["staged_md5"], image["flash_md5"])
        self.assertIn("verification mismatch", final["detail"])


if __name__ == "__main__":
    unittest.main()
