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

## Piper and LibriTTS Narration

- Project: Piper text-to-speech engine
- Copyright: Open Home Foundation and Piper contributors
- License: GNU General Public License v3.0
- Use: External development tool used to generate the WAV narration files
- Source and license: https://github.com/OHF-Voice/piper1-gpl

Piper is installed separately and is not vendored in this repository.

- Voice model: `en_US-libritts-high`
- Model repository: https://huggingface.co/rhasspy/piper-voices
- Selected male model speaker: `p4535` (source reader: Brett W. Downey)
- Training dataset: LibriTTS, trained from scratch on `train-clean-360`
- Dataset authors: Heiga Zen, Viet Dang, Rob Clark, Yu Zhang, Ron J. Weiss,
  Ye Jia, Zhifeng Chen, and Yonghui Wu
- Dataset license: Creative Commons Attribution 4.0 International
- Dataset source: https://www.openslr.org/60/
- License: https://creativecommons.org/licenses/by/4.0/

The generated WAV narration files use the LibriTTS high-quality Piper voice.
The model card is included under `video_narration/`. The 137 MB ONNX model is
downloaded by the generation script and intentionally excluded from Git.

The narration is synthetic. Naming a source reader documents model provenance
and does not state or imply that the reader recorded, participated in, or
endorsed this project. LibriVox states that its recordings are public domain in
the United States:
https://librivox.org/pages/public-domain/
