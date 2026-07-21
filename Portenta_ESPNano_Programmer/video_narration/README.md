# Narration

The uploader can play six WAV cues that explain the Codex/GPT-5.6 collaboration
and the upload and flash workflow.
The narration is synthetic and generated locally with Piper. Source text lives
in UTF-8 `VoiceText.txt` under the labeled sections `#VOICE #0`, `#VOICE #1`,
`#VOICE #2`, `#VOICE #2a`, `#VOICE #3`, and `#VOICE #4`.

## Active Files

`tools/esp32_uploader.py` plays these exact filenames:

| Cue | Active filename |
| --- | --- |
| Codex and GPT-5.6 introduction | `Male_Voice00_codex_and_gpt.wav` |
| Setup | `Male_Voice01_setup_and_command.wav` |
| Upload and staging | `Male_Voice02_uploader_and_staging.wav` |
| Chunk transfer and resume | `Male_Voice02a_chunk_resume_and_verification.wav` |
| Flash start | `Male_Voice03_start_flash.wav` |
| Verified completion | `Male_Voice04_flash_done..wav` |

## Generate Male Voices

The 133 MB `en_US-libritts-high.onnx` model and its JSON configuration are not
stored in GitHub. The generator downloads both files from the Piper voice-model
repository when they are absent and verifies their SHA-256 hashes before use.

From the repository root on Windows, create a Python virtual environment,
install Piper, and generate the six narration files:

```powershell
python -m venv .venv-piper
.\.venv-piper\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install piper-tts
$env:PIPER_PYTHON = (Resolve-Path .\.venv-piper\Scripts\python.exe)
.\video_narration\Generate_Male_Voices.bat
```

The first generation downloads these exact files:

* [en_US-libritts-high.onnx](https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/libritts/high/en_US-libritts-high.onnx?download=true)
  into `video_narration/en_US-libritts-high.onnx`
* [en_US-libritts-high.onnx.json](https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/libritts/high/en_US-libritts-high.onnx.json?download=true)
  into `video_narration/en_US-libritts-high.onnx.json`

Expected SHA-256 values:

```text
9127a559e11603f10b366d1a20ac7426826081dbc521de4c2420c57728d73f0f  en_US-libritts-high.onnx
2efdc6d7f954588b8180132cbd9b8001933fdd00932c92bc92fd0d2028a9eb3d  en_US-libritts-high.onnx.json
```

To use an existing Piper environment instead, set `PIPER_PYTHON` to that
environment's Python executable before running the batch file:

```powershell
$env:PIPER_PYTHON = "C:\path\to\venv\Scripts\python.exe"
.\video_narration\Generate_Male_Voices.bat
```

The batch generates each section separately and pauses after every completed
file. It uses U.S. English LibriTTS speaker `p4535`, prefix `Male_`,
`noise_scale=0.3`, and `noise_w_scale=0.3`.

To regenerate only Male Voice #1:

```powershell
& $env:PIPER_PYTHON .\video_narration\Voice_en_US-libritts-high.py `
    --speaker p4535 --prefix Male_ --section 1 `
    --noise-scale 0.3 --noise-w-scale 0.3
```

Lower `--noise-scale` values flatten overall voice variation. Lower
`--noise-w-scale` values make phoneme timing steadier. `--length-scale` changes
speaking duration; `1.0` is the model's normal speed.

The generator downloads and SHA-256-validates the 133 MB
`en_US-libritts-high` model when it is absent. ONNX model files are excluded
from Git and judge exports because they exceed GitHub's normal file-size limit.

## Licensing

The narration was synthesized using Piper and the `en_US-libritts-high` model;
no source reader participated in or endorsed this project.

Piper is an external GPLv3 generation tool and is not vendored here. The model
was trained from scratch on LibriTTS `train-clean-360`, licensed under Creative
Commons Attribution 4.0. LibriVox states that its source recordings are public
domain in the United States. See the bundled model card and
`../THIRD_PARTY_NOTICES.md` for attribution and source links.
