# Security

Piper SAPI5 does not contain malicious code.

What the installers do:

- Copy the speech engine and the selected voice models into `C:\Program Files\Piper SAPI5`.
- Register those voices with Windows SAPI5 (both the classic Speech key and the Speech_OneCore key that Chromium browsers read).
- Add a Start menu shortcut that speaks one test sentence with the installed voice.

What they do not do:

- They do not collect telemetry, crash reports, or personal data.
- They do not contact the network while a voice is speaking. Downloads happen only when you build installers from source.
- They do not install a browser extension or change browser settings.
- They do not add a Run key, a scheduled task, or any other program that starts with Windows.
- They do not close your browser. The installer does not force other applications to shut down.
- They do not remove voices from a different language package.

The 64-bit speech library stays loaded inside the browser process after the first use of a voice, so the next read does not have to load the neural model again. That cache ends when the browser process exits. It is not a separate startup program.

`MFPiperHost.exe` is started only when a 32-bit program asks a Piper voice to speak. Chromium browsers are 64-bit and do not need that host. The host is not registered to start with Windows.

Voice models are the published Piper ONNX files (and the prepared Polish BASS HIGH model). They are data for the speech engine, not programs.

If you build the installers yourself, the build downloads voice files from the public Hugging Face repository `rhasspy/piper-voices`. That download is part of building, not part of speaking.
