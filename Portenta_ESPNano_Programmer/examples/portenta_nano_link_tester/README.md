# Portenta Nano Link Tester

This standalone Portenta example exercises the same wiring used by the main flasher project:

- `Serial1` to Nano ESP32 `D0/D1`
- `D20 / PC_3` to Nano `EN / RESET`
- `D21 / PA_4` to Nano `GPIO0 / BOOT`

Open the Portenta USB serial monitor at `115200` and type:

- `ping` to verify UART data reaches the Nano and a reply comes back
- `status` to request the Nano's current boot count, reset reason, uptime, and `GPIO0` level
- `gpio low` / `gpio high` or `gpio0 low` / `gpio0 high` to verify the Nano sees `GPIO0` changes
- `gpio pulse` or `gpio0 pulse` to pulse the Nano `GPIO0 / BOOT` line manually
- `reset` to verify the Nano reboots and reports a new boot count
- `bootloader` to perform the same style of `GPIO0 low + EN pulse` sequence used for ESP ROM boot
- `led red`, `led green`, `led blue`, `led off` to command the Nano tester RGB LED
- `auto on` to cycle through UART ping, `GPIO0` toggles, and reset pulses repeatedly
- `auto off` to stop the automatic test cycle

## Notes

- The companion Nano tester now persists `boot_count`, so repeated Portenta-driven `reset` commands should increment it instead of staying at `1`.
- The Nano tester supports a `CLEARCOUNT` command, but the current Portenta tester does not expose a generic pass-through console command for it.
- During a successful `bootloader` sequence, the Nano test sketch should stop talking because the ESP32-S3 enters ROM bootloader mode instead of running the sketch.
- The main flasher in the repo root has now been hardware-validated on this same wiring, so the `bootloader` command is no longer only an indirect test. The full project has successfully entered ROM boot mode, synchronized, written flash, verified flash MD5s, and rebooted the Nano back into normal execution.

## How This Relates To The Main Project

This example is intentionally narrower than the real flasher. It is useful when you want to isolate wiring and signal-control problems before involving HTTP upload, QSPI staging, ROM packet parsing, or image flashing.

A few details discovered while bringing up the full flasher may help when using this tester:

- ESP32-S3 ROM boot can emit multiple `SYNC` replies after the first successful handshake.
- ESP32-S3 ROM command responses may include extra reserved trailer bytes, so a naive parser can misread a success reply as an error.
- `FLASH_END` is not a strong signal of success on its own when the host already controls `EN` and can hard-reset the target.

## Target-Specific Note

This tester assumes the target is an Arduino Nano ESP32, which means `ESP32-S3` behavior.

If you reuse this tester with another ESP32-family board, review:

- ROM UART pin mapping
- boot strap pin and reset polarity
- whether the board uses ESP32-S3-style ROM behavior or an older ESP32-family variant
- whether the main repo target configuration should change bootloader offsets, flash size, partition mapping, or stub-loader family
