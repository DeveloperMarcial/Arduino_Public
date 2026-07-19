# Third-Party Notices

The repository's MIT License applies to the original project source and
documentation written for this project. It does not replace or modify the
licenses of the third-party frameworks, libraries, tools, or generated firmware
components listed below.

## ArduinoJson

- Project: ArduinoJson
- Copyright: Benoit Blanchon and contributors
- License: MIT
- Use: Build dependency for the Portenta firmware, declared in `platformio.ini`
- Source and license: https://github.com/bblanchon/ArduinoJson

ArduinoJson is downloaded by PlatformIO and is not vendored in this repository.

## ArduinoCore-mbed and Mbed OS Components

- Project: ArduinoCore-mbed
- Copyright: Arduino and contributors
- Use: Arduino framework and board support for the Portenta H7 firmware
- Source and component license information:
  https://github.com/arduino/ArduinoCore-mbed

The framework is downloaded by PlatformIO and is not vendored in this
repository. ArduinoCore-mbed packages code from multiple upstream components;
their individual copyright notices and license terms remain applicable.

## Arduino Core for ESP32

- Project: Arduino core for the ESP32
- Copyright: Espressif Systems and contributors
- License: GNU Lesser General Public License v2.1 or later
- Use: Arduino framework and board support used to build the Nano ESP32 examples
  and the demonstration firmware binaries under `images/`
- Source: https://github.com/espressif/arduino-esp32
- License: https://github.com/espressif/arduino-esp32/blob/master/LICENSE.md

The Arduino-ESP32 framework source is available from the upstream repository
linked above. The Arduino-ESP32 license continues to apply to its code
incorporated into the generated firmware binaries.

## PlatformIO and Build Tools

PlatformIO, esptool, compilers, upload utilities, and board packages are external
development tools. They are installed separately and remain subject to their
respective upstream licenses. They are not relicensed under this project's MIT
License.
