# Publishing on GitHub

Publish the source from the `github` folder. Publish the installers as a GitHub Release. Do not put the installers or the ONNX models in the git repository. Voice models are large, and GitHub rejects files over 100 MB in a git push. Release assets can be up to 2 GB each, which is why English (United States) is split into two installers.

## Repository

1. Create a new public repository, for example `piper-sapi5`. Do not initialize it with a generated license file if you are about to push this project's `LICENSE`.
2. The files that belong in the repository are the contents of the `github` folder in this workspace. That folder is the repository root. It is not a subfolder named `github` on GitHub.
3. Copy those contents into a clean clone and push them:

```
git clone https://github.com/<your-account>/piper-sapi5.git
xcopy /E /I github piper-sapi5
cd piper-sapi5
git add .
git commit -m "Publish Piper SAPI5 browser voices."
git push origin main
```

Use your real account and the default branch name GitHub created (`main` or `master`).

Do not copy `payload`, `dist`, `build`, `third_party/sherpa`, `third_party/espeak`, or `*.onnx` into the repository.

## About text

Paste this into the repository **About** description:

```
Offline Piper neural voices for Chromium browsers on Windows (Brave, Chrome, Edge). Narrator and many Windows programs may not use them. The goal is SAPI5 Piper voices in the browser.
```

Suggested topics: `tts`, `piper`, `sapi5`, `chromium`, `brave`, `accessibility`, `windows`

The website field can be left empty, or set to `https://cursor.com` only if you want to point at the tool used to write the project. The README already credits Cursor.

## Release

1. Build the installers with `build.ps1 -AllLanguages` (see [BUILDING.md](BUILDING.md)).
2. On GitHub, open **Releases** and choose **Draft a new release**.
3. Tag it `v1.3.0`. Title: `Piper SAPI5 1.3.0`.
4. Upload every `dist\Piper-*-Language-Setup.exe` file as a release asset. Upload them here, not with **Add file** on the repository front page. The front page is for the source tree. The release page is for the installers.
5. Release notes can point at the README and say which license applies. A short note is enough:

```
Language installers for Piper SAPI5 1.3.0.
Install any language. Later languages add voices and leave the shared engine in place.
Each language can be uninstalled on its own.
Brave, Chrome, Edge, and other Chromium browsers are the supported players.
Narrator and many Windows programs may not use these voices.
The packages do not contain malicious code.
English (United States) is split into part 1 and part 2.
```

6. Publish the release.

GitHub will show the installers on the release page. The README should tell people to download them from the latest release rather than from the source tree.

## What else to set

- **License**: GitHub detects `LICENSE` (Apache 2.0). Leave that file as the repository license. Third-party licenses stay in `licenses/` and are explained in `NOTICE`.
- **Issues**: turn issues on if you want bug reports about browser playback.
- Do not attach the old `PiperBassHigh-SAPI5-Setup.exe`. The replacement is `Piper-Polish-Language-Setup.exe`.
- After you push, check that the repository does not contain `.onnx` files or `.exe` installers. Those belong only on the release.

## Refreshing the github folder

From the project root:

```
powershell -ExecutionPolicy Bypass -File export_github.ps1
```

That recopies the source, docs, and licenses into `github\`. Review that folder, then copy it to the git clone as above.
