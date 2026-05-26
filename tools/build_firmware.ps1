param(
    [string]$MirrorPath = 'C:\Loader_esp32wf',
    [string]$Fqbn = 'esp32:esp32:esp32c3:UploadSpeed=921600,FlashMode=dio,FlashSize=4M,PartitionScheme=custom,CDCOnBoot=default,DebugLevel=none',
    [switch]$KeepMirror
)

$ErrorActionPreference = 'Stop'

function Resolve-ArduinoCli {
    $candidates = @(
        'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe',
        'C:\Program Files (x86)\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $cmd = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    throw 'arduino-cli not found. Install Arduino IDE 2.x or add arduino-cli to PATH.'
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$arduinoCli = Resolve-ArduinoCli
$buildPath = Join-Path $MirrorPath '.build\esp32c3'

Write-Host "==> Mirror sketch to ASCII path: $MirrorPath"
if ((Test-Path $MirrorPath) -and (-not $KeepMirror)) {
    Remove-Item -Recurse -Force $MirrorPath
}

New-Item -ItemType Directory -Force $MirrorPath | Out-Null
$null = robocopy $repoRoot $MirrorPath /MIR /XD .git .build
if ($LASTEXITCODE -gt 7) {
    throw "robocopy failed with exit code $LASTEXITCODE"
}

New-Item -ItemType Directory -Force $buildPath | Out-Null

Write-Host '==> Compile with arduino-cli'
Write-Host "    CLI : $arduinoCli"
Write-Host "    FQBN: $Fqbn"
Write-Host "    Src : $MirrorPath"
Write-Host "    Out : $buildPath"

& $arduinoCli compile `
    --fqbn $Fqbn `
    --build-path $buildPath `
    --warnings default `
    $MirrorPath

if ($LASTEXITCODE -ne 0) {
    throw "arduino-cli compile failed with exit code $LASTEXITCODE"
}

$binPath = Join-Path $buildPath 'Loader_esp32wf.ino.bin'
$elfPath = Join-Path $buildPath 'Loader_esp32wf.ino.elf'
$partitionsPath = Join-Path $buildPath 'partitions.csv'

Write-Host ''
Write-Host '==> Build complete'
Write-Host "    BIN : $binPath"
Write-Host "    ELF : $elfPath"
Write-Host "    Part: $partitionsPath"
