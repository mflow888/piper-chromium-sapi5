# Piper SAPI5

Offline [Piper](https://github.com/rhasspy/piper) neural voices for Windows, exposed through the SAPI5 speech interface.

The goal of this project is Piper voices in Chromium-based browsers: Google Chrome, Brave, Microsoft Edge, and other browsers built on Chromium.

The same voices also work in [TextAloud](https://nextup.com/TextAloud/), including word highlighting while the text is read. TextAloud is a product of NextUp Technologies. This project is not affiliated with NextUp Technologies.

They also work in [Open WebUI](https://github.com/open-webui/open-webui), and they may work in other programs that speak through Windows SAPI5. Narrator may keep using its own voices.

The voices run on this computer. Nothing is sent to a server at speech time.

## No malicious code

This project does not contain malicious code.

- It does not collect telemetry or personal data.
- It does not download anything while a voice is speaking.
- It does not install a browser extension.
- It does not add a program that starts when Windows starts.
- The only background process is `MFPiperHost.exe`, and only while a 32-bit program is speaking. Chromium-based browsers are 64-bit and speak inside the browser process.
- Installing a language package copies voice files, the shared speech engine, and SAPI voice registration. Uninstalling that package removes those voices.

The source in this repository is what the installers are built from.

## How installation works

Every language is its own installer, for example:

- `Piper-English-United-States-Language-1-Setup.exe`
- `Piper-English-United-States-Language-2-Setup.exe`
- `Piper-English-Great-Britain-Language-Setup.exe`
- `Piper-German-Language-Setup.exe`
- `Piper-French-Language-Setup.exe`
- `Piper-Spanish-Spain-Language-Setup.exe`

All of them install into the same folder:

`C:\Program Files\Piper SAPI5`

- The first language package installs the shared engine (SAPI libraries, Piper runtime, eSpeak data) and that language's voices.
- A later language package leaves the engine where it is and adds only its own voices.
- Each package has its own entry in Windows **Apps & features**. Uninstalling one English (United States) part removes that part's voices and leaves the other American English part, English (Great Britain), German, French, Spanish, or any other installed language in place.
- The shared engine is removed only when the last language package is uninstalled.

English (United States) is split into two installers because one file would be too large for a GitHub release asset. Both parts share the same engine and add different American English voices. Install either part, or both.

## Use the voices

1. Install the language package you want.
2. Quit the program completely, including a browser's tray icon, and open it again.
3. Pick the Piper voice in the speech settings, or use the program's read-aloud command.

In a Chromium-based browser, use the browser's read-aloud command. In TextAloud, choose the Piper voice and read; the current word is highlighted. In Open WebUI, select the installed Piper voice where that program lists Windows voices.

The voice list shows names such as **LESSAC MEDIUM**, **AMY MEDIUM**, and **ALBA MEDIUM**. The Windows language of each voice follows the Piper locale, so an American English voice is offered for English (United States) text and a British English voice for English (United Kingdom) text.

You can listen to samples of many of these languages on the [Piper samples](https://rhasspy.github.io/piper-samples/) page.

The first time a voice is used, Windows has to load the neural model. That still takes a short moment. After that, speech starts from the first few words, and Stop drops audio that has not been played yet. The engine reports each word and sentence to Windows, so a program that highlights text can follow the voice. The loaded voice stays in the browser process until the browser exits, so the next read starts sooner.

## Licenses

The engine source in `src` is [Apache License 2.0](LICENSE).

The installers also include other people's work. Full texts are in [`licenses`](licenses):

| Piece | License |
| --- | --- |
| sherpa-onnx | Apache License 2.0 |
| ONNX Runtime | MIT License |
| eSpeak NG data | GNU GPL 3.0 |
| Piper voice models | The license named in each model card (CC0, CC BY 4.0, Apache 2.0, or another license stated by the dataset) |

Model cards are in [`licenses/karty`](licenses/karty). A per-language index is in [docs/VOICES.md](docs/VOICES.md). Voices whose license requires credit are listed in [docs/ATTRIBUTIONS.md](docs/ATTRIBUTIONS.md).

## Built with Cursor

This project was written with the help of [Cursor](https://cursor.com).

## More documentation

- [Installing and removing voices](docs/INSTALL.md)
- [How the engine and language packages fit together](docs/ARCHITECTURE.md)
- [Building from source](docs/BUILDING.md)
- [Attributions](docs/ATTRIBUTIONS.md)
- [Security notes](SECURITY.md)

## 🔮 What's Next?

- 🔄 **Expand Compatibility**: Native support for more Windows TTS programs, such as Balabolka, DSpeech, NaturalReader, Panopreter, and ClaroRead.
- 🎧 **Enhance Fidelity**: Continuous improvements to audio rendering quality for an even more immersive experience.

## 🔗 Related Resources

For recommended browser TTS extensions (with local voice support), see the project page: https://healingtools4you.com/piper-voices-sapi5-for-chromium-brave-chrome-edge-under-windows/
