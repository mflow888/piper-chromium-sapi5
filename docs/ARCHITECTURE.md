# Architecture

Piper SAPI5 is a Windows speech engine. Chromium browsers read voices from the SAPI5 OneCore voice list. This project registers Piper voices there.

## Shared engine, separate voice packages

Everything is installed in one directory, `C:\Program Files\Piper SAPI5`.

The engine is the same for every language:

- `PiperBassHighSAPI.dll` — 64-bit SAPI engine used by Chromium browsers
- `PiperBassHighSAPI32.dll` — 32-bit stub for older 32-bit programs
- `PiperBassHighHost.exe` — 64-bit synthesizer used only by the 32-bit stub
- `sherpa-onnx-c-api.dll`, ONNX Runtime, and `espeak-ng-data`

Each language package adds folders under `voices\`. A voice folder contains `model.onnx`, `tokens.txt`, and `voice.txt`. `voice.txt` stores the display name, gender, sample rate, English language name, Windows language id, and locale.

The engine files are installed as shared files. Windows reference-counts them. Installing German after Polish does not create a second engine. Uninstalling Polish decrements the count and deletes only the Polish voice folders. The engine files remain while any language package is still installed.

Registration does not use a fixed list of Polish voices. On install and on uninstall the engine scans `voices\`, deletes only the SAPI tokens that belong to this engine, and registers whatever models are still on disk. Each token's language id comes from that voice's `voice.txt`, so a German model is registered as German and a Polish model as Polish.

## Why browsers, not Narrator

Chromium on Windows uses SAPI voices registered under `Speech_OneCore` as well as the classic `Speech` key. This project writes both.

Narrator and a number of desktop programs use a different voice path, or they only accept voices that ship with Windows. Those programs may ignore Piper SAPI5. The project does not claim to fix that. The goal is Brave, Chrome, and the other Chromium browsers.

## Why speech used to start slowly

Older builds synthesized the whole request before sending any audio to Windows. Read-aloud waited until the entire selection existed as audio, and Stop waited for that work to finish.

The engine now does four things differently:

1. Text is split into short pieces, about a sentence or about 80 characters.
2. Each piece is written to the browser as soon as it is ready, so playback starts after the first piece instead of after the whole page.
3. Stop is checked between pieces. The current piece can also be dropped when Windows asks the engine to abort. The rest of the page is not synthesized.
4. Choosing a voice starts loading its model immediately, and the 64-bit library stays loaded in the browser process so the next read does not load the model from scratch.

The first load of a neural model is still not instant. That cost is the model itself. Later reads in the same browser session start from the already loaded model.

Rate and volume are read again for every piece, so a speed change applies on the next piece instead of only at the start of a long selection.

## 32-bit programs

The 32-bit library does not run the neural model itself. It sends text to `PiperBassHighHost.exe`. The host stays running and keeps the model loaded. The connection wait is short. Browsers do not use this path.
