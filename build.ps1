param(
    [string]$Language = "pl_PL",
    [switch]$AllLanguages,
    [switch]$PackageOnly,
    [string]$DistDir = "dist2"
)

$env:PYTHONIOENCODING = "utf-8"
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

Write-Host "Preparing voice payload..."
$prepareArgs = @((Join-Path $root "prepare_voice.py"))
if ($AllLanguages) {
    $prepareArgs += "--all"
} else {
    $prepareArgs += @("--language", $Language)
}
python @prepareArgs
if ($LASTEXITCODE -ne 0) {
    throw "Voice preparation failed with exit code $LASTEXITCODE"
}

if ($PackageOnly) {
    $packageArgs = @((Join-Path $root "package_languages.py"))
    if ($AllLanguages) {
        $packageArgs += "--all"
    } else {
        $packageArgs += @("--language", $Language)
    }
    $packageArgs += @("--dist", (Join-Path $root $DistDir))
    python @packageArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Installer packaging failed with exit code $LASTEXITCODE"
    }
    Write-Host "Build finished."
    exit 0
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere was not found. Install Visual Studio 2022 Build Tools with the C++ workload."
}
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) {
    throw "MSVC x64 tools were not found."
}
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$include = Join-Path $root "third_party\sherpa\sherpa-onnx-v1.13.7-win-x64-shared-MD-Release\include"
$build = Join-Path $root "build"
$testObj = Join-Path $build "test"
New-Item -ItemType Directory -Force -Path $build, $testObj, (Join-Path $root "payload") | Out-Null

$cmd = @"
set "PATH=C:\Windows\System32;C:\Windows"
call "$vcvars"
if errorlevel 1 exit /b 1
cd /d "$root"
if errorlevel 1 exit /b 1
rc /nologo /fo "$build\version.res" src\version.rc
if errorlevel 1 exit /b 1
rc /nologo /fo "$build\host_version.res" src\host_version.rc
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++17 /EHsc /MD /O2 /W3 /DUNICODE /D_UNICODE /DWIN32 /DNDEBUG /D_WINDOWS /I "$include" /Fo"$build\\" /Fd"$build\PiperBassHighSAPI.pdb" /LD /Fe"$root\payload\PiperBassHighSAPI.dll" src\dllmain.cpp src\TtsEngine.cpp src\SherpaTts.cpp src\Register.cpp src\Log.cpp src\VoiceFolder.cpp src\SynthLocal.cpp /link /DEF:src\PiperBassHighSAPI.def "$build\version.res" sapi.lib ole32.lib advapi32.lib user32.lib uuid.lib
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++17 /EHsc /MD /O2 /W3 /DUNICODE /D_UNICODE /DWIN32 /DNDEBUG /D_WINDOWS /I "$include" /Fo"$build\\" /Fd"$build\MFPiperHost.pdb" /Fe"$root\payload\MFPiperHost.exe" src\Host.cpp src\Log.cpp "$build\SherpaTts.obj" "$build\host_version.res" /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup ole32.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++17 /EHsc /MD /O2 /W3 /DUNICODE /D_UNICODE /DWIN32 /DNDEBUG /I "$include" /Fo"$testObj\\" /Fd"$testObj\BassHighTest.pdb" /Fe"$build\BassHighTest.exe" src\TestSynth.cpp src\SherpaTts.cpp /link ole32.lib
if errorlevel 1 exit /b 1
"@
$cmdFile = Join-Path $build "compile.cmd"
Set-Content -Path $cmdFile -Value $cmd -Encoding ASCII
cmd /c $cmdFile
if ($LASTEXITCODE -ne 0) {
    throw "Compilation failed with exit code $LASTEXITCODE"
}

foreach ($name in @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll")) {
    $src = Join-Path "$env:SystemRoot\System32" $name
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $root "payload\$name") -Force
    }
}
Remove-Item (Join-Path $root "payload\PiperBassHighSAPI.lib"), (Join-Path $root "payload\PiperBassHighSAPI.exp"), (Join-Path $root "payload\PiperBassHighSAPI.pdb"), (Join-Path $root "payload\MFPiperHost.pdb"), (Join-Path $root "payload\PiperBassHighHost.exe"), (Join-Path $root "payload\PiperBassHighHost.pdb") -ErrorAction SilentlyContinue

$vcvars32 = Join-Path $vs "VC\Auxiliary\Build\vcvars32.bat"
$x86 = Join-Path $build "x86"
New-Item -ItemType Directory -Force -Path $x86 | Out-Null
$cmd32 = @"
set "PATH=C:\Windows\System32;C:\Windows"
call "$vcvars32"
if errorlevel 1 exit /b 1
cd /d "$root"
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++17 /EHsc /MD /O2 /W3 /DUNICODE /D_UNICODE /DWIN32 /DNDEBUG /D_WINDOWS /Fo"$x86\\" /Fd"$x86\PiperBassHighSAPI32.pdb" /LD /Fe"$root\payload\PiperBassHighSAPI32.dll" src\dllmain.cpp src\TtsEngine.cpp src\Register.cpp src\Log.cpp src\VoiceFolder.cpp src\SynthRemote.cpp /link /DEF:src\PiperBassHighSAPI.def "$build\version.res" sapi.lib ole32.lib advapi32.lib user32.lib uuid.lib
if errorlevel 1 exit /b 1
"@
$cmd32File = Join-Path $build "compile32.cmd"
Set-Content -Path $cmd32File -Value $cmd32 -Encoding ASCII
cmd /c $cmd32File
if ($LASTEXITCODE -ne 0) {
    throw "32-bit compilation failed with exit code $LASTEXITCODE"
}
Remove-Item (Join-Path $root "payload\PiperBassHighSAPI32.lib"), (Join-Path $root "payload\PiperBassHighSAPI32.exp"), (Join-Path $root "payload\PiperBassHighSAPI32.pdb") -ErrorAction SilentlyContinue

Write-Host "Synthesizing a sample..."
& (Join-Path $build "BassHighTest.exe") (Join-Path $root "payload") (Join-Path $build "bass-high-test.wav")
if ($LASTEXITCODE -ne 0) {
    throw "Synthesis test failed with exit code $LASTEXITCODE"
}

$packageArgs = @((Join-Path $root "package_languages.py"))
if ($AllLanguages) {
    $packageArgs += "--all"
} else {
    $packageArgs += @("--language", $Language)
}
$packageArgs += @("--dist", (Join-Path $root $DistDir))
python @packageArgs
if ($LASTEXITCODE -ne 0) {
    throw "Installer packaging failed with exit code $LASTEXITCODE"
}

Write-Host "Build finished."
