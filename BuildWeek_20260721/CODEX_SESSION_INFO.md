# Codex Session Information

Complete this document before contest submission. Publish a curated record rather
than raw local Codex storage, which may contain private paths or unrelated data.

## Session Metadata

- Project: Portenta H7 Remote ESP32 Programmer
- Contest package: BuildWeek 20260721
- GitHub repository: https://github.com/DeveloperMarcial/Arduino_Public
- Codex session ID: TODO
- Approved Codex share or transcript URL: TODO
- Session date or date range: TODO
- Codex product and model shown in the session UI: TODO
- Human contributor: Dev Marcial

## Original Objective

Build a judge-friendly PC simulator for the Portenta H7 firmware protocol so the
complete ESP32 firmware upload, interrupted-transfer resume, simulated flash, and
digest verification workflow can be evaluated without physical hardware.

## Codex-Assisted Work Summary

- Inspected the uploader, Portenta firmware, protocol fields, state transitions,
  and README.
- Implemented the local Portenta HTTP simulator and persistent session storage.
- Added protocol validation, interrupted-upload resume, simulated flash offsets,
  progress reporting, and staged/flash digest verification.
- Added uploader timing and connection-recovery output with real session IDs.
- Added automated success and failure tests.
- Expanded judge, timeout, resume, full-erase, simulator, and unittest
  documentation.
- Prepared the reduced GitHub repository for contest judges.

## Human Decisions and Verification

- Hardware selection, wiring, pin mapping, and physical test setup: TODO
- Physical Portenta-to-Nano ESP32 flash tests performed: TODO
- Manual review or changes made after Codex output: TODO
- Final automated test command and result: TODO
- Final hardware demonstration result: TODO

## Safety and Privacy Review

- [ ] Remove credentials, access tokens, Wi-Fi details, and private URLs.
- [ ] Replace unnecessary local absolute paths with portable paths.
- [ ] Exclude Codex authentication files, databases, caches, and raw local
      session storage.
- [ ] Confirm any shared transcript contains only contest-relevant material.
- [ ] Confirm the published record accurately identifies simulated and physical
      operations.
