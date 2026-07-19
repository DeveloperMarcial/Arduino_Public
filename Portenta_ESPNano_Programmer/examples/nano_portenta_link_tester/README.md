# Nano Portenta Link Tester

This standalone Nano ESP32 example is meant to be paired with the Portenta test harness in the sibling `portenta_nano_link_tester` folder.

What it proves:

- UART traffic from the Portenta arrives on the Nano `D0/D1` pins
- The Portenta can drive the Nano `GPIO0` line high and low while the sketch is running
- The Portenta can reset the Nano through `EN`, which shows up as a new boot count and reset reason
- The same physical wiring used by the main project is good enough for the Portenta to place the Nano ESP32 into the ESP32-S3 ROM bootloader

Important limitation:

If `GPIO0` is held low during reset correctly, the Nano enters the ESP32-S3 ROM bootloader and this sketch stops running. So normal sketch output can prove `GPIO0` line control and `EN` reset control separately, while the actual ROM-boot strap test is best confirmed by seeing that the sketch stops talking during the `bootloader` command.

What has now been confirmed by the main project on the same wiring:

- The Portenta can sync with the Nano ESP32 ROM bootloader at `115200`
- The target behaves like an `ESP32-S3`, not a generic legacy ESP32
- The Nano can be flashed end-to-end through the Portenta, including flash-region MD5 verification and return to normal boot

That means this link tester is now best thought of as a focused wiring and signal-debug tool, while the repo root firmware has already proven the full ROM flashing path on real hardware.

## Build and Upload

Build the Nano test app:

```powershell
pio run -d examples\nano_portenta_link_tester -e arduino_nano_esp32
```

Upload the Nano test app:

Double-click Nano button on PCB.

```powershell
pio run -d examples\nano_portenta_link_tester -e arduino_nano_esp32 -t upload -v
```

If PlatformIO does not auto-detect the Nano's USB serial port correctly, override it explicitly:

```powershell
pio run -d examples\nano_portenta_link_tester -e arduino_nano_esp32 -t upload -v --upload-port COM15
pio device monitor -p COM15 -b 115200
```

You can also uncomment and edit these lines in `examples/nano_portenta_link_tester/platformio.ini` for your machine:

```ini
monitor_port = COM15
upload_port = COM15
```

To see which port the Nano is currently using:

```powershell
pio device list
```

On this machine during testing, the Nano enumerated as `COM15` with USB VID:PID `303A:1001`, which is why forcing the port may be necessary even though the board type is still `arduino_nano_esp32`.

## Companion Portenta Test App

Build the Portenta-side tester:

```powershell
pio run -d examples\portenta_nano_link_tester -e portenta_h7_m7
```

Upload the Portenta-side tester:

```powershell
pio run -d examples\portenta_nano_link_tester -e portenta_h7_m7 -t upload -v
```

## Target-Specific Note

These tester examples are written around the Arduino Nano ESP32, which is an `ESP32-S3` board.

If you adapt the wiring and test flow to a different ESP32-family target, double-check:

- whether the ROM boot strap pin is still `GPIO0`
- whether the board exposes the ROM UART on the same pins you expect
- whether the bootloader image offset is `0x0` or `0x1000`
- whether the main flasher should use `esp32`, `esp32s3`, or another target family in its manifest and stub-loader selection
