# Architecture

Piper SAPI5 is a Windows speech engine. Chromium-based browsers read voices from the SAPI5 OneCore voice list. This project registers Piper voices there. The same registration is what TextAloud, Open WebUI, and other SAPI5 programs use.

## Shared engine, separate voice packages

Everything is installed in one directory, `C:\Program Files\Piper SAPI5`.

The engine is the same for every language:

- `PiperBassHighSAPI.dll` — 64-bit SAPI engine used by Chromium-based browsers
- `PiperBassHighSAPI32.dll` — 32-bit stub for older 32-bit programs
- `MFPiperHost.exe` — 64-bit synthesizer used only by the 32-bit stub
- `sherpa-onnx-c-api.dll`, ONNX Runtime, and `espeak-ng-data`

Each language package adds folders under `voices\`. A voice folder contains `model.onnx`, `tokens.txt`, and `voice.txt`. `voice.txt` stores the display name, gender, sample rate, English language name, Windows language id, and locale.

The engine files are installed as shared files. Windows reference-counts them. Installing German after Polish does not create a second engine. Uninstalling Polish decrements the count and deletes only the Polish voice folders. The engine files remain while any language package is still installed.

Registration does not use a fixed list of Polish voices. On install and on uninstall the engine scans `voices\`, deletes only the SAPI tokens that belong to this engine, and registers whatever models are still on disk. Each token's language id comes from that voice's `voice.txt`, so a German model is registered as German and a Polish model as Polish.

## Why browsers, not Narrator

Chromium-based browsers on Windows use SAPI voices registered under `Speech_OneCore` as well as the classic `Speech` key. This project writes both.

The goal is Google Chrome, Brave, Microsoft Edge, and other browsers built on Chromium. TextAloud and Open WebUI can select the same voices. Narrator uses its own voice list and may ignore Piper SAPI5.

## Speaking, stop, and highlighting

The engine speaks through the standard SAPI5 interface.

1. Text is split into short pieces. The first piece is only the first few words, so playback can start before the rest of the page is synthesized.
2. Audio is handed to Windows in small slices, only a fraction of a second ahead of playback. Stop ends the current slice and does not send the rest.
3. Each word and each sentence is reported with its character position and its place in the audio. Programs that highlight the current word use those SAPI5 events.
4. Choosing a voice loads its model immediately, and runs one silent warmup so the first real sentence does not pay the whole startup cost. The 64-bit library stays loaded in the browser process.

The first load of a neural model is still not instant. That cost is the model itself. Later reads in the same browser session start from the already loaded model.

Rate and volume are read again for every piece, so a speed change applies on the next piece instead of only at the start of a long selection.

## 32-bit programs

The 32-bit library does not run the neural model itself. It sends text to `MFPiperHost.exe`. The host stays running and keeps the model loaded. The connection wait is short. Browsers do not use this path.
