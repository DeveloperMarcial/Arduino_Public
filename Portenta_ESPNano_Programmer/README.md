# FlashBridge H7: AES-Secured Network-to-Silicon ESP32 Programming

*100% Prompt-Driven Application Code, Built with Codex & GPT-5.6.*

FlashBridge H7's firmware, PC tools, simulator, recovery workflow, and automated
tests were created through iterative prompting with Codex and GPT-5.6. My direct
code contribution was limited to editing the main README; I guided the
requirements, reviewed the results, and validated the system on physical
hardware.

---

*Securely Upload, Resume, Flash, and Verify.*

## Use Cases

### Remote Firmware Deployment

Upload new ESP32 bootloader, partition, and application images over Ethernet or
WiFi. The Portenta stages the images, programs the target through UART, and
verifies what landed in flash without requiring a direct PC-to-ESP32 cable.

### Interrupted Upload Recovery

Resume a deployment after a network interruption or uploader restart. FlashBridge
queries the received-chunk maps and transfers only missing chunks instead of
starting the entire upload again.

### Verified Production Programming

Program ESP32 devices during assembly, repair, or field service with SHA-256
staging checks and post-flash MD5 readback instead of relying on a
fire-and-forget write.

### Remote Labs and Classrooms

Keep a Portenta and ESP32 connected in a shared lab so students and distributed
teams can upload and test firmware without repeatedly rewiring or physically
accessing the boards.

### Hardware-Free Evaluation

Run the included PC simulator with the unchanged uploader to demonstrate
sessions, chunk validation, interruption, resume, flash offsets, progress, and
digest verification without owning a Portenta H7 or ESP32.

### Future AES-Protected Deployment

Add AES encryption to protect proprietary firmware in transit while retaining
resumable uploads, staged-image validation, and post-flash verification.

> **Security status:** AES encryption is planned. The current implementation
> uses HTTP for transport, SHA-256 to verify staged images, and MD5 readback to
> verify flashed images.

## What It Does

This project turns an Arduino Portenta H7 into a network-connected, production-style programmer for a remote ESP32. A PC uploads bootloader, partition, and application images over HTTP; the Portenta stages them in QSPI, drives the ESP32 boot pins, flashes each image at its required address over UART, and verifies what actually landed in flash.

Why it rocks: this is not a fire-and-forget uploader. Transfers are chunked, hash-checked, and resumable after an interruption. Staged images are verified with SHA-256, flashed images are read back and verified with MD5, and every phase exposes useful progress and errors. The same uploader works over Ethernet or WiFi, while the included PC simulator lets judges exercise sessions, resume, flashing, and verification without owning either board.

Arduino Portenta H7: The WiFi RXer and ESP32 Nano programmer
* $115 [Buy It](https://store-usa.arduino.cc/products/portenta-h7])
* $50 [Wifi/Ethernet Shield](https://store-usa.arduino.cc/products/portenta-vision-shield-ethernet?_pos=1&_psq=AVX00039+OR+ASX00031+OR+ASX00027+OR+ASX00021&_psid=a54aa7f3a&_ss=e&_v=1.0)
* [Datasheet](https://www.st.com/resource/en/datasheet/stm32h747xi.pdf)
* [Schematic](https://content.arduino.cc/assets/Pinout-PortentaH7_latest.pdf)

Arduino Nano (ESP32): The target we want to remotely program
* $20 [Buy It](https://store-usa.arduino.cc/products/nano-esp32-with-headers?utm_source=google&utm_medium=cpc&utm_campaign=US-Pmax&gad_source=1&gad_campaignid=21317508903&gbraid=0AAAAACbEa851RIH7ETsiQLcRBH2UTh4Qz&gclid=Cj0KCQjw6_HSBhCpARIsANvVltb0hvLrkWAmrkuXAsR3QqS2igoS6kAk_-hC5XfleictbejjI_P49HsaAg8nEALw_wcB)
* [Datasheet](https://docs.arduino.cc/resources/datasheets/ABX00083-datasheet.pdf) <-- See page 16 for pinout.
* [Schematic](https://docs.arduino.cc/resources/schematics/ABX00083-schematics.pdf)

This project provides a [PlatformIO](https://docs.platformio.org/en/latest/integration/ide/vscode.html) application for an `Arduino Portenta H7` that receives ESP32 firmware artifacts over Portenta Ethernet or WiFi and flashes a remote ESP32 over `UART1 + EN + GPIO0`.

## Hardware Mapping

Recommended [Portenta wiring](Pinouts.png) when the `Portenta Vision Shield Ethernet` is attached:

Portenta H7 (USB Up)                                    Arduino Nano ESP32 (USB Up)
----------------------------------------------------    ---------------------------------------------------
- J1-33 UART1 TX D14 PA6  (6 pin down on right side) -> ESP32 U0RXD D0 (1 up from the bottom on right side) VIO
- J1-35 UART1 RX D13 PA10 (7 pin down on right side) -> ESP32 U0TXD D1 (0 up from the bottom on right side) BLU
- J2-76          D20 PC3  (7 pin down on left  side) -> ESP32 EN       (2 up from the bottom on right side) YLW
- J2-78          D21 PA4  (8 pin down on left  side) -> ESP32 GPIO0    (2 up from the bottom on left  side) ORG
- GND                     (4 pin down on right side) -> ESP32 GND      (1 up from the bottom on left  side)

Avoid `J2-46 GPIO_0` because the Vision Shield pinout shows that line used internally by the shield.

On the Arduino Nano ESP32, the "green pulsing" LED behavior is usually the Arduino UF2/USB bootloader mode, like what you get from a double-press reset button on PCB. That is not necessarily the same thing as the ESP32-S3 ROM serial bootloader that `esptool` talks to over TX0/RX0 on the Nano.
For this project, the Portenta is trying to make the Nano enter ESP32 ROM bootloader by holding:
    GPIO0 / BOOT = LOW
    EN / RESET   = pulse LOW then HIGH
Then it sends the ESP ROM sync packet over UART at 115200.

## Project Layout

- `src/main.cpp`: boot, loop, and top-level service orchestration
- `src/RemoteUpdateService.*`: Ethernet/WiFi transport, HTTP API, and session handling
- `src/Esp32RomFlasher.*`: UART/GPIO flashing flow and ESP ROM packet protocol
- `src/StagingStore.*`: QSPI-backed file staging and chunk tracking
- `include/AppConfig.h`: transport, timing, and pin assignments
- `tools/esp32_uploader.py`: PC-side uploader and flash trigger script
- `examples/session-manifest.json`: example upload manifest

## Protocol

The transport is intentionally simple JSON-over-HTTP:

1. `POST /api/v1/session`
2. `POST /api/v1/session/<id>/manifest`
3. `POST /api/v1/session/<id>/chunk/<image>/<index>`
4. `POST /api/v1/session/<id>/flash`
5. `GET /api/v1/session/<id>/status`
6. `GET /api/v1/session/<id>/chunks/<image>`

The Portenta-side transport can bind to the Arduino mbed Ethernet or WiFi server stack. Ethernet is the default and falls back to a static `192.168.1.177:8080` listener if DHCP is unavailable. WiFi uses the SSID/password build flags in `platformio.ini` and prints its assigned IP address on serial.

Chunk uploads are QSPI-backed and support sparse or resumable transfers. The manifest now includes `chunk_size`, and the uploader can query per-image chunk maps to send only missing chunks.
Each manifest image also carries a `sha256`, and the Portenta verifies the staged file contents against that digest before a session becomes flashable.
After each flash write, the Portenta also asks the ESP bootloader for an MD5 digest of the written flash region and compares it against an MD5 of the staged file. The final status payload now includes `staged_md5`, `flash_md5`, and `flash_verified` for each flashed image so the host can see proof of what actually landed in ESP flash.

### Timeouts and Polling

PC uploader timing:

| Operation | Current value |
| --- | ---: |
| Session, manifest, chunk, chunk-map, and status HTTP requests | 10 seconds |
| Flash initiation HTTP request | 120 seconds |
| Status polling interval | 0.5 seconds by default |

The `requests` timeout value applies separately to establishing the connection and waiting for response data; it is not a total deadline for the complete upload. A refused connection can fail immediately. Change the polling interval with `--poll-interval`; the HTTP timeout constants are currently defined as `DEFAULT_TIMEOUT` and `FLASH_TIMEOUT` in `tools/esp32_uploader.py`.

On systems with `pyserial` installed, the uploader also identifies the Arduino Nano ESP32 COM port and watches it while flashing. Windows may remove that COM port when the ESP32 enters programming mode; this is expected because the Portenta performs the actual programming over its UART connection. If the port was present before flashing and disconnects, the uploader waits up to 30 seconds for it to reappear before printing `completed`; a timeout produces a clear USB-status error and does not report completion. Install the uploader dependencies with `python -m pip install -r tools/requirements.txt`.

Portenta firmware timing:

| Operation | Current value |
| --- | ---: |
| Ethernet/WiFi startup | 30 seconds |
| HTTP request reading | 2 seconds |
| ESP bootloader settle delay | 15 seconds |
| ESP ROM synchronization response | 2 seconds |
| Normal ESP ROM command response | 3 seconds |
| `FLASH_END` response | 250 milliseconds |
| Flasher-stub ready handshake | 3 seconds |
| Full-chip stub erase | 120 seconds |
| Region erase/`FLASH_BEGIN` | At least 3 seconds, scaled at 30 seconds per MB |
| ESP flash MD5 readback | At least 3 seconds, scaled at 8 seconds per MB |

If a normal uploader request times out after a session ID has been received, the uploader prints a phase-appropriate retry or resume command. A flash-request timeout does not necessarily mean flashing failed: the uploader first queries session status and continues polling when the Portenta reports `flashing`, `completed`, or `failed`.

## PC Simulator for Judges

`tools/portenta_sim.py` implements the same HTTP routes and status fields as the Portenta firmware so the existing uploader can be demonstrated without hardware. It uses only the Python standard library. Start it from the repository root:

```powershell
# Start Simulator App
python tools/portenta_sim.py --host 127.0.0.1 --port 8080
```

Without `--storage`, staged data is placed in a temporary directory and safely removed when the simulator exits. Use a persistent directory to demonstrate resume across restarts:

```powershell
# Start Simulator App with Storage
python tools/portenta_sim.py --host 127.0.0.1 --port 8080 --storage .portenta-sim --flash-delay 0.4
```

The startup banner, HTTP details, root health response, and status details are labeled `SIMULATION`. `GET /health` and `GET /api/v1/device` also return `"simulation": true`. `GET /api/v1/sessions` lists every known persisted session ID and its state. Persisted sessions older than seven days are cleaned up at startup by default; set `--session-max-age 0` to retain them indefinitely. Use `--fail-verification firmware.bin` (or `*`) to demonstrate a failed ESP flash readback check.

Run the existing uploader unchanged apart from targeting localhost:

```powershell
# Generic Upload
python tools/esp32_uploader.py `
  --host 127.0.0.1 --port 8080 `
  --target arduino_nano_esp32 --baud 460800 `
  --image bootloader:path\to\bootloader.bin `
  --image partitions:path\to\partitions.bin `
  --image app0:path\to\firmware.bin

# This Repo: 3 Blinks: 1 long, then 2 short
python tools/esp32_uploader.py `
  --host 127.0.0.1 --port 8080 `
  --target arduino_nano_esp32 --baud 460800 `
  --image bootloader:.\images\9ADE7FFE281152CE1367D725B05AA39B\Blinky_1000ms.ino.bootloader.bin `
  --image partitions:.\images\9ADE7FFE281152CE1367D725B05AA39B\Blinky_1000ms.ino.partitions.bin `
  --image app0:.\images\9ADE7FFE281152CE1367D725B05AA39B\Blinky_1000ms.ino.bin
	
# This Repo: Upload other Arduino App: 3 Fast Blinks
python tools/esp32_uploader.py `
  --host 127.0.0.1 --port 8080 `
  --target arduino_nano_esp32 --baud 460800 `
  --image bootloader:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.bootloader.bin `
  --image partitions:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.partitions.bin `
  --image app0:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.bin
```

The simulator validates image names, offsets, flash bounds, chunk indexes and exact chunk lengths. Once all chunks arrive, it verifies each staged SHA-256. Flashable images are written at their manifest offsets in `.portenta-sim\flash\<target>.bin`, then read back to produce the same `staged_md5`, `flash_md5`, and `flash_verified` fields as the device.

### Short Judge Demo

1. Start the persistent simulator:

   ```powershell
   # Open PowerShell (Terminal_1):
   # cd <repo_root>
   python tools/portenta_sim.py --host 127.0.0.1 --port 8080 --storage .portenta-sim --flash-delay 0.4
   ```

2. In another PowerShell terminal, run the complete uploader command below. While it displays `image chunk upload started...`, press `Ctrl+Break` to simulate an interrupted upload. Keep the simulator running in the first terminal and note the real session ID and resume command printed by the uploader.

   ```powershell
   # Open Another PowerShell (Terminal_2):
   # cd <repo_root>
   python tools/esp32_uploader.py `
     --host 127.0.0.1 --port 8080 `
     --target arduino_nano_esp32 --baud 460800 `
     --chunk-size 1024 `
     --poll-interval 0.5 `
     --image 'bootloader:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.bootloader.bin' `
     --image 'partitions:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.partitions.bin' `
     --image 'app0:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.bin'

   # When you see "image chunk upload started...", press `Ctrl+Break`.
   ```

3. The simulator in Terminal_1 is now terminated. Then start another upload with the same persistent storage:

   ```powershell
   # In Terminal_1:
   python tools/portenta_sim.py --host 127.0.0.1 --port 8080 --storage .portenta-sim --flash-delay 0.4
   ```

4. Run the exact resume command printed by the uploader after `Ctrl+Break`. It contains the real session ID from step 2 and has this complete form:

   ```powershell
   # NOTE: In Terminal_2 you will see the following command. Complete with Session-Id.
   # In Terminal_2:
   python tools/esp32_uploader.py `
     --host 127.0.0.1 --port 8080 `
     --target arduino_nano_esp32 --baud 460800 `
     --chunk-size 1024 `
     --poll-interval 0.5 `
     --session-id sess-xxxxxxxx `
     --resume `
     --image 'bootloader:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.bootloader.bin' `
     --image 'partitions:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.partitions.bin' `
     --image 'app0:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.bin'
   ```

   In the example above, replace `sess-xxxxxxxx` with the actual session ID printed in step 2. The uploader validates the stored manifest, queries each `received` chunk bitmap, sends only missing chunks, and starts the simulated flash.

5. Watch `flashing` progress details followed by `completed`.
* The uploader prints matching `staged_md5` and `flash_md5` values with `flash_verified=True`.
* Re-run the uploader and allow it to finish.
  * You will see the "total time" is longer verifying the _resume_ option works.

Run the automated simulator checks with:

```powershell
python tools/run_tests.py
or
python -m unittest discover -s tests -v
```

`unittest` is Python's built-in testing framework, so a separate test framework
such as `pytest` is not required. `tools/run_tests.py`:

- discovers test modules and test cases under `tests`
- uses verbose output so every test reports `ok`, `FAIL`, or `ERROR`
- preserves a failing exit code for automation
- ends with a concise result such as `11 of 11 PASSED`

These tests start local simulator servers on temporary ports, use temporary storage directories, exercise upload, resume, validation, failure, session-listing, and flash-verification behavior, then clean up their temporary resources. They do not contact a physical Portenta or ESP32 and do not flash real hardware. A successful run ends with `OK` and the pass-count summary; failures include a traceback identifying the failed test and assertion.

The simulator models local staging, interrupted upload/resume, erase metadata acceptance, ESP flash address ranges, progress, and digest verification. It does **not** access QSPI, toggle Portenta `EN`/`GPIO0`, communicate over UART, enter an ESP ROM bootloader, run a real stub erase, verify electrical wiring, or flash physical Portenta/ESP32 hardware. Those operations require the actual boards.

### ESP ROM Notes

The Arduino Nano ESP32 path is now hardware-validated end-to-end with the Portenta driving the ESP32-S3 ROM bootloader over `UART1 + EN + GPIO0`.

Some practical ESP ROM details that turned out to matter:

- A successful ROM `SYNC` can be followed by several extra `SYNC` replies. The flasher now drains those before issuing the next command so `SPI_ATTACH` does not accidentally consume a stale `0x08` response.
- The ROM response layout is not always just `data + 2 status bytes`. On ESP32-S3 ROM replies, commands such as `FLASH_BEGIN` and `SPI_FLASH_MD5` can return `expected data`, then `2 status bytes`, then `2 reserved bytes`. The flasher now parses that layout explicitly instead of assuming the last two bytes are always status.
- `SPI_FLASH_MD5` in ROM mode returns a 32-character ASCII MD5 string for the requested flash region. That is now used for post-write verification against the staged image MD5.
- `FLASH_BEGIN` for ESP32-S3 ROM uses the extended parameter format, which includes the extra `encrypted_write` word even when encryption is not in use.
- `FLASH_END` is treated as best-effort. The Portenta already has direct control of `EN` and `GPIO0`, so a missing or odd final ROM reply should not mark a verified flash as failed if the Portenta can still hardware-reset the target cleanly.

What is QSPI

On the Arduino Portenta H7, QSPI (Quad Serial Peripheral Interface) refers to both a communication protocol and a specific external Flash memory chip soldered onto the board.  While the Portenta's main processor (STM32H747) has 2MB of internal flash, the QSPI interface provides a massive expansion for storing heavy assets like graphics, machine learning models, or large codebases. 

Key Specifications

- Capacity: The standard Portenta H7 comes with 16MB of external QSPI Flash. High-end custom versions can support up to 128MB.
- Speed: It operates at up to 133 MHz. Because it uses four data lines instead of one (like standard SPI), it can transfer data up to four times faster.
- Memory Mapping: The processor can "map" this external memory into its own address space (starting at address 0x90000000), allowing the CPU to read data or even execute code directly from it as if it were internal memory. 

You can interact with the QSPI flash using the BlockDevice API provided by Mbed OS on the Portenta. Arduino provides a dedicated example called QSPIFormat (found under File > Examples > STM32H747_System) to initialize and partition the memory for use. 

## Esptool

You do not need `esptool` to build or run the Portenta-side firmware in this repository. `PlatformIO` handles the Portenta build and upload flow.

You may want `esptool` on your PC if you want to:

- flash an ESP32 directly from your computer
- inspect or compare behavior against Espressif's reference tooling
- use the uploader's `--erase` mode, which stages the local Espressif stub loader to enable true full-chip erase

The Portenta firmware still starts from the ESP ROM bootloader, but `tools/esp32_uploader.py --erase` now augments the manifest with target-specific stub metadata and uploads the local `esptool` stub as non-flash helper images. For the Arduino Nano ESP32 target, the uploader selects the ESP32-S3 stub. The Portenta RAM-loads that stub, waits for the `OHAI` handshake, issues the stub-only full-chip erase command, and then continues with the normal image flash flow.

## Network Transport

The Portenta-side HTTP service can run over Ethernet or WiFi. Ethernet is the default build.

Ethernet build:

    pio run -e portenta_h7_m7

WiFi build:

Build the WiFi environment. PlatformIO will prompt for the WiFi SSID and password at compile time:

    pio run -e portenta_h7_m7_wifi

The password prompt is hidden, and the credentials are injected as compiler defines only for that build.

For a non-interactive shell, set environment variables before building:

    $env:PORTENTA_WIFI_SSID="YourWiFiSSID"
    $env:PORTENTA_WIFI_PASS="YourWiFiPassword"
    pio run -e portenta_h7_m7_wifi

You can also store those values in a local `.portenta_wifi.env` file at the repo root:

    PORTENTA_WIFI_SSID=YourWiFiSSID
    PORTENTA_WIFI_PASS=YourWiFiPassword

The HTTP API and `tools/esp32_uploader.py` usage are the same either way. Use the IP address printed by the Portenta serial log as `--host`. Ethernet still falls back to `192.168.1.177:8080` when DHCP is unavailable; WiFi requires successful association with the configured access point.

### Portenta WiFi Firmware and QSPI

The Portenta H7 WiFi stack loads Murata/Cypress firmware from the board's QSPI flash, not from this PlatformIO firmware image. If serial output says:

    Failed to mount the filesystem containing the WiFi firmware.
Usually that means that the WiFi firmware has not been installed yet or was overwritten with another firmware.

Restore the Arduino QSPI partition layout and WiFi firmware before uploading this project's WiFi build again:

1. In Arduino IDE, select `Portenta H7 (M7 core)`.
2. Run `File > Examples > STM32H747_System > QSPIFormat`.
3. Let it create the standard partitions and restore the WiFi firmware/certificates when prompted.
WARNING! Running the sketch all the content of the QSPI flash will be erased.
The following partitions will be created:

- Partition 1: WiFi firmware and certificates 1MB
- Partition 2: OTA 5MB
- Partition 3: Provisioning KVStore 1MB
- Partition 4: User data / OPTA PLC runtime 7MB

`Do you want to proceed? Y/[n]`

`Do you want to perform a full erase of the QSPI flash before proceeding? Y/[n]`  <--Enter Y twice to continue.

Note: Full flash erase can take up to one minute.

`Full erase started, please wait...`

`Flashing memory mapped WiFi firmware`

`Flashed 0%`

`. . .`

`Flashed 100%`

`Do you want to use LittleFS to format user data partition? Y/[n]` <--YES!

If No, FatFS will be used to format user partition.

Note: Arduino PLC IDE is using LittleFS to store runtime data on this partition.

`Use LittleFS for partition 4`.

For this project, partition 4 is just the Portenta's local staging area for uploaded ESP32 image chunks. We care about robustness during repeated writes, deletes, and possible interrupted uploads more than PC-readable compatibility. LittleFS is the better fit for that.

Do not choose FatFS for partition 4 unless you specifically want the user-data partition to be easier to inspect as a generic filesystem outside this firmware. For our remote ESP32 programmer, LittleFS is the right call.

    Formatting user partition with LittleFS.
    QSPI Flash formatted!
    It's now safe to reboot or disconnect your board.

4. Run `File > Examples > STM32H747_System > WiFiFirmwareUpdater`.

   Re-boot Portenta

   Answer prompt. 

   Scroll down in Serial Monitor:

   - `Firmware and certificates updated!`
   - `It's now safe to reboot or disconnect your board.`

6. Re-upload this project with:

    Enter DFU mode (double-press RST button on Portenta)

   `pio run -e portenta_h7_m7_wifi -t upload -v --upload-port COM12`

This project stages incoming ESP32 images on QSPI partition 4, the standard user-data partition. It must not format the raw QSPI chip, because partition 1 contains `/wlan/4343WA1.BIN`, which the Arduino WiFi library needs at boot.

## Update the Bootloader
Find the `STM32H747_manageBootloader` sketch in the Arduino IDE under 
    `File > Examples > STM32H747_System`
Upload sketch and answer questions in Serial Monitor.

    Magic Number (validation):  a0
    Bootloader version:         25
    Clock source:               External oscillator
    USB Speed:                  USB 2.0/Hi-Speed (480 Mbps)
    Has Ethernet:               Yes
    Has WiFi module:            Yes
    RAM size:                    8 MB
    QSPI size:                  16 MB
    Has Video output:           Yes
    Has Crypto chip:            Yes

## Compile and Upload

Use DFU mode on the Portenta for both Ethernet and WiFi firmware builds.
`C:\Users\<user>\.platformio\packages\tool-dfuutil\bin\dfu-util.exe -l`

returns:

    Found DFU: [2341:035b] ver=0200, devnum=30, cfg=1, intf=0, path="1-4.3", alt=3, name="@Arduino  boot  v.25   /0x00000000/0*4Kg", serial="0038002A3230510F31303431"
    Found DFU: [2341:035b] ver=0200, devnum=30, cfg=1, intf=0, path="1-4.3", alt=2, name="@Ext File Flash  0MB   /0x00000000/0*4Kg", serial="0038002A3230510F31303431"
    Found DFU: [2341:035b] ver=0200, devnum=30, cfg=1, intf=0, path="1-4.3", alt=1, name="@Ext RAW  Flash 16MB   /0x90000000/4096*4Kg", serial="0038002A3230510F31303431"
    Found DFU: [2341:035b] ver=0200, devnum=30, cfg=1, intf=0, path="1-4.3", alt=0, name="@Internal Flash  2MB   /0x08000000/01*128Ka,15*128Kg", serial="0038002A3230510F31303431"

This repo has two Portenta firmware environments in `platformio.ini`:

`portenta_h7_m7`: Ethernet HTTP transport. This is the default environment.

`portenta_h7_m7_wifi`: WiFi HTTP transport. This prompts for WiFi credentials at compile time, or reads them from environment variables.

### Shared setup:

Connect the Portenta to your PC over USB.

Open a terminal in the repo root:
`c:\Users\<user>\OneDrive\Documents\GitHub\Arduino\Portenta_ESP-EYE_Programmer`

Put the Portenta into bootloader mode if needed.
Usually this means double-pressing reset until the bootloader LED pattern changes.
COM11 changes to COM12 and the on-board pulses green.

### Runtime Network Behavior: Finding the Portenta IP Address

After uploading the Portenta firmware, open its serial monitor and reboot the
board:

`pio device monitor -p COM11 -b 115200`

Watch the startup log for the HTTP listener address, then pass that address to
`esp32_uploader.py` with `--host`:

- Ethernet prefers a DHCP-assigned address. If DHCP fails, use the static
  fallback `192.168.1.177` on port `8080`.
- WiFi prints its assigned address after joining the configured access point.
  WiFi does not have a static fallback.

#### Ethernet build and upload:

Build:
    `pio run -e portenta_h7_m7` # Means Ethernet, because default_envs = portenta_h7_m7.

Upload:
    `pio run -e portenta_h7_m7 -t upload -v --upload-port COM12`

Runtime network behavior:

- The Portenta uses the Vision Shield Ethernet path.
- It prefers DHCP.
- If DHCP is unavailable, it falls back to `192.168.1.177:8080`.

#### WiFi build and upload:

Interactive build:
`pio run -e portenta_h7_m7_wifi` # Means WiFi, because that environment adds PORTENTA_NETWORK_USE_WIFI=1 and runs the credential prompt script.

PlatformIO runs `tools/wifi_credentials.py` before compiling.
It first checks process environment variables, then `.portenta_wifi.env`, and only prompts if neither is set.
If environment variables are not set, the script asks for:
    Portenta WiFi SSID
    Portenta WiFi password

The password prompt is hidden.
The credentials are injected as compiler defines for that build and are not stored in platformio.ini.

Non-interactive PowerShell build:
- `$env:PORTENTA_WIFI_SSID=`"YourWiFiSSID"
- `$env:PORTENTA_WIFI_PASS=`"YourWiFiPassword"
- `pio run -e portenta_h7_m7_wifi`

Build & Upload:
    `pio run -e portenta_h7_m7_wifi -t upload -v --upload-port COM12`

Runtime network behavior:
The Portenta joins the configured WiFi network.
There is no static WiFi fallback; use the IP address printed in the serial monitor.

Optional serial monitor:

`pio device monitor -b 115200`

`pio device monitor -p COM11 -b 115200`

A few practical notes:
- Ethernet firmware ends up under `.pio\build\portenta_h7_m7\`.
- WiFi firmware ends up under `.pio\build\portenta_h7_m7_wifi\`.
- If upload can't find the board, the usual fix is to re-enter bootloader mode with the double reset and run the command again.
- **WiFi credentials are embedded in the compiled firmware binary, so do not share a WiFi-built `firmware.bin` that was compiled with real credentials.**

## ESP Upload Workflow

The workflow is:

* Flash this Portenta firmware onto the Portenta.
* Connect the Portenta to Ethernet or build the WiFi environment and let it join your access point.
* [Wire the Portenta](Pinouts.png) to the Arduino Nano ESP32 UART/EN/GPIO0 lines.
* [Find the Portenta IP address](#runtime-network-behavior-finding-the-portenta-ip-address) in its startup serial output, or use the Ethernet fallback `192.168.1.177:8080` if DHCP is unavailable.
* Run `tools/esp32_uploader.py` on your PC with the ESP image files you want staged and flashed.

```powershell
# Typical command
python tools/esp32_uploader.py `
  --host 192.168.1.177 `
  --port 8080 `
  --target arduino_nano_esp32 `
  --baud 460800 `
  --image bootloader:Blinky_1000ms.ino.bootloader.bin `
  --image partitions:Blinky_1000ms.ino.partitions.bin `
  --image app0:Blinky_1000ms.ino.bin
```

Our Test command:
NOTE: So relative paths are resolved from the folder where you run the Python command, and absolute paths work from anywhere.

Our compiled Blinky app that we will load on the Nano is here: `C:\Users\mmarc\AppData\Local\arduino\sketches\9ADE7FFE281152CE1367D725B05AA39B`

```powershell
# Generic Upload**
python tools/esp32_uploader.py `
    --host 192.168.1.177 `
    --port 8080 `
    --target arduino_nano_esp32 `
    --baud 460800 `
    --image bootloader:Blinky_1000ms.ino.bootloader.bin `
    --image partitions:Blinky_1000ms.ino.partitions.bin `
    --image app0:Blinky_1000ms.ino.bin

# This Repo: 3 Blinks: 1 long, then 2 short**
python tools/esp32_uploader.py `
    --host 192.168.1.178 --port 8080 `
    --target arduino_nano_esp32 `
    --baud 460800 `
    --image bootloader:.\images\9ADE7FFE281152CE1367D725B05AA39B\Blinky_1000ms.ino.bootloader.bin `
    --image partitions:.\images\9ADE7FFE281152CE1367D725B05AA39B\Blinky_1000ms.ino.partitions.bin `
    --image app0:.\images\9ADE7FFE281152CE1367D725B05AA39B\Blinky_1000ms.ino.bin

# This Repo: 3 Blinks: 3 short** 
python tools/esp32_uploader.py `
--host 192.168.1.178 `
--port 8080 `
--target arduino_nano_esp32 `
--baud 460800 `
--image bootloader:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.bootloader.bin `
--image partitions:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.partitions.bin `
--image app0:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.bin  
```
Or point to absolute path:
```powershell
# Upload with Absolute Paths
python tools/esp32_uploader.py `
    --host 192.168.1.178 --port 8080 --target arduino_nano_esp32 --baud 460800 `
    --image bootloader:C:\Users\mmarc\OneDrive\Documents\PlatformIO\Projects\260425-115203-arduino_nano_esp32\.pio\build\arduino_nano_esp32\bootloader.bin `
    --image partitions:C:\Users\mmarc\OneDrive\Documents\PlatformIO\Projects\260425-115203-arduino_nano_esp32\.pio\build\arduino_nano_esp32\partitions.bin `
    --image app0:C:\Users\mmarc\OneDrive\Documents\PlatformIO\Projects\260425-115203-arduino_nano_esp32\.pio\build\arduino_nano_esp32\firmware.bin
```
What the Python script does:

* POST /api/v1/session to create a session
* Uploads a manifest with image names, offsets, sizes, and SHA-256 digests
* Uploads each image in chunks to the Portenta's QSPI staging area
* Asks the Portenta to flash the ESP32
* Polls session status until completed or failed

What happens on the Portenta:

* Verifies staged files against the manifest sha256
* Resets the Nano ESP32 into ROM bootloader mode
* Syncs at 115200 and drains any extra ROM sync replies before flash commands
* Optionally loads the Espressif stub and erases flash if you pass `--erase`
* Writes each image to ESP flash
* Post-verifies each flashed region by comparing `staged-file MD5` vs `ESP-flash MD5`
* Leaves ROM mode and then hardware-resets the target back into normal boot
* Returns staged_md5, flash_md5, and flash_verified in final status

### Useful Variants: Full Erase and Resume Upload

#### Full erase first:

Use a full erase when old data elsewhere in ESP flash could affect the newly uploaded firmware. Typical scenarios include:

- Switching to a substantially different partition layout.
- Replacing firmware from another framework or project that used different flash regions.
- Clearing stale NVS settings, WiFi credentials, calibration values, filesystems, OTA slots, or crash data.
- Troubleshooting behavior that continues after the bootloader, partition table, and application images have been replaced.
- Preparing a known-clean device for a demonstration, validation run, or transfer to another user.

A full erase is normally unnecessary for routine updates that use the same partition layout. The regular upload already overwrites each specified image region and verifies its contents after flashing.

`--erase` is destructive: it removes all contents from the target ESP32 flash, including data partitions and settings that are not included in the upload command. Back up anything important first. It also takes longer and adds a full-chip erase cycle, so it should not be used automatically for every update.

The uploader requires a local `esptool` installation because it obtains the target-specific flasher stub and includes that stub in the manifest. The Portenta loads the stub into ESP RAM, performs the full-chip erase, then writes and verifies the requested images. Keep the target selection and image set correct: erase does not make firmware or partition layouts from different ESP32 families interchangeable.
```powershell
# Uploader Generic Command
python tools/esp32_uploader.py `
    --host 192.168.1.177 `
    --target arduino_nano_esp32 `
    --erase `
    --image bootloader:bootloader.bin `
    --image partitions:partitions.bin `
    --image app0:firmware.bin
```
#### Resume a partial upload:

Resume is useful when the manifest was accepted and only some image chunks reached the Portenta. Typical scenarios include:

- The uploader was stopped with `Ctrl+C` during `image chunk upload started...`.
- The uploader process or terminal closed unexpectedly.
- Ethernet or WiFi briefly disconnected during a large upload.
- An HTTP chunk request timed out while the Portenta remained powered and running.
- The PC slept, restarted, or changed networks, but the Portenta stayed powered and retained the active session.

Record the `sess-xxxxxxxx` value printed when the upload starts. Keep the Portenta powered and do not create another session. If the uploader is interrupted with `Ctrl+C`, detects a timeout, or loses the HTTP connection, it prints the complete PowerShell command to run when the Portenta is reachable again. After the manifest has been accepted, that command includes the actual Portenta-issued `--session-id` and the `--resume` argument automatically.

Values such as `sess-xxxxxxxx` and `sess-12345678` in the documentation are illustrative placeholders. The command printed by the uploader uses the real active session ID, for example `sess-006595d0`.

When resumed, the uploader verifies that the existing session manifest matches the local command, queries the received-chunk bitmap for each image, and sends only the missing chunks.

If the requested session returns `404 Unknown session`, the uploader queries `GET /api/v1/sessions` and prints every known session ID and state. The persistent PC simulator supports this listing. Current physical Portenta firmware tracks only one active in-memory session and does not expose the listing endpoint, so the uploader reports that known session IDs are unavailable when that firmware returns `404` for the discovery request.

The target, baud, chunk size, erase setting, image offsets, filenames, sizes, and SHA-256 hashes must match the original upload. In practice, use the identical command and add only `--session-id sess-xxxxxxxx --resume`.

Resume does not recover an interrupted ESP flash operation. If the flash request merely timed out, query the existing session status first because the Portenta may still be flashing or may already have completed. Also, the real Portenta currently keeps the active session and chunk bitmap in RAM: rebooting or power-cycling it, or creating a new session, prevents resuming the old upload even though partial staged bytes may remain in QSPI.
```powershell
# Resume an Upload
python tools/esp32_uploader.py `
    --host 192.168.1.177 `
    --target arduino_nano_esp32 `
    --session-id sess-12345678 `
    --resume `
    --image bootloader:bootloader.bin `
    --image partitions:partitions.bin `
    --image app0:firmware.bin

# Resume an Upload with Absolute Paths
python tools/esp32_uploader.py `
    --host 192.168.1.178 `
    --target arduino_nano_esp32 `
    --session-id sess-006595d0 `
    --resume `
    --image bootloader:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.bootloader.bin `
    --image partitions:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.partitions.bin `
    --image app0:.\images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.bin   
```  

NOTE: Depending on where yourun the Python, the Python output might look like:
  
    `session: sess-000050a6000ms.ino.bin;e1432d89-32ee-461d-bddd-c7e44b892584`
    
    `sess-000050a6 <--Actual session id is 8 chars after "sess-".`

The flash URL: `/api/v1/session/sess-000050a6/flash`

A few important notes:

`--target` defaults to arduino_nano_esp32. That target sends:

- manifest target esp32s3,
- uses 16 MB flash size,
- selects the esptool esp32s3 stub for  `--erase`, and
- resolves partition names from partitions_arduino_nano_esp32.csv.

That `partitions_arduino_nano_esp32.csv` file exists as a checked-in reference copy of the Arduino Nano ESP32 partition layout that this repo expects. The uploader uses it to map symbolic names such as `app0`, `app1`, `factory`, `ffat`, and `coredump` to the correct flash offsets for this specific board, instead of making you hard-code addresses by hand.

It is in the repo root on purpose so the flashing workflow stays self-contained and repeatable even when your generated build artifacts only give you `bootloader.bin`, `partitions.bin`, and the application image. The CSV is not flashed directly; it is metadata the uploader reads so it can interpret image names safely for the Nano ESP32 target.
  
You can use symbolic image offsets such as `bootloader`, `partitions`, `app0`, `app1`, `factory`, `ffat`, and `coredump`.
You can also still pass explicit numeric offsets such as `0x10000:firmware.bin`.

Different ESP32-family targets are not interchangeable.
The Arduino Nano ESP32 is an ESP32-S3 target, so this repo currently assumes ESP32-S3 ROM behavior when 
`--target arduino_nano_esp32` is selected.

Other targets may differ in `bootloader` `image offset`, `flash size`, `partition layout`, `stub selection`, and `ROM command` quirks.
Example: classic ESP32 commonly uses bootloader offset `0x1000`, while ESP32-S3 boards such as Arduino Nano ESP32 place the bootloader at `0x0`.

If you add another target, review all of these together:
- target manifest name
- default flash size
- fixed or CSV-derived offsets
- erase stub family
- whether ROM flash commands use legacy or extended parameter layouts
- whether ROM readback replies include extra reserved trailer bytes

`--erase` needs a local esptool install on the PC because the script pulls target-specific stub metadata from it.

Verify esptool is installed:

`python -m esptool version` # Returns: esptool v5.2.0

`esptool.py version`

Verify the stub files are available too, run:

`python -c "import importlib.util;`

`print(importlib.util.find_spec('esptool'))"`

If that prints a real module path, the uploader's `--erase` mode should be able to find the local esptool package.
    
Install esptools:
`python -m pip install esptool`

Use the exact Nano ESP32 files generated by Arduino IDE or Arduino CLI. The symbolic offsets help avoid copying addresses by hand, but they do not convert the CSV partition table into a .bin file.

The uploader is HTTP-based; your PC must be able to reach the Portenta over the network.

The final console output prints per-image flash verification results from the Portenta.

## Current Status

The original scaffold "next steps" have mostly been completed:

- Portenta pin mappings in `include/AppConfig.h` are now resolved to match the Hardware Mapping: `D20 PC3` for ESP32 EN and `D21 PA4` for ESP32 GPIO0.
- The HTTP transport binding is implemented on Portenta Ethernet or WiFi.
- The ESP ROM packet layer is implemented for ROM-mode flashing.
- A target-aware stub-loader erase path is implemented for true `erase: true` support when the PC uploader has local `esptool` stub files available.
- Staging has been moved from RAM into QSPI-backed files.
- Chunk uploads now validate indexes and support sparse or resumable transfer.
- Staged images are SHA-256 verified before the session enters `ready_to_flash`.
- Flashed images are post-verified against ESP flash contents using the bootloader's flash MD5 command.
- The uploader now depends on partitions_arduino_nano_esp32.csv for the Nano target.
- The full flash flow is now hardware-validated on Portenta H7 -> Arduino Nano ESP32, including ROM sync, SPI attach/setup, image writes, ESP flash

MD5 verification, and reboot back to normal boot.
- The ROM-mode implementation now explicitly handles the extra ESP32-S3 sync replies and the ESP32-S3 ROM response trailer format used by `FLASH_BEGIN` and `SPI_FLASH_MD5`.

## Remaining Work

The meaningful remaining gaps are:

1. Hardware-verify resumed uploads and baud-rate switching on the actual Portenta + Arduino Nano ESP32 wiring.
2. Hardware-verify the target-aware stub-assisted `--erase` flow on the actual Portenta + Arduino Nano ESP32 wiring.
3. Consider whether you want to persist the post-flash verification record beyond the live session status API, for example in QSPI logs or a downloadable audit artifact.
4. Create a solution to save WiFi credentials into an encrypted file and then have platformio.ini consume that encrypted file instead of saving credentials in plain open ASCII text. 

## License

Original project source and documentation are licensed under the
[MIT License](LICENSE). Third-party frameworks, libraries, tools, and code
incorporated into generated firmware remain under their respective licenses; see
[Third-Party Notices](THIRD_PARTY_NOTICES.md).
