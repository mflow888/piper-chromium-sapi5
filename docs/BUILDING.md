# Building

Build on 64-bit Windows 10 or 11.

You need:

- Python 3 with the `onnx` package
- Visual Studio 2022 Build Tools, including the C++ workload (x86 and x64)
- Inno Setup 6

The speech runtime is not committed. Before the first build, unpack [sherpa-onnx 1.13.7 win-x64 shared MD Release](https://github.com/k2-fsa/sherpa-onnx/releases) so this folder exists:

`third_party/sherpa/sherpa-onnx-v1.13.7-win-x64-shared-MD-Release`

Unpack eSpeak NG data so this folder exists:

`third_party/espeak/espeak-ng-data`

Voice models are downloaded from `rhasspy/piper-voices` on Hugging Face when a language is prepared. The Polish BASS HIGH model is used from `third_party/pl_PL-bass-high.onnx` when that file is already present.

## One language

Polish is the default:

```
powershell -ExecutionPolicy Bypass -File build.ps1 -Language pl_PL
```

The installer is written to:

`dist2\Piper-Polish-Language-Setup.exe`

Another language uses its Piper locale code:

```
powershell -ExecutionPolicy Bypass -File build.ps1 -Language de_DE
```

## Every language

This downloads the full Piper voice catalog (about 12 GB) and compiles one installer per language. English (United States) becomes two installers.

```
powershell -ExecutionPolicy Bypass -File build.ps1 -AllLanguages
```

Expect a long download and a long compression step. The scripts skip voice files that are already on disk, so you can stop and run the same command again.

To package installers again without recompiling the C++ engine:

```
powershell -ExecutionPolicy Bypass -File build.ps1 -AllLanguages -PackageOnly
```

`-PackageOnly` still prepares any missing voice files, then runs Inno Setup.

## What the scripts do

`prepare_voice.py` downloads a voice, writes `tokens.txt` and `voice.txt`, and stores the ONNX model in a form sherpa-onnx can load.

`package_languages.py` writes an English Inno Setup script for each language pack and compiles `dist2\Piper-<Language>-Language-Setup.exe`.

The generated scripts are under `build\iss\`. That directory is a build output, not something to commit.

## Internal file names

The installer file names are language names. The SAPI libraries inside the install folder keep the historical name PiperBassHigh: `PiperBassHighSAPI.dll` and `PiperBassHighSAPI32.dll`. That name is the engine's COM identity. Language packages share those libraries on purpose so a second language does not register a second engine. The 32-bit synthesizer process is `MFPiperHost.exe`.
