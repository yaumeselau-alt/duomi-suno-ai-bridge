param([string]$FFmpegPath)
$ErrorActionPreference = 'Stop'
try {
    if (-not $FFmpegPath) {
        Add-Type -AssemblyName System.Windows.Forms
        $picker = New-Object System.Windows.Forms.OpenFileDialog
        $picker.Title = 'Select a trusted ffmpeg.exe for M4A/Opus stem audio'
        $picker.Filter = 'FFmpeg|ffmpeg.exe'
        if ($picker.ShowDialog() -ne 'OK') { exit 0 }
        $FFmpegPath = $picker.FileName
        $picker.Dispose()
    }
    $resolved = (Resolve-Path -LiteralPath $FFmpegPath).Path
    if ([IO.Path]::GetFileName($resolved) -ine 'ffmpeg.exe' -or
        -not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw 'Select an existing ffmpeg.exe from a trusted installation.'
    }
    $configDir = Join-Path $env:APPDATA 'DuomiSunoAIBridge'
    New-Item -ItemType Directory -Path $configDir -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $configDir 'ffmpeg-path.txt'), $resolved,
        (New-Object System.Text.UTF8Encoding($false)))
    Write-Host 'FFmpeg path saved. Select the stem again in the plugin.'
} catch {
    Write-Error $_
    exit 1
}

