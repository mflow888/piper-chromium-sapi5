# Install and uninstall

Installers are named like the language they add:

- `Piper-English-United-States-Language-1-Setup.exe`
- `Piper-English-United-States-Language-2-Setup.exe`
- `Piper-English-Great-Britain-Language-Setup.exe`
- `Piper-German-Language-Setup.exe`
- `Piper-French-Language-Setup.exe`
- `Piper-Spanish-Spain-Language-Setup.exe`

English (United States) is two files, part 1 and part 2, because a single file would be too large to attach to a GitHub release. Install either part, or both. They add different American English voices and share one engine.

Other languages, including English (Great Britain), German, French, and Spanish (Spain), are one installer each. The full list is in [VOICES.md](VOICES.md).

## Requirements

- Windows 10 or Windows 11, 64-bit
- An administrator account, because voice registration is per machine

## Install a language

1. Run the language setup you want. The wizard is in English and shows the license before it copies files.
2. Leave the voices you want selected. Clearing a voice omits that model.
3. The files go to `C:\Program Files\Piper SAPI5`. The folder is fixed so every language package finds the same engine.

The first package installs the engine and its voices. The next package sees the engine already there, leaves those engine files in place, and adds only the new voice folders.

## Use the voice in Brave or Chrome

1. Finish the setup.
2. Quit the browser completely. Check the system tray as well, then open the browser again.
3. Use the browser's read-aloud feature, or choose the Piper voice in the browser's speech settings.

Narrator, and many classic Windows programs, may not list or speak these voices. The supported path is a Chromium browser (Brave, Chrome, Edge, and other Chromium browsers).

## What the names look like

Voices use the Piper speaker and quality, for example:

- LESSAC MEDIUM and AMY MEDIUM (English, United States)
- ALBA MEDIUM and ALAN MEDIUM (English, Great Britain)
- THORSTEN HIGH (German)
- SIWIS MEDIUM (French)
- SHARVARD MEDIUM (Spanish, Spain)

If the same label exists in more than one language, the language name is added in parentheses, for example MLS MEDIUM (GERMAN) and MLS MEDIUM (FRENCH).

## Uninstall one language

Open **Settings > Apps** and uninstall the language package, for example "Piper English (United States) Language (part 1 of 2)" or "Piper English (Great Britain) Language".

That removes only that package's voice folders and refreshes the voice list. Other languages stay. The engine stays too, until you uninstall the last language package. The last uninstall also unregisters the SAPI engine and deletes the shared files.

You can also run the uninstall shortcut in the **Piper SAPI5** Start menu folder.

## Test a voice

Each package adds a **Test** shortcut in the Start menu. It speaks one short sentence with the first voice in that package. The shortcut uses 64-bit PowerShell.

## Where the licenses are

After installation, open `C:\Program Files\Piper SAPI5\licenses`. The wizard shows the same texts before you accept the install. `NOTICES.txt` in the install folder is a short index.
