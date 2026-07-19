from __future__ import annotations

import argparse
import contextlib
import io
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import Mock, patch


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from esp32_uploader import (  # noqa: E402
    Esp32UsbMonitor,
    NARRATION_FILES,
    NarrationPlayer,
    SerialPortInfo,
    build_resume_command,
    is_esp32_serial_port,
    print_flash_finished,
    print_interrupted_recovery,
    print_known_sessions,
    usb_port_transition_messages,
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

    def test_total_time_prints_immediately_after_flash_time(self) -> None:
        output = io.StringIO()
        with (
            patch("esp32_uploader.time.perf_counter", return_value=35.5),
            contextlib.redirect_stdout(output),
        ):
            print_flash_finished(
                flash_started=30.0,
                total_started=10.0,
                state="completed",
            )

        self.assertEqual(
            output.getvalue().splitlines(),
            [
                "flash finished: 5.50 seconds",
                "total time: 25.50 seconds",
                "completed",
            ],
        )

    def test_completed_narration_runs_before_final_state(self) -> None:
        output = io.StringIO()

        def narrate() -> None:
            print("narration played")

        with (
            patch("esp32_uploader.time.perf_counter", return_value=35.5),
            contextlib.redirect_stdout(output),
        ):
            print_flash_finished(
                flash_started=30.0,
                total_started=10.0,
                state="completed",
                before_state=narrate,
            )

        self.assertEqual(
            output.getvalue().splitlines(),
            [
                "flash finished: 5.50 seconds",
                "total time: 25.50 seconds",
                "narration played",
                "completed",
            ],
        )

    def test_narration_cues_play_the_bundled_filenames(self) -> None:
        self.assertEqual(
            list(NARRATION_FILES.values()),
            [
                "Male_Voice01_setup_and_command.wav",
                "Male_Voice02_uploader_and_staging.wav",
                "Male_Voice02a_chunk_resume_and_verification.wav",
                "Male_Voice03_start_flash.wav",
                "Male_Voice04_flash_done..wav",
            ],
        )
        fake_winsound = Mock()
        fake_winsound.SND_FILENAME = 1
        fake_winsound.SND_NODEFAULT = 2
        fake_winsound.SND_ASYNC = 4

        with tempfile.TemporaryDirectory() as temp_dir:
            narration_dir = Path(temp_dir)
            for filename in NARRATION_FILES.values():
                (narration_dir / filename).write_bytes(b"RIFF")
            player = NarrationPlayer(narration_dir=narration_dir)

            with patch("esp32_uploader.winsound", fake_winsound):
                player.play("setup", wait=True)
                player.play_sequence(("upload", "transfer"))
                player._sequence_thread.join(timeout=1)
                player.play("flash")
                player.play("completed", wait=True)

        played_paths = [
            Path(call.args[0]).name
            for call in fake_winsound.PlaySound.call_args_list
        ]
        self.assertEqual(played_paths, list(NARRATION_FILES.values()))
        self.assertEqual(
            fake_winsound.PlaySound.call_args_list[0].args[1],
            fake_winsound.SND_FILENAME | fake_winsound.SND_NODEFAULT,
        )
        self.assertEqual(
            fake_winsound.PlaySound.call_args_list[1].args[1],
            fake_winsound.SND_FILENAME | fake_winsound.SND_NODEFAULT,
        )
        self.assertEqual(
            fake_winsound.PlaySound.call_args_list[2].args[1],
            fake_winsound.SND_FILENAME | fake_winsound.SND_NODEFAULT,
        )
        self.assertEqual(
            fake_winsound.PlaySound.call_args_list[3].args[1],
            fake_winsound.SND_FILENAME
            | fake_winsound.SND_NODEFAULT
            | fake_winsound.SND_ASYNC,
        )

    def test_nano_usb_port_is_distinguished_from_portenta(self) -> None:
        nano = SerialPortInfo("COM7", "USB Serial Device", 0x2341, 0x0070, "NANO")
        portenta = SerialPortInfo("COM8", "USB Serial Device", 0x2341, 0x025B, "H7")

        self.assertTrue(is_esp32_serial_port(nano, "arduino_nano_esp32"))
        self.assertFalse(is_esp32_serial_port(portenta, "arduino_nano_esp32"))

    def test_usb_disconnect_and_reconnect_messages_include_com_port(self) -> None:
        nano = SerialPortInfo("COM7", "USB Serial Device", 0x2341, 0x0070, "NANO")

        disconnected = usb_port_transition_messages([nano], [])
        reconnected = usb_port_transition_messages([], [nano])

        self.assertIn("COM7", disconnected[0])
        self.assertIn("disconnected", disconnected[0])
        self.assertIn("COM7", reconnected[0])
        self.assertIn("detected", reconnected[0])

    def test_completed_waits_for_previously_detected_port_to_reappear(self) -> None:
        nano = SerialPortInfo("COM7", "USB Serial Device", 0x2341, 0x0070, "NANO")
        visible_ports = [nano]

        def list_ports() -> list[SerialPortInfo]:
            return list(visible_ports)

        monitor = Esp32UsbMonitor(
            "arduino_nano_esp32",
            poll_interval=0.01,
            port_lister=list_ports,
        )
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            monitor.start()
            visible_ports.clear()
            time.sleep(0.03)
            self.assertFalse(monitor.wait_for_reappearance(timeout=0.02))
            monitor.stop()

        self.assertIn("waiting up to 0.02 seconds", output.getvalue())
        self.assertIn("not reporting completed", output.getvalue())

    def test_completed_does_not_wait_when_port_never_disconnected(self) -> None:
        nano = SerialPortInfo("COM7", "USB Serial Device", 0x2341, 0x0070, "NANO")
        monitor = Esp32UsbMonitor(
            "arduino_nano_esp32",
            poll_interval=1,
            port_lister=lambda: [nano],
        )
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            monitor.start()
            self.assertTrue(monitor.wait_for_reappearance(timeout=0))
            monitor.stop()

        self.assertIn("completion gate armed for COM7", output.getvalue())
        self.assertIn(
            "completion gate satisfied; COM7 (VID:PID 2341:0070) is present",
            output.getvalue(),
        )

    def test_completed_wait_unblocks_when_port_reappears(self) -> None:
        nano = SerialPortInfo("COM7", "USB Serial Device", 0x2341, 0x0070, "NANO")
        visible_ports = [nano]
        monitor = Esp32UsbMonitor(
            "arduino_nano_esp32",
            poll_interval=0.01,
            port_lister=lambda: list(visible_ports),
        )

        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            monitor.start()
            visible_ports.clear()
            time.sleep(0.03)
            reconnect = threading.Timer(0.03, lambda: visible_ports.append(nano))
            reconnect.start()
            self.assertTrue(monitor.wait_for_reappearance(timeout=0.5))
            reconnect.join()
            monitor.stop()

        self.assertIn("completion gate satisfied", output.getvalue())
        self.assertIn("COM7 (VID:PID 2341:0070) reappeared", output.getvalue())

    def test_second_disconnect_clears_previous_reappearance(self) -> None:
        nano = SerialPortInfo("COM7", "USB Serial Device", 0x2341, 0x0070, "NANO")
        visible_ports = [nano]
        monitor = Esp32UsbMonitor(
            "arduino_nano_esp32",
            poll_interval=1,
            port_lister=lambda: list(visible_ports),
        )

        with contextlib.redirect_stdout(io.StringIO()):
            monitor.start()
            visible_ports.clear()
            monitor._observe_ports([])
            visible_ports.append(nano)
            monitor._observe_ports([nano])
            visible_ports.clear()
            monitor._observe_ports([])
            self.assertFalse(monitor.wait_for_reappearance(timeout=0))
            monitor.stop()


if __name__ == "__main__":
    unittest.main()
