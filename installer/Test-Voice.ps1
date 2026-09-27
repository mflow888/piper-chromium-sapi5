param(
    [Parameter(Mandatory = $true)][string]$VoiceName,
    [Parameter(Mandatory = $true)][string]$Sample
)

$ErrorActionPreference = "Stop"
if (-not [Environment]::Is64BitProcess) {
    Write-Host "This shortcut must run 64-bit PowerShell."
    exit 1
}

Add-Type -AssemblyName System.Speech
$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$voice = $synth.GetInstalledVoices() | Where-Object { $_.VoiceInfo.Name -eq $VoiceName } | Select-Object -First 1
if (-not $voice) {
    Write-Host "The voice '$VoiceName' is not installed."
    Write-Host "Installed voices:"
    $synth.GetInstalledVoices() | ForEach-Object { Write-Host ("- " + $_.VoiceInfo.Name) }
    exit 1
}

$synth.SelectVoice($VoiceName)
$synth.Speak($Sample)
$synth.Dispose()
