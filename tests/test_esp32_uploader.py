from __future__ import annotations

import argparse
import contextlib
import io
import sys
import unittest
from pathlib import Path
from unittest.mock import patch


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from esp32_uploader import (  # noqa: E402
    build_resume_command,
    print_interrupted_recovery,
    print_known_sessions,
)


class Esp32UploaderRecoveryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.args = argparse.Namespace(
            host="192.168.1.178",
            port=8080,
            target="arduino_nano_esp32",
            baud=460800,
            chunk_size=1024,
            poll_interval=0.5,
            erase=False,
            image=[
                r"bootloader:.\images\build\bootloader.bin",
                r"app0:.\images\build\firmware.bin",
            ],
        )

    def test_resume_command_uses_device_session_id(self) -> None:
        command = build_resume_command(self.args, "sess-a1b2c3d4")

        self.assertIn("--session-id sess-a1b2c3d4", command)
        self.assertNotIn("sess-xxxxxxxx", command)
        self.assertNotIn("sess-12345678", command)

    def test_interruption_output_repeats_active_session_id(self) -> None:
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            print_interrupted_recovery(
                self.args,
                "sess-deadbeef",
                resume=True,
            )

        text = output.getvalue()
        self.assertIn("Active session: sess-deadbeef", text)
        self.assertIn("--session-id sess-deadbeef", text)
        self.assertIn("--resume", text)

    def test_known_sessions_output_includes_ids_and_states(self) -> None:
        payload = {
            "session_ids": ["sess-a1b2c3d4", "sess-deadbeef"],
            "sessions": [
                {"session_id": "sess-a1b2c3d4", "state": "uploading"},
                {"session_id": "sess-deadbeef", "state": "completed"},
            ],
        }
        output = io.StringIO()
        with (
            patch("esp32_uploader.request_json", return_value=payload),
            contextlib.redirect_stdout(output),
        ):
            print_known_sessions("http://127.0.0.1:8080")

        text = output.getvalue()
        self.assertIn("sess-a1b2c3d4 state=uploading", text)
        self.assertIn("sess-deadbeef state=completed", text)


if __name__ == "__main__":
    unittest.main()
