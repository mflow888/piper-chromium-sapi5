"""Build one Inno Setup installer per Piper language pack."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

from piper_catalog import ROOT, VERSION, Pack, Voice, build_packs, find_pack

PAYLOAD = ROOT / "payload"
LICENSES = ROOT / "licenses"
INSTALLER = ROOT / "installer"
DIST = ROOT / "dist"
ISS_ROOT = ROOT / "build" / "iss"


def iss_quote(text: str) -> str:
    return text.replace('"', "'")


def write_texts(folder: Path, pack: Pack) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    voices = "\n".join(f"- {voice.display_name}" for voice in pack.voices)
    about = f"""{pack.title}
Version {VERSION}

This package adds Piper neural voices to Windows so Chromium browsers can speak with them.
Brave, Chrome, Microsoft Edge, and other Chromium-based browsers are the target.

The same voices are registered with the Windows SAPI5 speech interface. Narrator and many ordinary Windows programs may not use them. That limitation is expected. The project was made so browsers can select these SAPI5 voices.

This software does not contain malicious code. It does not collect telemetry, it does not download anything while it is running, and it does not start with Windows. The synthesis host runs only while a program is speaking.

If the Piper SAPI5 engine is already installed, this setup leaves that engine in place and adds only the voices in this package. Each language package has its own uninstall entry and removes only its own voices.

Voices in this package:

{voices}

This project was written with the help of Cursor (https://cursor.com).
"""
    after = f"""The voices from this package are now registered.

Open Brave, Chrome, Edge, or another Chromium browser and read a page aloud, or pick the voice in the browser's speech settings. Quit the browser completely, including its tray icon, and open it again if the new voice is not listed yet.

Narrator and many Windows desktop programs may ignore these voices. Browser playback is the supported use.

The shared Piper SAPI5 engine stays installed when you add another language later. Uninstalling this package removes only these voices:

{voices}

The engine itself is removed only when the last language package is uninstalled.

Licenses are in the licenses folder of the installation. A Test voice shortcut was added to the Piper SAPI5 folder in the Start menu.

This software does not contain malicious code.
"""
    (folder / "about.txt").write_text(about, encoding="utf-8", newline="\r\n")
    (folder / "after.txt").write_text(after, encoding="utf-8", newline="\r\n")
    parts = [
        about,
        "",
        "By installing this package you accept the licenses below.",
        "The same texts are copied into the licenses folder.",
        "",
        "There is no malicious code in this package: no telemetry, no remote control,",
        "no browser extension, and no program that starts when Windows starts.",
        "",
        "The engine source in this project is Apache License 2.0.",
        "sherpa-onnx is Apache License 2.0.",
        "ONNX Runtime is the MIT License.",
        "eSpeak NG data is GNU General Public License version 3.",
        "Voice models keep the license of their training data, stated in each model card.",
        "",
        "This project was written with the help of Cursor (https://cursor.com).",
        "",
    ]
    for name in ("Apache-2.0.txt", "MIT.txt", "GPL-3.0.txt", "CC0-1.0.txt", "CC-BY-4.0.txt"):
        path = LICENSES / name
        parts.append("=" * 72)
        parts.append(name)
        parts.append("=" * 72)
        parts.append(path.read_text(encoding="utf-8"))
        parts.append("")
    parts.append("=" * 72)
    parts.append("Voice model cards")
    parts.append("=" * 72)
    parts.append("")
    for voice in pack.voices:
        card = LICENSES / "karty" / f"{voice.voice_id}.txt"
        parts.append(voice.display_name)
        parts.append("-" * len(voice.display_name))
        if card.exists():
            parts.append(card.read_text(encoding="utf-8"))
        elif voice.voice_id == "pl_PL-bass-high":
            parts.append(
                "Polish Piper voice, high quality, 22050 Hz.\n"
                "Fine-tuned from the Lessac high model.\n"
                "Base model: Apache License 2.0."
            )
        else:
            parts.append("See the model card published with this Piper voice on Hugging Face, rhasspy/piper-voices.")
        parts.append("")
    (folder / "license.txt").write_text("\n".join(parts), encoding="utf-8", newline="\r\n")


def component_block(voices: list[Voice]) -> str:
    lines = []
    for index, voice in enumerate(voices):
        description = iss_quote(f"{voice.display_name} — {voice.gender}")
        lines.append(f'Name: "v{index}"; Description: "{description}"; Types: full')
    return "\n".join(lines)


def file_block(voices: list[Voice]) -> str:
    lines = []
    for index, voice in enumerate(voices):
        lines.append(
            f'Source: "..\\..\\..\\payload\\voices\\{voice.voice_id}\\*"; '
            f'DestDir: "{{app}}\\voices\\{voice.voice_id}"; Components: v{index}; '
            "Flags: recursesubdirs ignoreversion"
        )
    return "\n".join(lines)


def pascal_remove(voices: list[Voice]) -> str:
    lines = ["procedure RemovePackVoices;", "begin"]
    for voice in voices:
        lines.append(f"  DelTree(ExpandConstant('{{app}}\\voices\\{voice.voice_id}'), True, True, True);")
    lines.append("end;")
    lines.append("")
    lines.append("procedure RemoveUnselectedVoices;")
    lines.append("begin")
    for index, voice in enumerate(voices):
        lines.append(f"  if not WizardIsComponentSelected('v{index}') then")
        lines.append(f"    DelTree(ExpandConstant('{{app}}\\voices\\{voice.voice_id}'), True, True, True);")
    lines.append("end;")
    return "\n".join(lines)


def write_iss(folder: Path, pack: Pack) -> Path:
    voice_name = iss_quote(pack.voices[0].display_name)
    sample = iss_quote(pack.sample)
    icon_params = (
        '"-NoProfile -ExecutionPolicy Bypass -File ""{app}\\Test-Voice.ps1"" '
        f'-VoiceName ""{voice_name}"" -Sample ""{sample}"""'
    )
    iss = f"""; UTF-8
#define MyAppName "{iss_quote(pack.title)}"
#define MyAppVersion "{VERSION}"
#define MyAppPublisher "Piper SAPI5"

[Setup]
AppId={pack.app_id}
AppName={{#MyAppName}}
AppVersion={{#MyAppVersion}}
AppPublisher={{#MyAppPublisher}}
AppVerName={{#MyAppName}} {{#MyAppVersion}}
DefaultDirName={{autopf}}\\Piper SAPI5
DisableDirPage=yes
DefaultGroupName=Piper SAPI5
DisableProgramGroupPage=yes
OutputDir=..\\..\\..\\dist
OutputBaseFilename={pack.setup_name}
Compression=lzma2
SolidCompression=no
WizardStyle=modern
LicenseFile=license.txt
InfoBeforeFile=about.txt
InfoAfterFile=after.txt
AlwaysShowComponentsList=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=no
RestartApplications=no
MinVersion=10.0
UninstallDisplayIcon={{app}}\\PiperBassHighSAPI.dll
VersionInfoVersion={VERSION}.0
VersionInfoDescription={iss_quote(pack.title)}
VersionInfoProductName=Piper SAPI5
ShowLanguageDialog=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "All voices in this package"
Name: "custom"; Description: "Selected voices"; Flags: iscustom

[Components]
{component_block(pack.voices)}

[Files]
Source: "..\\..\\..\\payload\\PiperBassHighSAPI.dll"; DestDir: "{{app}}"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\payload\\PiperBassHighSAPI32.dll"; DestDir: "{{app}}"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\payload\\PiperBassHighHost.exe"; DestDir: "{{app}}"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\payload\\sherpa-onnx-c-api.dll"; DestDir: "{{app}}"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\payload\\onnxruntime.dll"; DestDir: "{{app}}"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\payload\\onnxruntime_providers_shared.dll"; DestDir: "{{app}}"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\payload\\vcruntime140.dll"; DestDir: "{{app}}"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\payload\\vcruntime140_1.dll"; DestDir: "{{app}}"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\payload\\msvcp140.dll"; DestDir: "{{app}}"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\payload\\msvcp140_1.dll"; DestDir: "{{app}}"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\payload\\engine.version"; DestDir: "{{app}}"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\payload\\espeak-ng-data\\*"; DestDir: "{{app}}\\espeak-ng-data"; Flags: recursesubdirs ignoreversion sharedfile
Source: "..\\..\\..\\licenses\\Apache-2.0.txt"; DestDir: "{{app}}\\licenses"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\licenses\\MIT.txt"; DestDir: "{{app}}\\licenses"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\licenses\\GPL-3.0.txt"; DestDir: "{{app}}\\licenses"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\licenses\\CC0-1.0.txt"; DestDir: "{{app}}\\licenses"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\licenses\\CC-BY-4.0.txt"; DestDir: "{{app}}\\licenses"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\installer\\NOTICES.txt"; DestDir: "{{app}}"; Flags: ignoreversion sharedfile
Source: "..\\..\\..\\installer\\Test-Voice.ps1"; DestDir: "{{app}}"; Flags: ignoreversion sharedfile
{file_block(pack.voices)}

[Icons]
Name: "{{autoprograms}}\\Piper SAPI5\\Test {iss_quote(pack.title)}"; Filename: "{{sys}}\\WindowsPowerShell\\v1.0\\powershell.exe"; Parameters: {icon_params}; WorkingDir: "{{app}}"
Name: "{{autoprograms}}\\Piper SAPI5\\Uninstall {iss_quote(pack.title)}"; Filename: "{{uninstallexe}}"

[Code]
function AnyVoiceLeft: Boolean;
var
  FindRec: TFindRec;
begin
  Result := False;
  if FindFirst(ExpandConstant('{{app}}\\voices\\*'), FindRec) then
  try
    repeat
      if ((FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0) and
         (FindRec.Name <> '.') and (FindRec.Name <> '..') and
         FileExists(ExpandConstant('{{app}}\\voices\\') + FindRec.Name + '\\model.onnx') then
      begin
        Result := True;
        Exit;
      end;
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

procedure RegisterVoices;
var
  ResultCode: Integer;
begin
  if not FileExists(ExpandConstant('{{app}}\\PiperBassHighSAPI.dll')) then
    Exit;
  if (not Exec(ExpandConstant('{{sys}}\\regsvr32.exe'), '/s "' + ExpandConstant('{{app}}\\PiperBassHighSAPI.dll') + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then
    SuppressibleMsgBox('Could not register the 64-bit voices. Code: ' + IntToStr(ResultCode), mbError, MB_OK, IDOK);
  if (not Exec(ExpandConstant('{{syswow64}}\\regsvr32.exe'), '/s "' + ExpandConstant('{{app}}\\PiperBassHighSAPI32.dll') + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then
    SuppressibleMsgBox('Could not register the 32-bit voices. Code: ' + IntToStr(ResultCode), mbError, MB_OK, IDOK);
end;

procedure UnregisterVoices;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{{syswow64}}\\regsvr32.exe'), '/s /u "' + ExpandConstant('{{app}}\\PiperBassHighSAPI32.dll') + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{{sys}}\\regsvr32.exe'), '/s /u "' + ExpandConstant('{{app}}\\PiperBassHighSAPI.dll') + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

{pascal_remove(pack.voices)}

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{{sys}}\\taskkill.exe'), '/F /IM PiperBassHighHost.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := '';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssPostInstall then
    Exit;
  RemoveUnselectedVoices;
  RegisterVoices;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep <> usUninstall then
    Exit;
  Exec(ExpandConstant('{{sys}}\\taskkill.exe'), '/F /IM PiperBassHighHost.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  RemovePackVoices;
  if AnyVoiceLeft then
    RegisterVoices
  else
    UnregisterVoices;
end;
"""
    # The f-string doubled braces for Inno constants. Pascal already uses single braces
    # from the helper functions. Fix the double-brace leftovers that were meant to be single.
    iss = iss.replace("{{", "{").replace("}}", "}")
    iss = iss.replace("AppId=" + pack.app_id, "AppId={" + pack.app_id + "}", 1)
    path = folder / "setup.iss"
    path.write_text(iss, encoding="utf-8-sig", newline="\r\n")
    return path


def find_iscc() -> Path:
    candidates = [
        Path.home() / "AppData" / "Local" / "Programs" / "Inno Setup 6" / "ISCC.exe",
        Path(r"C:\Program Files (x86)\Inno Setup 6\ISCC.exe"),
        Path(r"C:\Program Files\Inno Setup 6\ISCC.exe"),
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise SystemExit("Inno Setup 6 was not found.")


def package(pack: Pack) -> Path:
    missing = [voice.voice_id for voice in pack.voices if not (PAYLOAD / "voices" / voice.voice_id / "model.onnx").exists()]
    if missing:
        raise SystemExit("Missing prepared voices: " + ", ".join(missing))
    folder = ISS_ROOT / pack.pack_id
    write_texts(folder, pack)
    iss = write_iss(folder, pack)
    DIST.mkdir(parents=True, exist_ok=True)
    print("compiling", pack.setup_name)
    subprocess.run([str(find_iscc()), str(iss)], check=True)
    setup = DIST / f"{pack.setup_name}.exe"
    if not setup.exists():
        raise SystemExit(f"Installer was not created: {setup}")
    print("created", setup)
    return setup


def main() -> None:
    parser = argparse.ArgumentParser(description="Build Piper language installers.")
    parser.add_argument("--language", help="Locale code, for example pl_PL")
    parser.add_argument("--all", action="store_true", help="Build every language pack")
    args = parser.parse_args()
    packs = build_packs()
    if args.all:
        chosen = packs
    else:
        chosen = find_pack(args.language or "pl_PL", packs)
    for pack in chosen:
        package(pack)


if __name__ == "__main__":
    main()
