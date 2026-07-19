# BuildWeek 20260721 Submission Information

## Links

- GitHub repository: https://github.com/DeveloperMarcial/Arduino_Public
- YouTube demonstration: TODO
- Codex session or transcript: TODO
- Contest entry: TODO
- Final release or tag: TODO

## Project

- Title: Portenta H7 Remote ESP32 Programmer
- One-line summary: A Portenta H7 stages, programs, and verifies remote ESP32
  firmware over HTTP, with resumable uploads and a hardware-free PC simulator for
  judges.
- Target hardware: Arduino Portenta H7 and Arduino Nano ESP32
- Primary languages: C++ and Python
- License: MIT

## Key Features

- Chunked HTTP firmware upload with SHA-256 staging validation.
- Interrupted-upload resume using per-image received-chunk maps.
- ESP32 ROM UART flashing with image offsets preserved.
- Post-flash MD5 readback verification.
- Ethernet and Wi-Fi Portenta transports.
- Standard-library Portenta simulator with persistent sessions.
- Automated success, corruption, missing-chunk, resume, invalid-manifest,
  session-recovery, and verification-failure tests.

## Final Verification

- Automated test date: TODO
- Automated test result: TODO
- Simulator demonstration result: TODO
- Physical hardware demonstration date: TODO
- Physical hardware demonstration result: TODO
- Repository visibility verified while signed out: TODO
- YouTube visibility verified while signed out: TODO

## Simulated Versus Physical Operations

The PC simulator models HTTP sessions, manifests, chunk validation, resume,
staging, flash address ranges, progress, and digest verification using local
files. It does not access QSPI, toggle ESP32 programming pins, communicate over
UART, enter the ESP ROM bootloader, erase physical flash, or program hardware.
Those operations require the physical Portenta H7 and ESP32 target.

## Submission Notes

TODO: Add the final contest-facing project description, challenges,
accomplishments, lessons learned, and future work after the video and final
hardware validation are complete.
