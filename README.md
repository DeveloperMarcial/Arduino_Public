# DeveloperMarcial Arduino Projects

Open-source embedded projects and community contributions by DeveloperMarcial, sharing practical software, hardware designs, tools, documentation, and tested examples. More projects are coming soon.

## Featured Project

### [FlashBridge H7: Network-to-Silicon ESP32 Programming](Portenta_ESPNano_Programmer)

*Upload over Wi-Fi. Resume after interruption. Flash through UART. Verify every byte.*

FlashBridge H7 turns an Arduino Portenta H7 into a network-connected ESP32 programmer. A desktop uploader sends firmware images to the Portenta, which stages them in QSPI storage, controls the ESP32 programming pins, flashes the images at their required offsets, and verifies the resulting flash hashes.

The repository also includes a persistent PC simulator, allowing the complete upload, interruption/resume, flash-progress, and verification workflow to be evaluated without physical hardware.

**Highlights:**

- Resumable, chunked firmware uploads
- Manifest and SHA-256 validation
- ESP32 UART flashing through the Portenta H7
- Staged-image and flashed-image MD5 verification
- Connection-loss recovery with exact resume commands
- Windows ESP32 COM-port monitoring
- Persistent, hardware-free Portenta simulator
- 21 automated success and failure tests
- Wiring diagrams, hardware photos, and judge instructions

## Built with Codex & GPT-5.6

FlashBridge H7 was substantially improved through iterative collaboration with Codex and GPT-5.6. Codex inspected the existing system, implemented the simulator and recovery features, created tests, debugged failures, expanded the documentation, assisted with video narration and processing, synchronized this public repository, and performed the requested Git workflow.

We supplied the product requirements, hardware knowledge, engineering decisions, acceptance criteria, and physical-hardware validation.

## Demonstration

[Watch the narrated hardware demonstration on YouTube](https://youtu.be/MSb_y7rtKms)

For the simulator demo, physical wiring, uploader commands, and complete technical documentation, see the [FlashBridge H7 README](Portenta_ESPNano_Programmer/README.md).

## Repository Layout

| Project | Description |
| --- | --- |
| [`Portenta_ESPNano_Programmer`](Portenta_ESPNano_Programmer) | Network-based Portenta H7 programmer and simulator for ESP32 targets |

## Licensing

Each project contains its applicable license and third-party notices. Review those files before using, modifying, or distributing the software.
