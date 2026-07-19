"""Inject Portenta WiFi credentials without storing them in platformio.ini."""

from __future__ import annotations

import getpass
import os
from pathlib import Path

Import("env")


def load_env_file(path: Path) -> None:
    if not path.is_file():
        return

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue

        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()

        if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
            value = value[1:-1]

        os.environ.setdefault(key, value)


def c_string_literal(value: str) -> str:
    escaped = (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
    )
    return f'\\"{escaped}\\"'


def read_secret(name: str, prompt: str, *, hidden: bool = False) -> str:
    value = os.environ.get(name)
    if value is not None:
        return value

    if not os.isatty(0):
        raise SystemExit(
            f"{name} is not set and stdin is not interactive. "
            f"Set {name} in the environment or build from an interactive shell."
        )

    if hidden:
        return getpass.getpass(prompt)

    print(prompt, end="", flush=True)
    return input()


project_dir = Path(env["PROJECT_DIR"])
load_env_file(project_dir / ".portenta_wifi.env")

print("Enter Portenta WiFi credentials. SSID=:")
ssid     = read_secret("PORTENTA_WIFI_SSID", "Portenta WiFi SSID: ")
password = read_secret("PORTENTA_WIFI_PASS", "Portenta WiFi password: ", hidden=True)

if not ssid:
    raise SystemExit("PORTENTA_WIFI_SSID cannot be empty for the WiFi build.")

env.Append(
    CPPDEFINES=[
        ("PORTENTA_WIFI_SSID", c_string_literal(ssid)),
        ("PORTENTA_WIFI_PASS", c_string_literal(password)),
    ]
)

print("Portenta WiFi credentials injected from environment/prompt; password was not printed.")
